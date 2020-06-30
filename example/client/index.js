var client = null;
var html = '';

function appendmsgbox (string) { // 添加信息到msgbox底部
    var msgbox = document.getElementById('msgbox');
    html = html + string;
    msgbox.innerHTML = html;
    if (msgbox.scrollHeight > 480) {
        msgbox.scrollTo(0, msgbox.scrollHeight-480); // 500是msgbox的高度，msgbox被设置为了500px
    }
}

function connect_server () {
    if (client != null && client.isConnected()) {
        alert('请先断开之前的连接');
        return
    }
    var server = document.getElementById('server').value;
    var port = document.getElementById('port').value;
    var username = document.getElementById('username').value;
    var password = document.getElementById('password').value;
    var topic = document.getElementById('topic').value;
    var useSSL = document.getElementById('useSSL').checked;
//  var reconnect = document.getElementById('reconnect').checked;
// 生成随机数的clientid
    var date = new Date();
    var clientid = date.getTime(); // 返回当前毫秒时间戳
    var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    for (var i = 0 ; i < 32 ; i++) {
        clientid += chars.charAt(Math.floor(Math.random() * chars.length));
    }
// End of 生成随机数的clientid
    client = new Paho.MQTT.Client(server, Number(port), clientid);
    client.connect({
        userName: username,
        password: password,
        onSuccess: function (e) {
            console.log (e);
            appendmsgbox ('连接成功<br />');
            client.subscribe (topic);
        },
        onFailure: function (e) {
            console.log (e);
            appendmsgbox ('连接失败;errorCode:' + e.errorCode + ';errorMessage:' + e.errorMessage + '<br />');
            client = null;
        },
        reconnect: false, // 这个参数可以不设置，默认为false
        useSSL:useSSL // 这个参数可以不设置，默认为false
    });
    client.onConnectionLost = function (e) {
        console.log (e);
        appendmsgbox ('断开连接;errorCode:' + e.errorCode + ';errorMessage:' + e.errorMessage + '<br />');
        client = null;
    }
    client.onMessageArrived = function (msg) {
        console.log (msg);
        appendmsgbox ('收到消息:' + msg.payloadString + '<br />');
    }
}

function disconnect_server () {
    client.disconnect ();
}

function mqtt_send () {
    if (client == null || !client.isConnected()) {
        alert('请先连接mqtt服务器');
        return
    }
    var sendtopic = document.getElementById('sendtopic').value;
    var sendmsg = document.getElementById('sendmsg').value;
    var message = new Paho.MQTT.Message(sendmsg);
    message.destinationName = sendtopic;
    client.send(message);
}