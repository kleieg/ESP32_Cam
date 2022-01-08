var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    var message = "{\"card\":0,\"value\":0}";
    console.log(message);
    websocket.send(message);
}
  
function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
} 

function onMessage(event) {
    var myObj = JSON.parse(event.data);
            console.log(myObj);
            for (i in myObj.cards){
                var c_text = myObj.cards[i].c_text;
                console.log(c_text);
                document.getElementById(i).innerHTML = c_text;
            }
    console.log(event.data);
}


function myFunction(id) {
    var inpObj = document.getElementById("i"+id);
    if (!inpObj.checkValidity()) {
      document.getElementById("m"+id).innerHTML = inpObj.validationMessage;
    } else {
      document.getElementById("m"+id).innerHTML = "Input OK";
      console.log('Send data :');
      
      var message = "{\"card\":" + id + ",\"value\":" + inpObj.value + "}";
      console.log(message);
      websocket.send(message);
    } 
  } 