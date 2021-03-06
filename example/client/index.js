let client = null;
let html = '';

function appendmsgbox (string) { // 添加信息到msgbox底部
    let msgbox = document.getElementById('msgbox');
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
    let server = document.getElementById('server').value;
    let port = document.getElementById('port').value;
    let username = document.getElementById('username').value;
    let password = document.getElementById('password').value;
    let topic = document.getElementById('topic').value;
    let useSSL = document.getElementById('useSSL').checked;
//  let reconnect = document.getElementById('reconnect').checked;
// 生成随机数的clientid
    let date = new Date();
    let clientid = date.getTime(); // 返回当前毫秒时间戳
    let chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    for (let i = 0 ; i < 32 ; i++) {
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
            localStorage.setItem('d', JSON.stringify({
              server: server,
              port: port,
              username: username,
              password: password,
              topic: topic,
              useSSL: useSSL
            }));
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
    let sendtopic = document.getElementById('sendtopic').value;
    let sendmsg = document.getElementById('sendmsg').value;
    let message = new Paho.MQTT.Message(sendmsg);
    message.destinationName = sendtopic;
    client.send(message);
    localStorage.setItem('s', JSON.stringify({
      sendtopic: sendtopic,
      sendmsg: sendmsg
    }));
}

function clear_msg () {
    let msgbox = document.getElementById('msgbox');
    html = '';
    msgbox.innerHTML = html;
}

window.addEventListener('load', function (e) {
  let d = window.localStorage.getItem('d')
  if (d) {
    let a = JSON.parse(d)
    document.getElementById('server').value = a.server;
    document.getElementById('port').value = a.port;
    document.getElementById('username').value = a.username;
    document.getElementById('password').value = a.password;
    document.getElementById('topic').value = a.topic;
    document.getElementById('useSSL').checked = a.useSSL;
  }
  d = window.localStorage.getItem('s')
  if (d) {
    let a = JSON.parse(d)
    document.getElementById('sendtopic').value = a.sendtopic;
    document.getElementById('sendmsg').value = a.sendmsg;
  }
}, false)
