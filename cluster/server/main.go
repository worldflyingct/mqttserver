package main

import (
    "fmt"
    "net"
    "database/sql"
    _ "github.com/go-sql-driver/mysql"
    // "github.com/gomodule/redigo/redis"
    "time"
    "crypto/sha256"
    "encoding/hex"
    "strings"
    "strconv"
)

var (
    // pool *redis.Pool
    db *sql.DB
)

const connection        = 0x01
const clientonline      = 0x02
const clientoffline     = 0x03
const subscribe         = 0x04
const unsubscribe       = 0x05
const publish           = 0x06
const ping              = 0x07
const pong              = 0x08
const disconnectclient  = 0x10

func main () {
    var err error
    db, err = sql.Open("mysql", "root:root@tcp(127.0.0.1:3306)/uit?charset=utf8")
    if err != nil {
        fmt.Println("Can't resolve address: ", err)
        return
    }
    db.SetMaxOpenConns(1000)
    db.SetMaxIdleConns(200)
    db.Ping()
/*
    pool = &redis.Pool{
        MaxIdle: 2,
        IdleTimeout: 40 * time.Second,
        Dial: func () (redis.Conn, error) {
            c, e := redis.Dial("tcp", "192.168.86.247:6379")
            if e != nil {
                return nil, e
            }
            if _, e := c.Do("AUTH", "5u68dDBloozy1eKndihybdFyXOOgA26U"); e != nil {
                c.Close()
                return nil, e
            }
            if _, e := c.Do("SELECT", 2); err != nil {
                c.Close()
                return nil, e
            }
            return c, nil
        },
        TestOnBorrow: func(c redis.Conn, t time.Time) error {
            _, e := c.Do("PING")
            return e
        },
    }
*/
    tcpAddr, _ := net.ResolveTCPAddr("tcp", ":2839")
    listener, err := net.ListenTCP("tcp", tcpAddr)
    if err != nil {
        fmt.Println("Error listening:", err)
        return
    }
    for {
        conn, err := listener.AcceptTCP()
        if err != nil {
            fmt.Println("failed to read TCP msg because of ", err.Error())
            return
        }
        go HandleConn(conn)
    }
}

func HandleConn (conn *net.TCPConn) {
    defer LoseNode(conn)

    pack := []byte(nil)
    packlen := uint32(0)
    uselen := uint32(0)
    state := false

    for {
        data := make([]byte, 32*1024)
        n, err := conn.Read(data)
        if err != nil {
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
            datalen, offset := GetDataLength(data, num)
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
                if data[0] == clientonline {
                    clientaddrlen := uint32(data[offset])
                    offset += 1
                    clientaddr := uint64(0)
                    for i := uint32(0) ; i < clientaddrlen ; i++ {
                        clientaddr = (clientaddr<<8) | uint64(data[offset+i])
                    }
                    offset += clientaddrlen
                    clientidlen := (uint32(data[offset]) << 8) | uint32(data[offset+1])
                    offset += 2
                    clientid := string(data[offset:offset+clientidlen])
                    offset += clientidlen
                    willtopiclen := (uint32(data[offset]) << 8) | uint32(data[offset+1])
                    offset += 2
                    willtopic := string(data[offset:offset+willtopiclen])
                    offset += willtopiclen
                    willmsglen := (uint32(data[offset]) << 8) | uint32(data[offset+1])
                    offset += 2
                    willmsg := string(data[offset:offset+willmsglen])
                    CheckClientId(clientid)
                    stmt, err := db.Prepare("INSERT INTO `client`(`clientid`, `nodeaddr`, `clientaddr`, `willtopic`, `willmsg`)VALUES(?,?,?,?,?)")
                    if err != nil {
                        fmt.Println(err)
                    }
                    _, err = stmt.Exec(clientid, conn, clientaddr, willtopic, willmsg)
                    if err != nil {
                        fmt.Println(err)
                    }
                    err = stmt.Close()
                    if err != nil {
                        fmt.Println(err)
                    }
                } else if data[0] == clientoffline {
                    clientaddrlen := uint32(data[offset])
                    offset += 1
                    clientaddr := uint64(0)
                    for i := uint32(0) ; i < clientaddrlen ; i++ {
                        clientaddr = (clientaddr<<8) | uint64(data[offset+i])
                    }
                    offset += clientaddrlen
                    stmt, err := db.Prepare("DELETE FROM `topic` WHERE `nodeaddr` = ? AND `clientaddr` = ?")
                    if err != nil {
                        fmt.Println(err)
                    }
                    _, err = stmt.Exec(conn, clientaddr)
                    if err != nil {
                        fmt.Println(err)
                    }
                    err = stmt.Close()
                    if err != nil {
                        fmt.Println(err)
                    }
                    stmt, err = db.Prepare("SELECT `willtopic`,`willmsg` FROM `client` WHERE `nodeaddr` = ? AND `clientaddr` = ?")
                    if err != nil {
                        fmt.Println(err)
                    }
                    row := stmt.QueryRow(conn, clientaddr)
                    var willtopic string
                    var willmsg []byte
                    err = row.Scan(&willtopic, &willmsg)
                    if err != nil && err != sql.ErrNoRows {
                        fmt.Println(err)
                    }
                    err = stmt.Close()
                    if err != nil {
                        fmt.Println(err)
                    }
                    stmt, err = db.Prepare("DELETE FROM `client` WHERE `nodeaddr` = ? AND `clientaddr` = ?")
                    if err != nil {
                        fmt.Println(err)
                    }
                    _, err = stmt.Exec(conn, clientaddr)
                    if err != nil {
                        fmt.Println(err)
                    }
                    err = stmt.Close()
                    if err != nil {
                        fmt.Println(err)
                    }
                    PublishData(willtopic, willmsg)
                } else if data[0] == subscribe {
                    clientaddrlen := uint32(data[offset])
                    offset += 1
                    clientaddr := uint64(0)
                    for i := uint32(0) ; i < clientaddrlen ; i++ {
                        clientaddr = (clientaddr<<8) | uint64(data[offset+i])
                    }
                    offset += clientaddrlen
                    topiclen := (uint32(data[offset]) << 8) | uint32(data[offset+1])
                    offset += 2
                    topic := string(data[offset:offset+topiclen])
                    stmt, err := db.Prepare("SELECT COUNT(*) as count FROM `topic` WHERE `topic`= ? AND `nodeaddr` = ? AND `clientaddr` = ?")
                    if err != nil {
                        fmt.Println(err)
                    }
                    var count uint64
                    row := stmt.QueryRow(topic, conn, clientaddr)
                    err = row.Scan(&count)
                    if err != nil && err != sql.ErrNoRows {
                        fmt.Println(err)
                    }
                    err = stmt.Close()
                    if err != nil {
                        fmt.Println(err)
                    }
                    if count == 0 {
                        stmt, err = db.Prepare("INSERT INTO `topic`(`topic`, `nodeaddr`, `clientaddr`)VALUES(?,?,?)")
                        if err != nil {
                            fmt.Println(err)
                        }
                        _, err = stmt.Exec(topic, conn, clientaddr)
                        if err != nil {
                            fmt.Println(err)
                        }
                        err = stmt.Close()
                        if err != nil {
                            fmt.Println(err)
                        }
                    }
                } else if data[0] == unsubscribe {
                    clientaddrlen := uint32(data[offset])
                    offset += 1
                    clientaddr := uint64(0)
                    for i := uint32(0) ; i < clientaddrlen ; i++ {
                        clientaddr = (clientaddr<<8) | uint64(data[offset+i])
                    }
                    offset += clientaddrlen
                    topiclen := (uint32(data[offset]) << 8) | uint32(data[offset+1])
                    offset += 2
                    topic := string(data[offset:offset+topiclen])
                    stmt, err := db.Prepare("DELETE FROM `topic` WHERE `topic`= ? AND `nodeaddr` = ? AND `clientaddr` = ?")
                    if err != nil {
                        fmt.Println(err)
                    }
                    _, err = stmt.Exec(topic, conn, clientaddr)
                    if err != nil {
                        fmt.Println(err)
                    }
                    err = stmt.Close()
                    if err != nil {
                        fmt.Println(err)
                    }
                } else if data[0] == publish {
                    topiclen := (uint32(data[offset]) << 8) | uint32(data[offset+1])
                    offset += 2
                    topic := string(data[offset:offset+topiclen])
                    offset += topiclen
                    msg := data[offset:num]
                    PublishData(topic, msg)
                } else if data[0] == ping {
                    conn.Write([]byte{pong, 0x00})
                }
            } else {
                if data[0] != 0x01 {
                    return
                }
                offset += 1
                nm := strings.IndexByte(string(data[offset:]), '&')
                if nm == -1 {
                    return
                }
                userpassend := uint32(nm)
                userpass := string(data[offset:offset+userpassend])
                if userpass != "SOUUsLxVBhHvZVxY" {
                    return
                }
                offset += userpassend+1
                nm = strings.IndexByte(string(data[offset:]), '&')
                if nm == -1 {
                    return
                }
                timestampend := uint32(nm)
                timestampString := string(data[offset:offset+timestampend])
                timestamp, err := strconv.ParseInt(timestampString, 10, 64)
                if err != nil {
                    return
                }
                now := time.Now().Unix()
                if timestamp + 1800 < now || now + 1800 < timestamp {
                    return
                }
                offset += timestampend+1
                signiture := string(data[offset:])
                strings := userpass + "&" + timestampString + "&7PE2TDYSfsZOWv2i"
                s256b := sha256.Sum256([]byte(strings)) // 计算哈希值，返回一个长度为32的数组
                hashcode := hex.EncodeToString(s256b[:]) // 将数组转换成切片，转换成16进制，返回字符串
                if signiture != hashcode {
                    return
                }
                conn.Write([]byte{0x20, 0x02, 0x00, 0x00})
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

func GetDataLength (data []byte, num uint32) (uint32, uint32)  {
    var datalen uint32
    var offset uint32
    if num < 2 {
        return 0, 0
    }
    if data[1] & 0x80 != 0x00 {
        if num < 3 {
            return 0, 0
        }
        if data[2] & 0x80 != 0x00 {
            if num < 4 {
                return 0, 0
            }
            if data[3] & 0x80 != 0x00 {
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

func PublishData (topic string, msg []byte) {
    topiclen := uint32(len(topic))
    msglen := uint32(len(msg))
    if topiclen == 0 || msglen == 0 {
        return
    }
    stmt, err := db.Prepare("SELECT `nodeaddr`,`clientaddr` FROM `topic` WHERE `topic` = ?")
    rows, err := stmt.Query(topic)
    if err != nil {
        fmt.Println(err)
    }
    for rows.Next() {
        var nodeaddr *net.TCPConn
        var clientaddr uint64
        err = rows.Scan(&nodeaddr, &clientaddr)
        if err != nil {
            fmt.Println(err)
        } else {
            clientaddrlen := uint32(0)
            tmp := clientaddr
            for tmp > 0 {
                tmp = tmp >> 8
                clientaddrlen++
            }
            var data []byte
            var offset uint32
            num := 2 + clientaddrlen + 2 + topiclen + msglen
            if num < 127 {
                data = make([]byte, num+2)
                offset = 3
            } else if num < 16384 {
                data = make([]byte, num+3)
                offset = 4
            } else if num < 2097151 {
                data = make([]byte, num+4)
                offset = 5
            } else {
                data = make([]byte, num+5)
                offset = 6
            }
            n := 0
            for num > 0 {
                data[n] |= 0x80
                n++
                data[n] = byte(num & 0x7f)
                num >>= 7
            }
            data[0] = publish
            data[offset] = byte(clientaddrlen)
            for i := uint32(0) ; i < clientaddrlen ; i++ {
                data[clientaddrlen-i-1+offset] = byte(clientaddr)
                clientaddr = clientaddr >> 8
            }
            offset += clientaddrlen
            data[offset] = byte(topiclen>>8)
            data[offset+1] = byte(topiclen)
            offset += 2
            for i := uint32(0) ; i < topiclen ; i++ {
                data[offset+i] = topic[i]
            }
            offset += topiclen
            for i := uint32(0) ; i < msglen ; i++ {
                data[offset+i] = msg[i]
            }
            nodeaddr.Write(data)
        }
    }
    err = stmt.Close()
    if err != nil {
        fmt.Println(err)
    }
}

func CheckClientId (clientid string) {
    stmt, err := db.Prepare("SELECT `nodeaddr`,`clientaddr`,`willtopic`,`willmsg` FROM `client` WHERE `clientid` = ?")
    if err != nil {
        fmt.Println(err)
    }
    row := stmt.QueryRow(clientid)
    var nodeaddr *net.TCPConn
    var clientaddr uint64
    var willtopic string
    var willmsg []byte
    err = row.Scan(&nodeaddr, &clientaddr, &willtopic, &willmsg)
    if err != nil && err != sql.ErrNoRows {
        fmt.Println(err)
    }
    err = stmt.Close()
    if err != nil {
        fmt.Println(err)
    }
    if err == nil {
        clientaddrlen := uint64(0)
        tmp := clientaddr
        for tmp > 0 {
            tmp = tmp >> 8
            clientaddrlen++
        }
        data := make([]byte, 3+clientaddrlen)
        data[0] = disconnectclient
        data[1] = byte(clientaddrlen+1)
        data[2] = byte(clientaddrlen)
        for i := uint64(0) ; i < clientaddrlen ; i++ {
            data[clientaddrlen-i+2] = byte(clientaddr)
            clientaddr = clientaddr >> 8
        }
        nodeaddr.Write(data)
    }
}

func LoseNode (conn *net.TCPConn) {
    stmt, err := db.Prepare("DELETE FROM `topic` WHERE `nodeaddr` = ?")
    if err != nil {
        fmt.Println(err)
    }
    _, err = stmt.Exec(conn)
    if err != nil {
        fmt.Println(err)
    }
    err = stmt.Close()
    if err != nil {
        fmt.Println(err)
    }
    stmt, err = db.Prepare("DELETE FROM `client` WHERE `nodeaddr` = ?")
    if err != nil {
        fmt.Println(err)
    }
    _, err = stmt.Exec(conn)
    if err != nil {
        fmt.Println(err)
    }
    err = stmt.Close()
    if err != nil {
        fmt.Println(err)
    }
}
