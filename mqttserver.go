package mqttserver

/*
参考资料：
https://wenku.baidu.com/view/f103c085561252d381eb6edc.html
测试命令：
mosquitto_sub -d -h 192.168.56.1 -i ljiafe -u worldflying -P worldflying -t testtopic -v
mosquitto_pub -d -h 192.168.56.1 -u worldflying -P worldflying -i hemlow -t testtopic -m success
*/

import (
	"log"
	"net"
	"net/http"
	"strconv"
	"sync"
	"time"

	"github.com/fasthttp/websocket"
	"github.com/valyala/fasthttp"
)

type MqttClient struct {
	connType         uint8 // 0为tcp, 1为ws
	tcp              net.Conn
	ws               *websocket.Conn
	clientid         *string
	topics           []*string
	mutex            sync.RWMutex
	willtopic        *string
	willmessage      []byte
	defaultkeepalive uint16
	keepalive        uint16
}

func (c MqttClient) Write(b []byte) (int, error) {
	var num int
	var err error
	if c.connType == 0 { // tcp数据
		num, err = c.tcp.Write(b)
	} else if c.connType == 1 { // ws数据
		err = c.ws.WriteMessage(2, b) // 1为文本数据，2为二进制数据
		num = len(b)
	}
	return num, err
}

func (c MqttClient) Read(b []byte) (int, error) {
	var num int
	var err error
	if c.connType == 0 { // tcp数据
		num, err = c.tcp.Read(b)
	} else if c.connType == 1 { // ws数据
		/*
		   mt, message, e := c.ws.ReadMessage()
		   log.Println("messageType:", mt)
		*/
		_, message, e := c.ws.ReadMessage()
		num = copy(b, message)
		err = e
	}
	return num, err
}

func (c MqttClient) Close() error {
	var err error
	if c.connType == 0 { // tcp数据
		err = c.tcp.Close()
	} else if c.connType == 1 { // ws数据
		err = c.ws.Close()
	}
	return err
}

type Callback func(topic string, data []byte)

type MqttServer struct {
	mqttclients []*MqttClient
	mutex       sync.RWMutex
	useful      bool
	topics      []string
	tcpListen   *net.TCPListener
	cb          Callback
	username    string
	password    string
}

func GetMqttDataLength(data []byte, num uint32) (uint32, uint32) {
	var datalen uint32
	var offset uint32
	if num < 2 {
		return 0, 0
	}
	if (data[1] & 0x80) != 0x00 {
		if num < 3 {
			return 0, 0
		}
		if (data[2] & 0x80) != 0x00 {
			if num < 4 {
				return 0, 0
			}
			if (data[3] & 0x80) != 0x00 {
				if num < 5 {
					return 0, 0
				}
				datalen = (uint32(data[4]) << 21) | (uint32(data[3]&0x7f) << 14) | (uint32(data[2]&0x7f) << 7) | uint32(data[1]&0x7f) + 5
				offset = 5
			} else {
				datalen = (uint32(data[3]) << 14) | (uint32(data[2]&0x7f) << 7) | uint32(data[1]&0x7f) + 4
				offset = 4
			}
		} else {
			datalen = (uint32(data[2]) << 7) | uint32(data[1]&0x7f) + 3
			offset = 3
		}
	} else {
		datalen = uint32(data[1]) + 2
		offset = 2
	}
	return datalen, offset
}

func HasSliceValue(arr []*string, d string) bool {
	arrlen := len(arr)
	for i := 0; i < arrlen; i += 1 {
		if *arr[i] == d {
			return true
		}
	}
	return false
}

func RemoveSliceByValue(arrs []*string, d string) []*string {
	arrslen := len(arrs)
	for i := 0; i < arrslen; i += 1 {
		if *arrs[i] == d {
			if i == 0 {
				return arrs[1:]
			} else if i+1 == arrslen {
				return arrs[:arrslen-1]
			} else {
				copy(arrs[i:], arrs[i+1:])
				return arrs[:arrslen-1]
			}
		}
	}
	return arrs
}

func RemoveClientFromMqttClients(ms *MqttServer, mqttclient *MqttClient) {
	findclient := false
	ms.mutex.Lock()
	mqttclientslen := len(ms.mqttclients)
	for i := 0; i < mqttclientslen; i += 1 {
		if ms.mqttclients[i] == mqttclient {
			log.Println("connect lose, clientid:", *mqttclient.clientid)
			if i == 0 {
				ms.mqttclients = ms.mqttclients[1:]
			} else if i+1 == mqttclientslen {
				ms.mqttclients = ms.mqttclients[:mqttclientslen-1]
			} else {
				copy(ms.mqttclients[i:], ms.mqttclients[i+1:])
				ms.mqttclients = ms.mqttclients[:mqttclientslen-1]
			}
			findclient = true
			break
		}
	}
	ms.mutex.Unlock()
	if *mqttclient.willtopic != "" && findclient {
		PublishData(ms, *mqttclient.willtopic, mqttclient.willmessage)
	}
}

func HandleMqttClientRequest(ms *MqttServer, mqttclient *MqttClient) {
	defer mqttclient.Close()

	pack := []byte(nil)
	packlen := uint32(0)
	uselen := uint32(0)
	state := false

	for {
		data := make([]byte, 32*1024)
		n, err := mqttclient.Read(data)
		if err != nil {
			log.Println(err)
			return
		}
		num := uint32(n)
		data = data[:num]
		if packlen > 0 {
			pack = append(pack, data...)
			uselen += num
			if uselen < packlen {
				continue
			}
			data = pack
			num = uselen
			pack = nil
			packlen = 0
			uselen = 0
		}
		for {
			datalen, offset := GetMqttDataLength(data, num)
			if datalen == 0 || num < datalen {
				pack = data
				packlen = datalen
				uselen = num
				break
			}
			if state {
				mqttclient.keepalive = mqttclient.defaultkeepalive
				switch data[0] & 0xf0 {
				case 0x30: // publish
					log.Println("publish")
					if (data[0] & 0x05) != 0x00 { // 本程序不处理retain不为0，QOS大于1的报文
						log.Println("publish retain is not 0 or qos>1.")
						return
					}
					if num < offset+2 {
						log.Println("mqtt data so short.")
						return
					}
					topiclen := 256*uint32(data[offset]) + uint32(data[offset+1])
					offset += 2
					if num < offset+topiclen {
						log.Println("mqtt data so short.")
						return
					}
					topic := string(data[offset : offset+topiclen])
					offset += topiclen
					// qos为0时，无报文标识符，剩下的全部都是内容
					if (data[0] & 0x06) != 0x00 { // qos不为0时，存在报文标识符。
						_, err = mqttclient.Write([]byte{0x40, 0x02, data[offset], data[offset+1]})
						if err != nil {
							log.Println(err)
							return
						}
						offset += 2
					}
					tslen := len(ms.topics)
					for i := 0; i < tslen; i += 1 {
						if ms.topics[i] == topic {
							ms.cb(topic, data[offset:datalen])
							break
						}
					}
					ms.mutex.RLock()
					clientlen := len(ms.mqttclients)
					for i := 0; i < clientlen; i += 1 {
						ms.mqttclients[i].mutex.RLock()
						topiclen = uint32(len(ms.mqttclients[i].topics))
						for j := uint32(0); j < topiclen; j += 1 {
							if *ms.mqttclients[i].topics[j] == topic {
								ms.mqttclients[i].Write(data[:datalen])
								ms.mqttclients[i].keepalive = ms.mqttclients[i].defaultkeepalive
							}
						}
						ms.mqttclients[i].mutex.RUnlock()
					}
					ms.mutex.RUnlock()
				case 0x40:
					log.Println("puback")
				case 0x50:
					log.Println("pubrec")
				case 0x60:
					log.Println("pubrel")
				case 0x70:
					log.Println("pubcomp")
				case 0x80: // subscribe
					log.Println("subscribe")
					if (data[0] & 0x0f) != 0x02 {
						log.Println("mqtt connect package flag error")
						return
					}
					if num < offset+2 {
						log.Println("mqtt data so short.")
						return
					}
					subackdata := []byte{0x90, 0x02, data[offset], data[offset+1]} // 报文标识符
					offset += 2
					for offset < datalen {
						if num < offset+2 {
							log.Println("mqtt data so short.")
							return
						}
						topiclen := 256*uint32(data[offset]) + uint32(data[offset+1])
						offset += 2
						if num < offset+topiclen {
							log.Println("mqtt data so short.")
							return
						}
						topic := string(data[offset : offset+topiclen])
						offset += topiclen
						// log.Println("topic", topic)
						mqttclient.mutex.Lock()
						if HasSliceValue(mqttclient.topics, topic) == false {
							mqttclient.topics = append(mqttclient.topics, &topic)
						}
						mqttclient.mutex.Unlock()
						if num < offset+1 {
							log.Println("mqtt data so short.")
							return
						}
						if data[offset] != 0 && data[offset] != 0 && data[offset] != 0 {
							log.Println("subscribe qos error.")
							return
						}
						offset += 1 // 跳过迁移服务质量等级
						subackdata[1] += 1
						subackdata = append(subackdata, 0x00)
					}
					_, err = mqttclient.Write(subackdata)
					if err != nil {
						log.Println(err)
						return
					}
				case 0xa0: // unsubscribe
					log.Println("unsubscribe")
					if (data[0] & 0x0f) != 0x02 {
						log.Println("mqtt connect package flag error")
						return
					}
					if num < offset+2 {
						log.Println("mqtt data so short.")
						return
					}
					unsubackdata := []byte{0xb0, 0x02, data[offset], data[offset+1]} // 报文标识符
					offset += 2
					for offset < datalen {
						if num < offset+2 {
							log.Println("mqtt data so short.")
							return
						}
						topiclen := 256*uint32(data[offset]) + uint32(data[offset+1])
						offset += 2
						if num < offset+topiclen {
							log.Println("mqtt data so short.")
							return
						}
						topic := string(data[offset+2 : offset+2+topiclen])
						offset += topiclen
						mqttclient.mutex.Lock()
						mqttclient.topics = RemoveSliceByValue(mqttclient.topics, topic)
						mqttclient.mutex.Unlock()
					}
					_, err = mqttclient.Write(unsubackdata)
					if err != nil {
						log.Println(err)
						return
					}
				case 0xc0: // pingreq
					log.Println("pingreq")
					if (data[0]&0x0f) != 0x00 || data[1] != 0x00 || datalen != 2 {
						log.Println("mqtt connect package flag error")
						return
					}
					_, err = mqttclient.Write([]byte{0xd0, 0x00})
					if err != nil {
						log.Println(err)
						return
					}
				case 0xe0: // disconnect
					log.Println("disconnect")
					return
				default:
					log.Println("unknown mqtt package type", data[0]&0xf0)
					return
				}
			} else {
				if (data[0] & 0xf0) != 0x10 { // connect
					log.Println("client need connect first")
					_, err = mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x05})
					if err != nil {
						log.Println(err)
						return
					}
					return
				}
				log.Println("connect")
				if (data[0] & 0x0f) != 0x00 {
					log.Println("mqtt connect package flag error")
					mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x03})
					return
				}
				if num < offset+2 {
					log.Println("mqtt data so short.")
					return
				}
				protnamelen := 256*uint32(data[offset]) + uint32(data[offset+1])
				offset += 2
				if num < offset+protnamelen {
					log.Println("mqtt data so short.")
					return
				}
				log.Println("protocol name:", string(data[offset:offset+protnamelen]))
				offset += protnamelen
				if num < offset+1 {
					log.Println("mqtt data so short.")
					return
				}
				if data[offset] != 0x03 && data[offset] != 0x04 && data[offset] != 0x05 { // 0x03为mqtt3.1, 0x04为mqtt3.1.1, 0x05为mqtt5
					log.Println("no support mqtt version", data[offset])
					mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
					return
				}
				offset += 1
				if num < offset+1 {
					log.Println("mqtt data so short.")
					return
				}
				if (data[offset] & 0xc3) != 0xc2 { // 必须支持需要用户名，密码，清空session的模式。
					log.Println("need a username")
					mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x04})
					return
				}
				if (data[offset]&0x04) != 0x00 && (data[offset]&0x38) != 0x00 { // 目前该服务器仅支持will的qos为0，非保留标识
					log.Println("just support have a will and qos is 0, no retain")
					mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x03})
					return
				}
				needwill := data[offset] & 0x04
				offset += 1
				if num < offset+2 {
					log.Println("mqtt data so short.")
					return
				}
				keepalive := 256*uint16(data[offset]) + uint16(data[offset+1])
				offset += 2
				if num < offset+2 {
					log.Println("mqtt data so short.")
					return
				}
				clientidlen := 256*uint32(data[offset]) + uint32(data[offset+1])
				offset += 2
				if num < offset+clientidlen {
					log.Println("mqtt data so short.")
					return
				}
				clientid := string(data[offset : offset+clientidlen])
				log.Println("clientid:", clientid)
				offset += clientidlen
				var willtopic string
				var willmessage []byte
				if needwill != 0 { // 需要遗嘱
					if num < offset+2 {
						log.Println("mqtt data so short.")
						return
					}
					willtopiclen := 256*uint32(data[offset]) + uint32(data[offset+1])
					offset += 2
					if num < offset+willtopiclen {
						log.Println("mqtt data so short.")
						return
					}
					willtopic = string(data[offset : offset+willtopiclen])
					offset += willtopiclen
					if num < offset+2 {
						log.Println("mqtt data so short.")
						return
					}
					willmessagelen := 256*uint32(data[offset]) + uint32(data[offset+1])
					offset += 2
					if num < offset+willmessagelen {
						log.Println("mqtt data so short.")
						return
					}
					willmessage = data[offset : offset+willmessagelen]
					offset += willmessagelen
				} else {
					willtopic = ""
					willmessage = make([]byte, 0)
				}
				if num < offset+2 {
					log.Println("mqtt data so short.")
					return
				}
				userlen := 256*uint32(data[offset]) + uint32(data[offset+1])
				offset += 2
				if num < offset+userlen {
					log.Println("mqtt data so short.")
					return
				}
				user := string(data[offset : offset+userlen])
				log.Println("username", user)
				offset += userlen
				if offset < 2 {
					log.Println("mqtt data so short.")
					return
				}
				passlen := 256*uint32(data[offset]) + uint32(data[offset+1])
				offset += 2
				if num < offset+passlen {
					log.Println("mqtt data so short.")
					return
				}
				pass := string(data[offset : offset+passlen])
				log.Println("password", pass)
				offset += passlen
				if user != ms.username || pass != ms.password {
					log.Println("username or password error", user, pass)
					mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x04})
					return
				}
				ms.mutex.RLock()
				clientlen := len(ms.mqttclients)
				for i := 0; i < clientlen; i += 1 {
					if *ms.mqttclients[i].clientid == clientid {
						ms.mqttclients[i].Close()
						break
					}
				}
				ms.mutex.RUnlock()
				_, err = mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x00})
				if err != nil {
					log.Println(err)
					return
				}
				mqttclient.clientid = &clientid
				mqttclient.topics = make([]*string, 0)
				mqttclient.willtopic = &willtopic
				mqttclient.willmessage = willmessage
				mqttclient.defaultkeepalive = uint16(1.5 * float64(keepalive))
				mqttclient.keepalive = mqttclient.defaultkeepalive
				ms.mutex.Lock()
				ms.mqttclients = append(ms.mqttclients, mqttclient)
				ms.mutex.Unlock()
				defer RemoveClientFromMqttClients(ms, mqttclient)
				state = true
			}
			if num == datalen {
				break
			}
			num -= datalen
			data = data[datalen:]
		}
	}
}

func SubscribeAll(ms *MqttServer, topics []string) {
	ms.mutex.Lock()
	ms.topics = topics
	ms.mutex.Unlock()
}

func Subscribe(ms *MqttServer, topics []string) {
	ms.mutex.Lock()
	t := ms.topics
	for _, v1 := range topics {
		b := false
		for _, v2 := range t {
			if v1 == v2 {
				b = true
				break
			}
		}
		if b {
			continue
		}
		t = append(t, v1)
	}
	ms.topics = t
	ms.mutex.Unlock()
}

func Unsubscribe(ms *MqttServer, topics []string) {
	ms.mutex.Lock()
	t := ms.topics
	tlen := len(t)
	for _, v1 := range topics {
		for n, v2 := range t {
			if v1 == v2 {
				copy(t[:n], t[n+1:])
				t = t[:tlen-1]
				tlen--
				break
			}
		}
	}
	ms.topics = t
	ms.mutex.Unlock()
}

func StartTcpServer(ms *MqttServer, tcpListen *net.TCPListener) {
	for {
		tcpclient, err := (*tcpListen).Accept()
		if err != nil {
			log.Println(err)
			continue
		}
		go HandleMqttClientRequest(ms, &MqttClient{connType: 0, tcp: tcpclient})
	}
}

func StartServer(tcpport uint16, username string, password string, topics []string, cb Callback) *MqttServer {
	log.SetFlags(log.LstdFlags | log.Lshortfile)
	ms := &MqttServer{
		mqttclients: make([]*MqttClient, 0),
		topics:      topics,
		cb:          cb,
		username:    username,
		password:    password,
		useful:      true,
	}
	go CheckMqttClients(ms)
	if tcpport != 0 {
		p := strconv.Itoa(int(tcpport))
		addr, _ := net.ResolveTCPAddr("tcp4", ":"+p)
		tcpListen, err := net.ListenTCP("tcp", addr)
		if err != nil {
			log.Println(err)
			return nil
		}
		go StartTcpServer(ms, tcpListen)
		ms.tcpListen = tcpListen
	} else {
		ms.tcpListen = nil
	}
	return ms
}

type WebSocketParam struct {
	timeout  time.Duration
	rbufsize int
	wbufsize int
}

func ListenNetHttpWebSocket(ms *MqttServer, w http.ResponseWriter, r *http.Request, params *WebSocketParam) error {
	var timeout time.Duration
	var rbufsize, wbufsize int
	if params != nil {
		if params.timeout == 0 {
			timeout = 5
		} else {
			timeout = params.timeout
		}
		if params.rbufsize == 0 {
			rbufsize = 1024
		} else {
			rbufsize = params.rbufsize
		}
		if params.wbufsize == 0 {
			wbufsize = 1024
		} else {
			wbufsize = params.wbufsize
		}
	} else {
		timeout = 5
		rbufsize = 1024
		wbufsize = 1024
	}
	upgrader := websocket.Upgrader{
		HandshakeTimeout: timeout * time.Second,
		ReadBufferSize:   rbufsize,
		WriteBufferSize:  wbufsize,
		Subprotocols:     []string{"mqtt", "mqttv3.1"},
		CheckOrigin: func(r *http.Request) bool {
			return true
		},
		EnableCompression: true,
	}
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println(err)
		return err
	}
	go HandleMqttClientRequest(ms, &MqttClient{connType: 1, ws: conn})
	return nil
}

func ListenFastHttpWebSocket(ms *MqttServer, ctx *fasthttp.RequestCtx, params *WebSocketParam) error {
	var timeout time.Duration
	var rbufsize, wbufsize int
	if params != nil {
		if params.timeout == 0 {
			timeout = 5
		} else {
			timeout = params.timeout
		}
		if params.rbufsize == 0 {
			rbufsize = 1024
		} else {
			rbufsize = params.rbufsize
		}
		if params.wbufsize == 0 {
			wbufsize = 1024
		} else {
			wbufsize = params.wbufsize
		}
	} else {
		timeout = 5
		rbufsize = 1024
		wbufsize = 1024
	}
	upgrader := websocket.FastHTTPUpgrader{
		HandshakeTimeout: timeout * time.Second,
		ReadBufferSize:   rbufsize,
		WriteBufferSize:  wbufsize,
		Subprotocols:     []string{"mqtt", "mqttv3.1"},
		CheckOrigin: func(ctx *fasthttp.RequestCtx) bool {
			return true
		},
		EnableCompression: true,
	}
	err := upgrader.Upgrade(ctx, func(conn *websocket.Conn) {
		HandleMqttClientRequest(ms, &MqttClient{connType: 1, ws: conn})
	})
	if err != nil {
		log.Println(err)
	}
	return err
}

func CheckMqttClients(ms *MqttServer) {
	for ms.useful {
		time.Sleep(1 * time.Second)
		ms.mutex.RLock()
		mqttclientslen := len(ms.mqttclients)
		for i := 0; i < mqttclientslen; i += 1 {
			if ms.mqttclients[i].defaultkeepalive > 0 {
				ms.mqttclients[i].keepalive--
				if ms.mqttclients[i].keepalive == 0 {
					ms.mqttclients[i].Close()
				}
			}
		}
		ms.mutex.RUnlock()
	}
}

func CloseServer(ms *MqttServer) {
	ms.mutex.RLock()
	mqttclientslen := len(ms.mqttclients)
	for i := 0; i < mqttclientslen; i += 1 {
		ms.mqttclients[i].Close()
	}
	ms.mutex.RUnlock()
	if ms.tcpListen != nil {
		ms.tcpListen.Close()
	}
	ms.useful = false
}

func PublishData(ms *MqttServer, topic string, msg []byte) {
	tslen := len(ms.topics)
	for i := 0; i < tslen; i += 1 {
		if ms.topics[i] == topic {
			ms.cb(topic, msg)
		}
	}
	var b []byte
	var offset uint32
	topiclen := uint32(len(topic))
	msglen := uint32(len(msg))
	num := 2 + topiclen + msglen
	if num < 0x80 {
		b = make([]byte, num+2)
		offset = 2
	} else if num < 0x4000 {
		b = make([]byte, num+3)
		offset = 3
	} else if num < 0x200000 {
		b = make([]byte, num+4)
		offset = 4
	} else {
		b = make([]byte, num+5)
		offset = 5
	}
	n := 0
	for num > 0 {
		b[n] |= 0x80
		n += 1
		b[n] = byte(num & 0x7f)
		num >>= 7
	}
	b[0] = 0x30
	b[offset] = byte(topiclen >> 8)
	b[offset+1] = byte(topiclen)
	offset += 2
	copy(b[offset:], []byte(topic))
	offset += topiclen
	copy(b[offset:], []byte(msg))
	ms.mutex.RLock()
	clientlen := len(ms.mqttclients)
	for i := 0; i < clientlen; i += 1 {
		ms.mqttclients[i].mutex.RLock()
		topiclen := uint32(len(ms.mqttclients[i].topics))
		for j := uint32(0); j < topiclen; j += 1 {
			if *ms.mqttclients[i].topics[j] == topic {
				ms.mqttclients[i].Write(b)
			}
		}
		ms.mqttclients[i].mutex.RUnlock()
	}
	ms.mutex.RUnlock()
}
