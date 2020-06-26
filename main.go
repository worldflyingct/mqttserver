package main

import (
    "mqttserver"
)

func main () {
	mqttserver.StartServer(1883, 0, 0, 0, "worldflying", "worldflying")
}
