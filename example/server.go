package main

import (
    "fmt"
    "time"
    "go-mqttserver"
)

func main () {
    var in string
	ms := go-mqttserver.StartServer(1883, 9001, "worldflying", "worldflying", []string{"loseconnect", "testtopic"}, func (topic string, d []byte) {
        fmt.Println(topic, string(d))
    })
    for {
        fmt.Println("SendData")
        go-mqttserver.PublishData(ms, "testtopic", []byte("hello world"))
        time.Sleep(5*time.Second)
    }
    fmt.Scanln(&in)
    go-mqttserver.CloseServer(ms)
}
