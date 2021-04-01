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
    "strconv"
    "time"
    "net/http"
    "github.com/valyala/fasthttp"
    "github.com/fasthttp/websocket"
)

type MqttClient struct {
    connType int // 0为tcp, 1为ws
    tcp net.Conn
    ws *websocket.Conn
    clientid *string
    topics []*string
    willtopic *string
    willmessage []byte
}

func (c MqttClient) Write (b []byte) (int, error) {
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

func (c MqttClient) Read (b []byte) (int, error) {
    var num int
    var err error
    if c.connType == 0 { // tcp数据
        num, err = c.tcp.Read(b)
    } else if c.connType == 1 { // ws数据
        mt, message, e := c.ws.ReadMessage()
        log.Println("messageType:", mt)
        num = copy(b, message)
        err = e
    }
    return num, err
}

func (c MqttClient) Close () (error) {
    var err error
    if c.connType == 0 { // tcp数据
        err = c.tcp.Close()
    } else if c.connType == 1 { // ws数据
        err = c.ws.Close()
    }
    return err
}

type Callback func (topic string, data []byte)

type MqttServer struct {
    mqttclients []*MqttClient
    topics []string
    tcpListen *net.TCPListener
    cb Callback
    username *string
    password *string
}

func GetMqttDataLength (data []byte, num uint32) (uint32, uint32)  {
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
                datalen = 128 * 128 * 128 * uint32(data[4]) + 128 * 128 * uint32(data[3] & 0x7f) + 128 * uint32(data[2] & 0x7f) + uint32(data[1] & 0x7f) + 5
                offset = 5
            } else {
                datalen = 128 * 128 * uint32(data[3]) + 128 * uint32(data[2] & 0x7f) + uint32(data[1] & 0x7f) + 4
                offset = 4
            }
        } else {
            datalen = 128 * uint32(data[2]) + uint32(data[1] & 0x7f) + 3
            offset = 3
        }
    } else {
        datalen = uint32(data[1]) + 2
        offset = 2
    }
    return datalen, offset
}

func HasSliceValue (arr []*string, d string) bool {
    arrlen := len(arr)
    for i := 0 ; i < arrlen ; i++ {
        if *arr[i] == d {
            return true
        }
    }
    return false
}

func RemoveSliceByValue (arrs []*string, d string) []*string {
    arrslen := len(arrs)
    for i := 0 ; i < arrslen ; i++ {
        if *arrs[i] == d {
            return append(arrs[:i], arrs[i+1:]...)
        }
    }
    return arrs
}

func RemoveClientFromMqttClients (ms *MqttServer, mqttclient *MqttClient) {
    mqttclientslen := len(ms.mqttclients)
    for i := 0 ; i < mqttclientslen ; i++ {
        if ms.mqttclients[i] == mqttclient {
            log.Println("connect lose, clientid:", *mqttclient.clientid)
            ms.mqttclients = append(ms.mqttclients[:i], ms.mqttclients[i+1:]...)
            if *mqttclient.willtopic != "" {
                PublishData(ms, *mqttclient.willtopic, mqttclient.willmessage)
            }
            time.Sleep(3 * time.Second)
            mqttclient.Close();
            return
        }
    }
}

func HandleMqttClientRequest (ms *MqttServer, mqttclient *MqttClient) {
    defer mqttclient.Close()

    var pack []byte
    var packlen uint32
    var uselen uint32
    pack = nil
    packlen = 0
    uselen = 0
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
            if datalen == 0 {
                return
            }
            if num < datalen {
                pack = data
                packlen = datalen
                uselen = num
                break
            }
            if state {
                switch data[0] & 0xf0 {                
                    case 0x30: // publish
                        log.Println("publish")
                        if (data[0] & 0x0f) != 0x00 { // 本程序不处理dup，qos与retain不为0的报文
                            log.Println("publish dup,qos or retain is not 0.")
                            return
                        }
                        topiclen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                        topic := string(data[offset+2 : offset+2+topiclen])
                        offset += 2+topiclen
                        // qos为0时，无报文标识符，剩下的全部都是内容
                        if (data[0] & 0x06) != 0x00 { // qos不为0时，存在报文标识符。
                            offset += 2
                        }
                        tslen := len(ms.topics)
                        for i := 0 ; i < tslen ; i++ {
                            if ms.topics[i] == topic {
                                ms.cb(topic, data[offset:datalen])
                            }
                        }
                        clientlen := len(ms.mqttclients)
                        for i := 0 ; i < clientlen ; i++ {
                            topiclen = uint32(len(ms.mqttclients[i].topics))
                            for j := uint32(0) ; j < topiclen ; j++ {
                                if *ms.mqttclients[i].topics[j] == topic {
                                    ms.mqttclients[i].Write(data[:datalen])
                                }
                            }
                        }
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
                            mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                            return
                        }
                        subackdata := []byte{0x90, 0x02, data[offset], data[offset+1]} // 报文标识符
                        offset += 2
                        for offset < datalen {
                            topiclen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                            topic := string(data[offset+2 : offset+2+topiclen])
                            offset += 2+topiclen
                            log.Println("topic", topic)
                            if HasSliceValue(mqttclient.topics, topic) == false {
                                mqttclient.topics = append(mqttclient.topics, &topic)
                            }
                            if data[offset] != 0 {
                                log.Println("only support Requested QoS is 0.")
                                mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                                return
                            }
                            offset += 1 // 跳过迁移服务质量等级
                            subackdata[1]++
                            subackdata = append(subackdata, 0x00)
                        }
                        mqttclient.Write(subackdata)
                    case 0xa0: // unsubscribe
                        log.Println("unsubscribe")
                        if (data[0] & 0x0f) != 0x02 {
                            log.Println("mqtt connect package flag error")
                            mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                            return
                        }
                        unsubackdata := []byte{0xb0, 0x02, data[offset], data[offset+1]} // 报文标识符
                        offset += 2
                        for offset < datalen {
                            topiclen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                            topic := string(data[offset+2 : offset+2+topiclen])
                            offset += 2+topiclen
                            mqttclient.topics = RemoveSliceByValue(mqttclient.topics, topic)
                        }
                        mqttclient.Write(unsubackdata)
                    case 0xc0: // pingreq
                        log.Println("pingreq")
                        if (data[0] & 0x0f) != 0x00 || data[1] != 0x00 || datalen != 2 {
                            log.Println("mqtt connect package flag error")
                            mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                            return
                        }
                        mqttclient.Write([]byte{0xd0, 0x00})
                    case 0xe0: // disconnect
                        log.Println("disconnect")
                        if (data[0] & 0x0f) != 0x00 || data[1] != 0x00 || datalen != 2 {
                            log.Println("mqtt connect package flag error")
                            mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                            return
                        }
                    default:
                        log.Println("unknown mqtt package type", data[0] & 0xf0)
                        return
                }
            } else {            
                if (data[0] & 0xf0) != 0x10 { // connect
                    log.Println("client need connect first")
                    return
                }
                log.Println("connect")
                if (data[0] & 0x0f) != 0x00 {
                    log.Println("mqtt connect package flag error")
                    mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                protnamelen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                log.Println("protocol name:", string(data[offset+2:offset+2+protnamelen]))
                offset += 2+protnamelen
                if data[offset] != 0x03 && data[offset] != 0x04 && data[offset] != 0x05 { // 0x03为mqtt3.1, 0x04为mqtt3.1.1, 0x05为mqtt5
                    log.Println("no support mqtt version", data[offset])
                    mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                offset += 1
                if (data[offset] & 0x80) != 0x80  { // 判断是否支持用户名，如果不支持，服务器直接断开连接
                    log.Println("need a username")
                    mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x04})
                    return
                }
                if (data[offset] & 0x40) != 0x40  { // 判断是否支持密码，如果不支持，服务器直接断开连接
                    log.Println("need a password")
                    mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x04})
                    return
                }
                if (data[offset] & 0x04) != 0x00 && (data[offset] & 0x38) != 0x00 { // 目前该服务器仅支持will的qos为0，非保留标识
                    log.Println("just support have a will and qos is 0, no retain")
                    mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                if (data[offset] & 0x02) != 0x02 { // Clean Session位必须位1
                    log.Println("clean session must 1")
                    mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                if (data[offset] & 0x01) == 0x01 { // 保留位错误
                    log.Println("Reserved Bit error")
                    mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                needwill := data[offset] & 0x04
                offset += 1
                offset += 2 // 舍弃keepalive数据的读取
                clientidlen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                clientid := string(data[offset+2 : offset+2+clientidlen])
                log.Println("clientid:", clientid)
                offset += 2+clientidlen
                clientlen := len(ms.mqttclients)
                for i := 0 ; i < clientlen ; i++ {
                    if *ms.mqttclients[i].clientid == clientid {
                        log.Println("clientid has exist")
                        mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x02})
                        return
                    }
                }
                var willtopic string
                var willmessage []byte
                if needwill != 0 { // 需要遗嘱
                    willtopiclen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                    willtopic = string(data[offset+2 : offset+2+willtopiclen])
                    offset += 2+willtopiclen
                    willmessagelen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                    willmessage = data[offset+2 : offset+2+willtopiclen]
                    offset += 2+willmessagelen
                } else {
                    willtopic = ""
                    willmessage = make([]byte, 0)
                }
                userlen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                user := string(data[offset+2 : offset+2+userlen])
                log.Println("username", user)
                offset += 2+userlen
                passlen := 256 * uint32(data[offset]) + uint32(data[offset+1])
                pass := string(data[offset+2 : offset+2+passlen])
                log.Println("password", pass)
                offset += 2+passlen
                if user != *ms.username || pass != *ms.password {
                    log.Println("username or password error", user, pass)
                    mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x04})
                    return
                }
                mqttclient.Write([]byte{0x20, 0x02, 0x00, 0x00})
                mqttclient.clientid = &clientid
                mqttclient.topics = make([]*string, 0)
                mqttclient.willtopic = &willtopic
                mqttclient.willmessage = willmessage
                ms.mqttclients = append(ms.mqttclients, mqttclient)
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

func StartTcpServer (ms *MqttServer, tcpListen *net.TCPListener) {
    for {
        tcpclient, err := (*tcpListen).Accept()
        if err != nil {
            log.Println(err)
            continue
        }
        go HandleMqttClientRequest(ms, &MqttClient{connType:0, tcp:tcpclient})
    }
}

func StartServer (tcpport int, username string, password string, topics []string, cb Callback) *MqttServer {
    log.SetFlags(log.LstdFlags | log.Lshortfile)
    ms := &MqttServer{
        mqttclients: make([]*MqttClient, 0),
        topics: topics,
        cb: cb,
        username: &username,
        password: &password,
    }
    if tcpport != 0 {
        p := strconv.Itoa(tcpport)
        addr, _ := net.ResolveTCPAddr("tcp4", ":" + p)
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
    timeout time.Duration
    rbufsize int
    wbufsize int
}

func ListenNetHttpWebSocket (ms *MqttServer, w http.ResponseWriter, r *http.Request, params *WebSocketParam) (error) {
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
        ReadBufferSize:  rbufsize,
        WriteBufferSize: wbufsize,
        Subprotocols: []string{"mqtt", "mqttv3.1"},
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
    go HandleMqttClientRequest(ms, &MqttClient{connType:1, ws:conn})
    return nil
}

func ListenFastHttpWebSocket (ms *MqttServer, ctx *fasthttp.RequestCtx, params *WebSocketParam) (error) {
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
    upgrader := websocket.FastHTTPUpgrader {
        HandshakeTimeout: timeout * time.Second,
        ReadBufferSize:  rbufsize,
        WriteBufferSize: wbufsize,
        Subprotocols: []string{"mqtt", "mqttv3.1"},
        CheckOrigin: func(ctx *fasthttp.RequestCtx) bool {
            return true
        },
        EnableCompression: true,
    }
    err := upgrader.Upgrade(ctx, func (conn *websocket.Conn) {
        HandleMqttClientRequest(ms, &MqttClient{connType:1, ws:conn})
    })
    if err != nil {
        log.Println(err)
    }
    return err
}

func CloseServer (ms *MqttServer) {
    mqttclientslen := len(ms.mqttclients)
    for i := 0 ; i < mqttclientslen ; i++ {
        ms.mqttclients[i].Close();
    }
    if ms.tcpListen != nil {
        ms.tcpListen.Close()
    }
}

func PublishData (ms *MqttServer, topic string, msg []byte) {
    tslen := len(ms.topics)
    for i := 0 ; i < tslen ; i++ {
        if ms.topics[i] == topic {
            ms.cb(topic, msg)
        }
    }
    var b []byte
    var offset uint32
    topiclen := uint32(len(topic))
    msglen := uint32(len(msg))
    num := 2 + topiclen + msglen
    if num < 127 {
        b = make([]byte, num + 2)
        offset = 3
    } else if num < 0x4000 {
        b = make([]byte, num + 3)
        offset = 4
    } else if num < 0x200000 {
        b = make([]byte, num + 4)
        offset = 5
    } else {
        b = make([]byte, num + 5)
        offset = 6
    }
    n := 0
    for num > 0 {
        b[n] |= 0x80
        n++
        b[n] = byte(num & 0x7f)
        num >>= 7
    }
    b[0] = 0x30
    b[offset] = byte(topiclen>>8)
    b[offset+1] = byte(topiclen)
    offset += 2
    for i := uint32(0) ; i < topiclen ; i++ {
        b[offset+i] = topic[i]
    }
    offset += topiclen
    for i := uint32(0) ; i < msglen ; i++ {
        b[offset+i] = msg[i]
    }
    clientlen := len(ms.mqttclients)
    for i := 0 ; i < clientlen ; i++ {
        topiclen := uint32(len(ms.mqttclients[i].topics))
        for j := uint32(0) ; j < topiclen ; j++ {
            if *ms.mqttclients[i].topics[j] == topic {
                ms.mqttclients[i].Write(b)
            }
        }
    }
}
