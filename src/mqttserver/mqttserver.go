package mqttserver

import (
    "log"
    "net"
    "strconv"
)

type MqttClient struct {
    client net.Conn
    clientid string
    topic []string
    willtopic string
    willmessage string
}

var mqttclients []MqttClient = make([]MqttClient, 0)

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
                    datalen = 128 * 128 * 128 * uint32(b[4]) + 128 * 128 * uint32(b[3] & 0x7f) + 128 * uint32(b[2] & 0x7f) + uint32(b[1] & 0x7f)
                }
            } else {
                offset = 4
                datalen = 128 * 128 * uint32(b[3]) + 128 * uint32(b[2] & 0x7f) + uint32(b[1] & 0x7f)
            }
        } else {
            offset = 3
            datalen = 128 * uint32(b[2]) + uint32(b[1] & 0x7f)
        }
    } else {
        offset = 2
        datalen = uint32(b[1])
    }
    return datalen, offset
}

func HandleTcpClientRequest (client net.Conn, username string, password string) {
    defer client.Close()

    var b [32*1024]byte
    _, err := client.Read(b[:])
    if err != nil {
        log.Println("mqtt package length error")
        return
    }

    datalen, offset := GetMqttDataLength(b[:])
    if datalen == 0 {
        log.Println(err)
        return
    }
    var mqttclient MqttClient
    if (b[0] & 0xf0) == 0x10 { // connect
        if (b[0] & 0x0f) != 0x00 {
            log.Println("mqtt connect package flag error")
            client.Write([]byte{0x20, 0x02, 0x00, 0x01})
            return
        }
        if b[offset] != 0x00 || b[offset+1] != 0x04 || b[offset+2] != 0x4d || b[offset+3] != 0x51 || b[offset+4] != 0x54 || b[offset+5] != 0x54 {
            log.Println("mqtt connect package Protocol Name error")
            client.Write([]byte{0x20, 0x02, 0x00, 0x01})
            return
        }
        if b[offset+6] != 0x03 || b[offset+6] != 0x04 || b[offset+6] != 0x05 { // 0x03为mqtt3.1, 0x04为mqtt3.1.1, 0x05为mqtt5
            log.Println("no support mqtt version")
            client.Write([]byte{0x20, 0x02, 0x00, 0x01})
            return
        }
        if (b[offset+7] & 0x80) != 0x80  { // 判断是否支持用户名，如果不支持，服务器直接断开连接
            log.Println("need a username")
            client.Write([]byte{0x20, 0x02, 0x00, 0x04})
            return
        }
        if (b[offset+7] & 0x40) != 0x40  { // 判断是否支持密码，如果不支持，服务器直接断开连接
            log.Println("need a password")
            client.Write([]byte{0x20, 0x02, 0x00, 0x04})
            return
        }
        if (b[offset+7] & 0x3c) != 0x04 { // 目前该服务器仅支持必须启动will，且will的qos为0，非保留标识
            log.Println("just support have a will and qos is 0, no retain")
            client.Write([]byte{0x20, 0x02, 0x00, 0x01})
            return
        }
        if (b[offset+7] & 0x02) != 0x02 { // Clean Session位必须位1
            log.Println("clean session must 1")
            client.Write([]byte{0x20, 0x02, 0x00, 0x01})
            return
        }
        if (b[offset+7] & 0x01) == 0x01 { // 保留位错误
            log.Println("Reserved Bit error")
            client.Write([]byte{0x20, 0x02, 0x00, 0x01})
            return
        }
        offset += 10 // 舍弃keepalive数据的读取
        clientidlen := 256 * uint32(b[offset]) + uint32(b[offset+1])
        clientid := string(b[offset+2 : offset+2+clientidlen])
        offset += 2+clientidlen
        clientlen := len(mqttclients)
        for i := 0 ; i < clientlen ; i++ {
            if mqttclients[i].clientid == clientid {
                log.Println("clientid has exist")
                client.Write([]byte{0x20, 0x02, 0x00, 0x02})
                return
            }
        }
        willtopiclen := 256 * uint32(b[offset]) + uint32(b[offset+1])
        willtopic := string(b[offset+2 : offset+2+willtopiclen])
        offset += 2+willtopiclen
        willmessagelen := 256 * uint32(b[offset]) + uint32(b[offset+1])
        willmessage := string(b[offset+2 : offset+2+willtopiclen])
        offset += 2+willmessagelen
        userlen := 256 * uint32(b[offset]) + uint32(b[offset+1])
        user := string(b[offset+2 : offset+2+userlen])
        offset += 2+userlen
        passlen := 256 * uint32(b[offset]) + uint32(b[offset+1])
        pass := string(b[offset+2 : offset+2+passlen])
        offset += 2+passlen
        if user != username || pass != password {
            log.Println("username or password error")
            client.Write([]byte{0x20, 0x02, 0x00, 0x04})
            return
        }
        mqttclient = MqttClient{client, clientid, make([]string, 0), willtopic, willmessage}
        mqttclients = append(mqttclients, mqttclient)
        client.Write([]byte{0x20, 0x02, 0x00, 0x00})
    } else {
        log.Println("client need connect first")
        return
    }

    for {
        _, err := client.Read(b[:])
        if err != nil {
            log.Println("client connect lose!")
            return
        }

        datalen, offset := GetMqttDataLength(b[:])
        if datalen == 0 {
            log.Println(err)
            continue
        }
        switch b[0] & 0xf0 {
            case 0x30: // publish
                if (b[0] & 0x0f) != 0x00 { // 本程序不处理dup，qos与retain不为0的报文
                    log.Println("publish dup,qos and retain is not 0.")
                    continue
                }
                topiclen := 256 * uint32(b[offset]) + uint32(b[offset+1])
                topic := string(b[offset+2 : offset+2+topiclen])
                clientlen := len(mqttclients)
                for i := 0 ; i < clientlen ; i++ {
                    topiclen = uint32(len(mqttclients[i].topic))
                    for j := uint32(0) ; j < topiclen ; j++ {
                        if mqttclients[i].topic[j] == topic {
                            mqttclients[i].client.Write(b[:datalen])
                        }
                    }
                }
            case 0x40:
            case 0x50:
            case 0x60:
            case 0x70:
            case 0x80: // subscribe
                if (b[0] & 0x0f) != 0x00 {
                    log.Println("mqtt connect package flag error")
                    client.Write([]byte{0x20, 0x02, 0x00, 0x01})
                    return
                }
                subackdata := []byte{0x90, 0, b[offset], b[offset+1]} // 报文标识符
                offset += 2
                for offset < datalen {
                    topiclen := 256 * uint32(b[offset]) + uint32(b[offset+1])
                    topic := string(b[offset+2 : offset+2+topiclen])
                    offset += 2+topiclen
                    mqttclient.topic = append(mqttclient.topic, topic)
                    if b[offset] != 0 {
                        log.Println("only support Requested QoS is 0.")
                        client.Write([]byte{0x20, 0x02, 0x00, 0x01})
                        return
                    }
                    offset += 1
                    subackdata[1]++
                    subackdata = append(subackdata, 0x00)
                }
                client.Write(subackdata)
            case 0xa0:
            case 0xc0:
            case 0xe0:
            default:
                log.Println("unknown mqtt package type", b[0] & 0xf0)
                return
        }
    }
}

func StartTcpServer (tcpport int, username string, password string) {
    p := strconv.Itoa(tcpport)
    tcpListen, err := net.Listen("tcp", ":" + p)
    if err != nil {
        log.Panic(err)
    }
    defer tcpListen.Close()
    for {
        tcpclient, err := tcpListen.Accept()
        if err != nil {
            log.Println(err)
            continue
        }
        go HandleTcpClientRequest(tcpclient, username, password)
    }
}

func StartServer (tcpport int, tlsport int, wsport int, wssport int, username string, password string) {
    if tcpport != 0 {
        go StartTcpServer(tcpport, username, password)
    }
}
