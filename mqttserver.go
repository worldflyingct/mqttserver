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
    "github.com/gorilla/websocket"
)

type Client struct {
    connType int // 0为tcp, 1为ws
    tcp net.Conn
    ws *websocket.Conn
}

func (c Client) Write (b []byte) (int, error) {
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

func (c Client) Read (b []byte) (int, error) {
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

func (c Client) Close () (error) {
    var err error
    if c.connType == 0 { // tcp数据
        err = c.tcp.Close()
    } else if c.connType == 1 { // ws数据
        err = c.ws.Close()
    }
    return err
}

type MqttClient struct {
    client *Client
    clientid *string
    topics []*string
    willtopic *string
    willmessage []byte
}

type Callback func (topic string, data []byte)

type MqttServer struct {
    mqttclients []*MqttClient
    topics []string
    tcpListen *net.TCPListener
    srv *http.Server
    cb Callback
}

func GetMqttDataLength (b []byte) (uint32, uint32)  {
    var datalen uint32
    var offset uint32
    if (b[1] & 0x80) != 0x00 {
        if (b[2] & 0x80) != 0x00 {
            if (b[3] & 0x80) != 0x00 {
                if (b[4] & 0x80) != 0x00 {
                    return 0, 0
                } else {
                    offset = 5
                    datalen = 128 * 128 * 128 * uint32(b[4]) + 128 * 128 * uint32(b[3] & 0x7f) + 128 * uint32(b[2] & 0x7f) + uint32(b[1] & 0x7f) + 2
                }
            } else {
                offset = 4
                datalen = 128 * 128 * uint32(b[3]) + 128 * uint32(b[2] & 0x7f) + uint32(b[1] & 0x7f) + 2
            }
        } else {
            offset = 3
            datalen = 128 * uint32(b[2]) + uint32(b[1] & 0x7f) + 2
        }
    } else {
        offset = 2
        datalen = uint32(b[1]) + 2
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
            mqttclient.client.Close();
            return
        }
    }
}

func HandleMqttClientRequest (ms *MqttServer, client *Client, username *string, password *string) {
    defer client.Close()

    var b [32*1024]byte
    _, err := client.Read(b[:])
    if err != nil {
        log.Println(err)
        return
    }

    datalen, offset := GetMqttDataLength(b[:])
    if datalen == 0 {
        log.Println(err)
        return
    }
    if (b[0] & 0xf0) != 0x10 { // connect
        log.Println("client need connect first")
        return
    }
    log.Println("connect")
    if (b[0] & 0x0f) != 0x00 {
        log.Println("mqtt connect package flag error")
        client.Write([]byte{0x20, 0x02, 0x00, 0x01})
        return
    }
    protnamelen := 256 * uint32(b[offset]) + uint32(b[offset+1])
    log.Println("protocol name:", string(b[offset+2:offset+2+protnamelen]))
    offset += 2+protnamelen
    if b[offset] != 0x03 && b[offset] != 0x04 && b[offset] != 0x05 { // 0x03为mqtt3.1, 0x04为mqtt3.1.1, 0x05为mqtt5
        log.Println("no support mqtt version", b[offset])
        client.Write([]byte{0x20, 0x02, 0x00, 0x01})
        return
    }
    offset += 1
    if (b[offset] & 0x80) != 0x80  { // 判断是否支持用户名，如果不支持，服务器直接断开连接
        log.Println("need a username")
        client.Write([]byte{0x20, 0x02, 0x00, 0x04})
        return
    }
    if (b[offset] & 0x40) != 0x40  { // 判断是否支持密码，如果不支持，服务器直接断开连接
        log.Println("need a password")
        client.Write([]byte{0x20, 0x02, 0x00, 0x04})
        return
    }
    if (b[offset] & 0x04) != 0x00 && (b[offset] & 0x38) != 0x00 { // 目前该服务器仅支持will的qos为0，非保留标识
        log.Println("just support have a will and qos is 0, no retain")
        client.Write([]byte{0x20, 0x02, 0x00, 0x01})
        return
    }
    if (b[offset] & 0x02) != 0x02 { // Clean Session位必须位1
        log.Println("clean session must 1")
        client.Write([]byte{0x20, 0x02, 0x00, 0x01})
        return
    }
    if (b[offset] & 0x01) == 0x01 { // 保留位错误
        log.Println("Reserved Bit error")
        client.Write([]byte{0x20, 0x02, 0x00, 0x01})
        return
    }
    needwill := b[offset] & 0x04
    offset += 1
    offset += 2 // 舍弃keepalive数据的读取
    clientidlen := 256 * uint32(b[offset]) + uint32(b[offset+1])
    clientid := string(b[offset+2 : offset+2+clientidlen])
    log.Println("clientid:", clientid)
    offset += 2+clientidlen
    clientlen := len(ms.mqttclients)
    for i := 0 ; i < clientlen ; i++ {
        if *ms.mqttclients[i].clientid == clientid {
            log.Println("clientid has exist")
            client.Write([]byte{0x20, 0x02, 0x00, 0x02})
            return
        }
    }
    var willtopic string
    var willmessage []byte
    if needwill != 0 { // 需要遗嘱
        willtopiclen := 256 * uint32(b[offset]) + uint32(b[offset+1])
        willtopic = string(b[offset+2 : offset+2+willtopiclen])
        offset += 2+willtopiclen
        willmessagelen := 256 * uint32(b[offset]) + uint32(b[offset+1])
        willmessage = b[offset+2 : offset+2+willtopiclen]
        offset += 2+willmessagelen
    } else {
        willtopic = ""
        willmessage = make([]byte, 0)
    }
    userlen := 256 * uint32(b[offset]) + uint32(b[offset+1])
    user := string(b[offset+2 : offset+2+userlen])
    log.Println("username", user)
    offset += 2+userlen
    passlen := 256 * uint32(b[offset]) + uint32(b[offset+1])
    pass := string(b[offset+2 : offset+2+passlen])
    log.Println("password", pass)
    offset += 2+passlen
    if user != *username || pass != *password {
        log.Println("username or password error", user, pass)
        client.Write([]byte{0x20, 0x02, 0x00, 0x04})
        return
    }
    client.Write([]byte{0x20, 0x02, 0x00, 0x00})
    mqttclient := &MqttClient{client, &clientid, make([]*string, 0), &willtopic, willmessage}
    ms.mqttclients = append(ms.mqttclients, mqttclient)
    defer RemoveClientFromMqttClients(ms, mqttclient)
    for {
        _, err := client.Read(b[:])
        if err != nil {
            log.Println("tcp read fail.")
            return
        }

        datalen, offset := GetMqttDataLength(b[:])
        if datalen == 0 {
            continue
        }
        switch b[0] & 0xf0 {
            case 0x30: // publish
                log.Println("publish")
                if (b[0] & 0x0f) != 0x00 { // 本程序不处理dup，qos与retain不为0的报文
                    log.Println("publish dup,qos and retain is not 0.")
                    continue
                }
                topiclen := 256 * uint32(b[offset]) + uint32(b[offset+1])
                topic := string(b[offset+2 : offset+2+topiclen])
                offset += 2+topiclen
                // qos为0时，无报文标识符，剩下的全部都是内容
                if (b[0] & 0x06) != 0x00 { // qos不为0时，存在报文标识符。
                    offset += 2
                }
                tslen := len(ms.topics)
                for i := 0 ; i < tslen ; i++ {
                    if ms.topics[i] == topic {
                        ms.cb(topic, b[offset:datalen])
                    }
                }
                clientlen := len(ms.mqttclients)
                for i := 0 ; i < clientlen ; i++ {
                    topiclen = uint32(len(ms.mqttclients[i].topics))
                    for j := uint32(0) ; j < topiclen ; j++ {
                        if *ms.mqttclients[i].topics[j] == topic {
                            ms.mqttclients[i].client.Write(b[:datalen])
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
                if (b[0] & 0x0f) != 0x02 {
                    log.Println("mqtt connect package flag error")
                    client.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                subackdata := []byte{0x90, 0x02, b[offset], b[offset+1]} // 报文标识符
                offset += 2
                for offset < datalen {
                    topiclen := 256 * uint32(b[offset]) + uint32(b[offset+1])
                    topic := string(b[offset+2 : offset+2+topiclen])
                    offset += 2+topiclen
                    log.Println("topic", topic)
                    if HasSliceValue(mqttclient.topics, topic) == false {
                        mqttclient.topics = append(mqttclient.topics, &topic)
                    }
                    if b[offset] != 0 {
                        log.Println("only support Requested QoS is 0.")
                        client.Write([]byte{0x20, 0x02, 0x00, 0x01})
                        return
                    }
                    offset += 1 // 跳过迁移服务质量等级
                    subackdata[1]++
                    subackdata = append(subackdata, 0x00)
                }
                client.Write(subackdata)
            case 0xa0: // unsubscribe
                log.Println("unsubscribe")
                if (b[0] & 0x0f) != 0x02 {
                    log.Println("mqtt connect package flag error")
                    client.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                subackdata := []byte{0xb0, 0x02, b[offset], b[offset+1]} // 报文标识符
                offset += 2
                for offset < datalen {
                    topiclen := 256 * uint32(b[offset]) + uint32(b[offset+1])
                    topic := string(b[offset+2 : offset+2+topiclen])
                    offset += 2+topiclen
                    mqttclient.topics = RemoveSliceByValue(mqttclient.topics, topic)
                }
                client.Write(subackdata)
            case 0xc0: // pingreq
                log.Println("pingreq")
                if (b[0] & 0x0f) != 0x00 || b[1] != 0x00 || datalen != 2 {
                    log.Println("mqtt connect package flag error")
                    client.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                client.Write([]byte{0xd0, 0x00})
            case 0xe0: // disconnect
                log.Println("disconnect")
                if (b[0] & 0x0f) != 0x00 || b[1] != 0x00 || datalen != 2 {
                    log.Println("mqtt connect package flag error")
                    client.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
            default:
                log.Println("unknown mqtt package type", b[0] & 0xf0)
                return
        }
    }
}

func StartTcpServer (ms *MqttServer, tcpListen *net.TCPListener, username *string, password *string) {
    for {
        tcpclient, err := (*tcpListen).Accept()
        if err != nil {
            log.Println(err)
            continue
        }
        go HandleMqttClientRequest(ms, &Client{0, tcpclient, &websocket.Conn{}}, username, password)
    }
}

func StartServer (tcpport int, wsport int, username string, password string, topics []string, cb Callback) *MqttServer {
    log.SetFlags(log.LstdFlags | log.Lshortfile)
    ms := &MqttServer{
        mqttclients: make([]*MqttClient, 0),
        topics: topics,
        cb: cb,
    }
    if tcpport != 0 {
        p := strconv.Itoa(tcpport)
        addr, _ := net.ResolveTCPAddr("tcp4", ":" + p)
        tcpListen, err := net.ListenTCP("tcp", addr)
        if err != nil {
            log.Panic(err)
        }
        go StartTcpServer(ms, tcpListen, &username, &password)
        ms.tcpListen = tcpListen
    } else {
        ms.tcpListen = nil
    }
    if wsport != 0 {
        upgrader := websocket.Upgrader{
            Subprotocols: []string{"mqtt", "mqttv3.1"},
            CheckOrigin: func(r *http.Request) bool {
                return true
            },
        }
        mux := http.NewServeMux()
        mux.HandleFunc("/mqtt", func (w http.ResponseWriter, r *http.Request) {
            wsclient, err := upgrader.Upgrade(w, r, nil)
            if err != nil {
                log.Println("err msg:", err)
                return
            }
            HandleMqttClientRequest(ms, &Client{1, nil, wsclient}, &username, &password)
        })
        mux.HandleFunc("/", func (w http.ResponseWriter, r *http.Request) {
            w.Write([]byte("The Worldflying Mqtt Server."))
        })
        srv := &http.Server{
            Addr: ":" + strconv.Itoa(wsport),
            Handler: mux,
        }
        go srv.ListenAndServe ()
        ms.srv = srv
    } else {
        ms.srv = nil
    }
    return ms
}

func CloseServer (ms *MqttServer) {
    if ms.srv != nil {
        ms.srv.Close()
    }
    if ms.tcpListen != nil {
        ms.tcpListen.Close()
    }
}

func PublishData (ms *MqttServer, topic string, d []byte) {
    tslen := len(ms.topics)
    for i := 0 ; i < tslen ; i++ {
        if ms.topics[i] == topic {
            ms.cb(topic, d)
        }
    }
    var b []byte
    topiclen := len(topic)
    num := 2 + topiclen + len(d)
    if num < 127 {
        b = append([]byte{0x30, byte(num), byte(topiclen / 256), byte(topiclen % 256)}, topic...)
    } else if num < 16384 {
        l1 := num % 128 + 128
        l2 := num / 128
        b = append([]byte{0x30, byte(l1), byte(l2), byte(topiclen / 256), byte(topiclen % 256)}, topic...)
    } else {
        l1 := num % 128 + 128
        l2 := num / 128 % 128 + 128
        l3 := num / 16384
        b = append([]byte{0x30, byte(l1), byte(l2), byte(l3), byte(topiclen / 256), byte(topiclen % 256)}, topic...)
    }
    b = append(b, d...)
    clientlen := len(ms.mqttclients)
    for i := 0 ; i < clientlen ; i++ {
        topiclen := uint32(len(ms.mqttclients[i].topics))
        for j := uint32(0) ; j < topiclen ; j++ {
            if *ms.mqttclients[i].topics[j] == topic {
                ms.mqttclients[i].client.Write(b)
            }
        }
    }
}
