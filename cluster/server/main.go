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
    pool *redis.Pool
    db *sql.DB
)

const connection  = 0x01
const clienton    = 0x02
const clientoff   = 0x03
const subscribe   = 0x04
const unsubscribe = 0x05
const publish     = 0x06
const ping        = 0x07
const pong        = 0x08

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
    listener, err := net.Listen("tcp", ":2839")
    if err != nil {
        fmt.Println("Error listening:", err)
        return
    }
    for {
        conn, err := listener.Accept()
        if err != nil {
            fmt.Println("failed to read TCP msg because of ", err.Error())
            return
        }
        go HandleConn(conn)
    }
}

func HandleConn (conn net.Conn) {
    defer conn.Close()
    data := make([]byte, 32*1024)
    n, err := conn.Read(data)
    if (err != nil) {
        return
    }
    if (data[0] != 0x01) {
        return
    }
    userpassindex := strings.IndexByte(string(data[:n]), '&')
    if (userpassindex == -1) {
        return
    }
    userpass := string(data[1:userpassindex])
    if (userpass != "SOUUsLxVBhHvZVxY") {
        return
    }
    data = data[userpassindex+1:n]
    timestampindex := strings.IndexByte(string(data), '&')
    if (timestampindex == -1) {
        return
    }
    timestampString := string(data[:timestampindex])
    timestamp, err := strconv.ParseInt(timestampString, 10, 64)
    now := time.Now().Unix()
    if timestamp + 1800 < now || now + 1800 < timestamp {
        return
    }
    signiture := string(data[timestampindex+1:])
    strings := userpass + "&" + timestampString + "&7PE2TDYSfsZOWv2i"
    s256b := sha256.Sum256([]byte(strings)) // 计算哈希值，返回一个长度为32的数组
    hashcode := hex.EncodeToString(s256b[:]) // 将数组转换成切片，转换成16进制，返回字符串
    if (signiture != hashcode) {
        return
    }
    conn.Write([]byte{0x20, 0x02, 0x00, 0x00})
    var pack []byte
    var packlen uint32
    var uselen uint32
    pack = nil
    packlen = 0
    uselen = 0
    for {
        n, err := conn.Read(data)
        num := uint32(n)
        if packlen > 0 {
            pack = append(pack, data...)
            if uselen + num < packlen {
                continue
            }
            data = pack
            pack = nil
            packlen = 0
            uselen = 0
        }
        datalen, offset := GetDataLength(data)
        if num < datalen {
            pack = data
            packlen = datalen
            uselen = num
            continue
        }
        switch data[0] {
            case clienton:
            case clientoff:
            case subscribe:
            case unsubscribe:
            case publish:
            case ping:
            case pong:
        }
    }
}

func GetDataLength (b []byte) (uint32, uint32)  {
    var datalen uint32
    var offset uint32
    if (b[1] & 0x80) != 0x00 {
        if (b[2] & 0x80) != 0x00 {
            if (b[3] & 0x80) != 0x00 {
                offset = 5
                datalen = 128 * 128 * 128 * uint32(b[4]) + 128 * 128 * uint32(b[3] & 0x7f) + 128 * uint32(b[2] & 0x7f) + uint32(b[1] & 0x7f) + 5
            } else {
                offset = 4
                datalen = 128 * 128 * uint32(b[3]) + 128 * uint32(b[2] & 0x7f) + uint32(b[1] & 0x7f) + 4
            }
        } else {
            offset = 3
            datalen = 128 * uint32(b[2]) + uint32(b[1] & 0x7f) + 3
        }
    } else {
        offset = 2
        datalen = uint32(b[1]) + 2
    }
    return datalen, offset
}
