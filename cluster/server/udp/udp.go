package udp

import (
    "fmt"
    "net"
    _ "github.com/go-sql-driver/mysql"
    "database/sql"
    "github.com/gomodule/redigo/redis"
    "time"
)

var (
    pool *redis.Pool
    db *sql.DB
)

func HandleClient () int {
    var err error
    db, err = sql.Open("mysql", "uit:Meh2B3KFct0POZOL@tcp(192.168.86.253:3306)/uit?charset=utf8")
    if err != nil {
        fmt.Println("Can't resolve address: ", err)
        return -1
    }
    db.SetMaxOpenConns(1000)
    db.SetMaxIdleConns(200)
    db.Ping()
    pool = &redis.Pool{
        MaxIdle: 2,
        IdleTimeout: 40 * time.Second,
        Dial: func () (redis.Conn, error) {
            c, err := redis.Dial("tcp", "192.168.86.247:6379")
            if err != nil {
                return nil, err
            }
            if _, err := c.Do("AUTH", "5u68dDBloozy1eKndihybdFyXOOgA26U"); err != nil {
                c.Close()
                return nil, err
            }
            if _, err := c.Do("SELECT", 2); err != nil {
                c.Close()
                return nil, err
            }
            return c, nil
        },
        TestOnBorrow: func(c redis.Conn, t time.Time) error {
            _, err := c.Do("PING")
            return err
        },
    }
    addr, err := net.ResolveUDPAddr("udp", ":2839");
    if err != nil {
        fmt.Println("Can't resolve address: ", err)
        return -1
    }
    conn, err := net.ListenUDP("udp", addr)
    if err != nil {
        fmt.Println("Error listening:", err)
        return -2
    }
    for {
        data := make([]byte, 1500)
        _, rAddr, err := conn.ReadFromUDP(data)
        if err != nil {
            fmt.Println("failed to read UDP msg because of ", err.Error())
            return -3
        }
        fmt.Println(rAddr)
        b := []byte("hello world")
        conn.WriteToUDP(b, rAddr)
    }
}
