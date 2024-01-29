package main

import (
	"encoding/json"
	"io/fs"
	"log"
	"mime"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"strconv"
	"time"

	"github.com/valyala/fasthttp"
	"github.com/worldflyingct/mqttserver"
)

var mqttuser string
var mqttpass string
var mqttport float64
var wsport float64

func getconfig() bool {
	fp := filepath.Dir(os.Args[0]) + "/config.json"
	jsonStr, err := os.ReadFile(fp)
	if err != nil {
		jsonStr = []byte("{\r\n" +
			"  \"mqttuser\":\"worldflying\",\r\n" +
			"  \"mqttpass\":\"worldflying\",\r\n" +
			"  \"mqttport\":1883,\r\n" +
			"  \"wsport\":9001\r\n" +
			"}\r\n")
		err = os.WriteFile(fp, jsonStr, 0777)
		if err != nil {
			log.Println("mqttservertool", err.Error())
			return false
		}
	}
	var jsonObj map[string]interface{}
	err = json.Unmarshal(jsonStr, &jsonObj)
	if err != nil {
		log.Println("mqttservertool", err.Error())
		return false
	}
	var ok bool
	mqttuser, ok = jsonObj["mqttuser"].(string)
	if !ok {
		log.Println("mqttservertool", "mqttuser not found")
		return false
	}
	mqttpass, ok = jsonObj["mqttpass"].(string)
	if !ok {
		log.Println("mqttservertool", "mqttpass not found")
		return false
	}
	mqttport, ok = jsonObj["mqttport"].(float64)
	if !ok {
		log.Println("mqttservertool", "mqttport not found")
		return false
	}
	wsport, ok = jsonObj["wsport"].(float64)
	if !ok {
		log.Println("mqttservertool", "wsport not found")
		return false
	}
	return true
}

func main() {
	log.SetFlags(log.LstdFlags | log.Lshortfile)
	log.Println("mqttservertool", "build by "+runtime.Version())
	if !getconfig() {
		return
	}
	retrytime := 0
RETRY:
	ms := mqttserver.StartServer(uint16(mqttport), mqttuser, mqttpass, []string{}, func(topic string, d []byte) {})
	if ms == nil {
		retrytime++
		if retrytime < 100 {
			time.Sleep(5 * time.Second)
			goto RETRY
		}
	}
	/*
		// 基于net/http服务的websocket来创建mqtt
		http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
			w.Write([]byte("hello, this is net/http."))
		})
		http.HandleFunc("/mqtt", func(w http.ResponseWriter, r *http.Request) {
			mqttserver.ListenNetHttpWebSocket(ms, w, r, nil)
		})
		err := http.ListenAndServe(":"+strconv.Itoa(int(wsport)), nil)
	*/
	// 基于fasthttp服务的websocket来创建mqtt
	err := fasthttp.ListenAndServe(":"+strconv.Itoa(int(wsport)), func(ctx *fasthttp.RequestCtx) {
		reqpath := string(ctx.Path())
		if string(ctx.Path()) == "/mqtt" {
			mqttserver.ListenFastHttpWebSocket(ms, ctx, nil)
		} else {
			/*
			   下方是访问当前可执行文件同级的真实www目录下的文件的方法，由于已经不需要了，所以注释掉。
			   handler := fasthttp.FSHandler("www", 0)
			   handler(ctx)
			*/
			if reqpath[len(reqpath)-1:] == "/" { // 当请求路径没有添加文件时，默认认为请求的是index.html
				reqpath += "index.html"
			}
			// 获取文件内容
			var f fs.File
			f, err := os.Open(filepath.Dir(os.Args[0]) + "/www" + reqpath)
			if err != nil {
				ctx.WriteString("404 Not Found!!!")
				return
			}
			info, err := f.Stat()
			if err != nil {
				ctx.WriteString(err.Error())
				return
			}
			size := info.Size()
			// 1.利用path.Ext()获取文件的后缀
			// 2.利用mime.TypeByExtension()获取对应后缀的html mime类型
			// 3.利用ctx.SetContentType()设置http响应的Content-Type
			ctx.SetContentType(mime.TypeByExtension(path.Ext(reqpath)))
			ctx.SetBodyStream(f, int(size)) // 设置f为输出流，该函数会在发送完毕后调用f.Close()
		}
	})
	log.Println(err)
	mqttserver.CloseServer(ms)
	retrytime++
	if retrytime < 100 {
		time.Sleep(5 * time.Second)
		goto RETRY
	}
}
