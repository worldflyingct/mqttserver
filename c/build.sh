#!/bin/bash

for gcc in aarch64_be-linux-musl armeb-linux-musleabi armel-linux-musleabi arm-linux-musleabi armv7l-linux-musleabihf i686-linux-musl mipsel-linux-musl x86_64-linux-musl aarch64-linux-musl armeb-linux-musleabihf armel-linux-musleabihf arm-linux-musleabihf
do
  echo "Building $gcc"
  export CC=$gcc-gcc
  make clean
  make
  mv uit mqttserver
  tar -czvf mqttserver.$gcc.tar.gz mqttserver
done
