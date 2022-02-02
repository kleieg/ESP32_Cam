/*  Chnagelog
8.2.21  UTC time eingeführt

10.2.21 Umgestellt von Messung Watt auf Raw-values.
Um nur Trockner ein/aus zu erkennen ist das weniger Fehleranfällig.
Außerdem den Meßbereich geändert:
ADC_ATTEN_DB_6  = 0 - 2V in ADC_ATTEN_DB_2_5 = 0-1.34V
Dazu am GPIO32 den Spannungteiler von 1:1 = 1.6 V  auf 1:3 = 0.825 V angepasst.
Der SCT013 liefert 1V bei 30A d.h. beo 2KW = 8,6 A nur ca. 0,3V

3.4.21 Online / Offline mit Last Wiil in MQTT eingeführt




*/

#include <Arduino.h>

#include <driver/adc.h>


#include <esp_system.h>
#include <string>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "WLAN_Credentials.h"
#include "config.h"
#include "wifi_mqtt.h"



// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
long My_time = 0;
long Start_time;
long Up_time;
long U_days;
long U_hours;
long U_min;
long U_sec;

                      

// Timers auxiliar variables
long now = millis();
char strtime[8];
int LEDblink = 0;
bool led = 1;



// define SCT stuff
int SCT_samples = 1900;         // ca. 100 msec = 5 50Hz Perioden
int SCT_scanInterval = 10000;   //In milliseconds
long SCT_lastScan = 0;
int SCT_raw = 1;
long  SCT_time =0;
long  SCT_duration;     

// Create AsyncWebServer object on port 80
AsyncWebServer Asynserver(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// end of definitions -----------------------------------------------------

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin()) {
    // Serial.println("An error has occurred while mounting LittleFS");
  }
  // Serial.println("LittleFS mounted successfully");
}

String getOutputStates(){
  JSONVar myArray;

  U_days = Up_time / 86400;
  U_hours = (Up_time % 86400) / 3600;
  U_min = (Up_time % 3600) / 60;
  U_sec = (Up_time % 60);

  myArray["cards"][0]["c_text"] = Hostname;
  myArray["cards"][1]["c_text"] = WiFi.dnsIP().toString() + "   /   " + String(VERSION);
  myArray["cards"][2]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][3]["c_text"] = String(MQTT_INTERVAL) + "ms";
  myArray["cards"][4]["c_text"] = String(U_days) + " days " + String(U_hours) + ":" + String(U_min) + ":" + String(U_sec);
  myArray["cards"][5]["c_text"] = "WiFi = " + String(WiFi_reconnect) + "   MQTT = " + String(Mqtt_reconnect);
  myArray["cards"][6]["c_text"] = String(SCT_raw);
  myArray["cards"][7]["c_text"] = " to reboot click ok";
  
  myArray["cards"][8]["c_text"] = String(SCT_scanInterval) + "ms";
  myArray["cards"][9]["c_text"] = String(SCT_samples);
  
  String jsonString = JSON.stringify(myArray);
  return jsonString;
}

void notifyClients(String state) {
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    char help[30];
    int card;
    int value;
    
    for (int i = 0; i <= len; i++){
      help[i] = data[i];
    }

    log_i("Data received: ");
    log_i("%s\n",help);

    JSONVar myObject = JSON.parse(help);

    card =  myObject["card"];
    value =  myObject["value"];
    log_i("%d", card);
    log_i("%d",value);

    switch (card) {
      case 0:   // fresh connection
        notifyClients(getOutputStates());
        break;
      case 7:
        log_i("Reset..");
        ESP.restart();
        break;
      case 8:
        SCT_scanInterval = value;
        notifyClients(getOutputStates());
        break;
      case 9:
        SCT_samples = value;
        notifyClients(getOutputStates());
        break;
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      log_i("WebSocket client connected");
      break;
    case WS_EVT_DISCONNECT:
      log_i("WebSocket client disconnected");
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

//  function for SCT scan
void SCT_scan () {
  
  int val;
  double sum = 0;  
  double raw;

  //  start scan
  log_i("start SCT scan!");
  
  SCT_duration  = millis();

  for (int i = 0; i < SCT_samples; i++) {

      val = adc1_get_raw(ADC1_CHANNEL_4) - 2125;    // 2125 ist Mittelwert bei 0 sct_raw vom SCT13
      sum = sum + (val * val);
  }

  SCT_duration = millis() - SCT_duration ;
  log_i("scan duration: %d",SCT_duration );

  raw = sum / SCT_samples;
  raw = sqrt(raw);
  log_i("raw value= %.5f",raw);

  SCT_raw = raw;

  notifyClients(getOutputStates());
}


void MQTTsend () {
  JSONVar mqtt_data; 

  String mqtt_tag = Hostname + "/STATUS";
  log_i("%s\n", mqtt_tag.c_str());
  
  mqtt_data["Time"] = My_time;
  mqtt_data["RSSI"] = WiFi.RSSI();
  mqtt_data["Raw_value"] =SCT_raw;

  String mqtt_string = JSON.stringify(mqtt_data);

  log_i("%s\n", mqtt_string.c_str());

  Mqttclient.publish(mqtt_tag.c_str(), mqtt_string.c_str());

  notifyClients(getOutputStates());
}

// setup 
void setup() {
  
  SERIALINIT                                 
  
  log_i("setup device\n");

  pinMode(GPIO_LED, OUTPUT);
  digitalWrite(GPIO_LED,led);

  log_i("setup WiFi\n");
  initWiFi();

  log_i("setup MQTT\n");
  Mqttclient.setServer(MQTT_BROKER, 1883);

// ADC1_CHANNEL_4  = GPIO32
// ADC_ATTEN_DB_6  = Meßbereich up to approx. 1350 mV.
/*
By default, the allowable input range is 0-1V but with different attenuations we can scale the input sct_rawage into this range. 
The available scales beyond the 0-1V include 0-1.34V, 0-2V and 0-3.6V.
   ADC_ATTEN_DB_0    
    ADC_ATTEN_DB_2_5 
    ADC_ATTEN_DB_6    
    ADC_ATTEN_DB_11  
    ADC_ATTEN_MAX,

*/

  log_i("setup ADC1\n");
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_4,ADC_ATTEN_DB_2_5);

  initSPIFFS();
  
  // init Websocket
  ws.onEvent(onEvent);
  Asynserver.addHandler(&ws);

  // Route for root / web page
  Asynserver.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html",false);
  });

  Asynserver.serveStatic("/", SPIFFS, "/");

  timeClient.begin();
  timeClient.setTimeOffset(0);
  // update UPCtime for Starttime
  timeClient.update();
  Start_time = timeClient.getEpochTime();

  // Start ElegantOTA
  AsyncElegantOTA.begin(&Asynserver);
  
  // Start server
  Asynserver.begin();

}

void loop() {

  ws.cleanupClients();

  // update UPCtime
  timeClient.update();
  My_time = timeClient.getEpochTime();
  Up_time = My_time - Start_time;

  now = millis();

  // LED blinken
  if (now - LEDblink > 2000) {
    LEDblink = now;
    if(led == 0) {
      digitalWrite(GPIO_LED, 1);
      led = 1;
    }else{
      digitalWrite(GPIO_LED, 0);
      led = 0;
    }
  }

  // perform SCT scan
  if (now - SCT_lastScan > SCT_scanInterval) {
    SCT_lastScan = now;
    SCT_scan();
  } 

  // check WiFi
  if (WiFi.status() != WL_CONNECTED  ) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;              // prevents mqtt reconnect running also
      // Attempt to reconnect
      reconnect_wifi();
    }
  }

  // check if MQTT broker is still connected
  if (!Mqttclient.connected()) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      reconnect_mqtt();
    }
  } else {
    // Client connected

    Mqttclient.loop();

    // send data to MQTT broker
    if (now - Mqtt_lastSend > MQTT_INTERVAL) {
    Mqtt_lastSend = now;
    MQTTsend();
    } 
  }   
}