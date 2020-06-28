package main

import (
    "fmt"
    "mqttserver"
)

func main () {
    var in string
	mqttserver.StartServer(1883, 0, 0, 0, "worldflying", "worldflying")
    fmt.Scanln(&in)
}
