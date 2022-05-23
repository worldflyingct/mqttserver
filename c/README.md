# C语言版本的mqtt服务器
依赖ssl，安装方法为apt-get install libssl-dev  
# mqtt用户名密码说明
用户名在config.json中配置，用户名为mqttuser，  
根据config.json中的mqttkeymode不同，密码方式也不同:  
当为0时，mqtt密码就是config.json中的mqttkey  
当为1时，密码需要通过计算得到，计算方法如下:  
为首先获得时间戳，然后通过&与mqttkey的值进行拼接成临时字符串  
然后通过sha256计算，最后再替换临时字符串中mqttkey成为新的字符串作为密码  
如当前时间戳为1619702833，mqttkey为QLUL276kqXn55lBK  
那么临时字符串就为1619702833&QLUL276kqXn55lBK  
通过sha256计算得到3aa7bc950890ed5bf1b3251d1c04c92d1289f3a48ab9cd2b39e74e4fd2cd7709  
那么密码就是1619702833&3aa7bc950890ed5bf1b3251d1c04c92d1289f3a48ab9cd2b39e74e4fd2cd7709  
密码有效期为10分钟，如果本地的时间与服务器的时间误差太大会导致无法登录  
