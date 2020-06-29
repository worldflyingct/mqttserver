package main

import (
    "fmt"
    "time"
    "mqttserver"
)

func main () {
    var in string
	ms := mqttserver.StartServer(1883, 9001, "worldflying", "worldflying", []string{"loseconnect", "testtopic"}, func (topic string, d []byte) {
        fmt.Println(topic, string(d))
    })
    for {
        fmt.Println("SendData")
        mqttserver.PublishData(ms, "testtopic", []byte("hello world"))
        time.Sleep(5*time.Second)
    }
    fmt.Scanln(&in)
    mqttserver.CloseServer(ms)
}
