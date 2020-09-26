package main

import (
    "fmt"
    "net/http"
    "time"
    "github.com/worldflyingct/mqttserver"
    "github.com/valyala/fasthttp"
)

func main () {
    fmt.Println("mqttserver example.");
    ms := mqttserver.StartServer(1883, "worldflying", "worldflying", []string{"loseconnect", "testtopic"}, func (topic string, d []byte) {
        fmt.Println(topic, string(d))
    })
    // 基于net/http服务的websocket来创建mqtt
    http.HandleFunc("/", func (w http.ResponseWriter, r *http.Request) {
        fmt.Fprint(w, "hello, this is net/http.")
    })
    http.HandleFunc("/mqtt", func (w http.ResponseWriter, r *http.Request) {
        mqttserver.ListenNetHttpWebSocket(ms, w, r, nil)
    })
    go http.ListenAndServe(":8080",nil)
    // 基于fasthttp服务的websocket来创建mqtt
    go fasthttp.ListenAndServe(":8081", func (ctx *fasthttp.RequestCtx) {
        if string(ctx.Path()) == "/mqtt" {
            mqttserver.ListenFastHttpWebSocket(ms, ctx, nil)
        } else {
            fmt.Fprint(ctx, "hello, this is fasthttp.")
        }
    })
    // 初始化结束，演示发数据
    for {
        fmt.Println("SendData")
        mqttserver.PublishData(ms, "testtopic", []byte("hello world"))
        time.Sleep(5*time.Second)
    }
    mqttserver.CloseServer(ms)
}
