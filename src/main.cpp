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

#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_system.h>
#include <string>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>

#include "WLAN_Credentials.h"


#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#define SERIALINIT Serial.begin(115200);
#else
#define SERIALINIT
#endif

//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<   Anpassungen !!!!
// set hostname used for MQTT tag and WiFi 
#define HOSTNAME "SCT013"


// variables to connects to  MQTT broker
const char* mqtt_server = "192.168.178.15";
const char* willTopic = "tele/SCT013/LWT";       // muss mit HOSTNAME passen !!!  tele/HOSTNAME/LWT    !!!

//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<   Anpassungen Ende !!!!

// for MQTT
byte willQoS = 0;
const char* willMessage = "Offline";
boolean willRetain = true;
std::string mqtt_tag;
int Mqtt_sendInterval = 20000;   // in milliseconds
long Mqtt_lastScan = 0;
long lastReconnectAttempt = 0;

// Define NTP Client to get time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;
int UTC_syncIntervall = 3600000; // in milliseconds = 1 hour
long UTC_lastSync;

// Initializes the espClient. 
WiFiClient myClient;
PubSubClient client(myClient);
// name used as Mqtt tag
std::string gateway = HOSTNAME ;                           

// Timers auxiliar variables
long now = millis();
char strtime[8];
int LEDblink = 0;
bool led = 1;
int esp32LED = 1;
time_t UTC_time;


// define SCT stuff
int SCT_samples = 1900;         // ca. 100 msec = 5 50Hz Perioden
int SCT_scanInterval = 10000;   //In milliseconds
long SCT_lastScan = 0;
int SCT_raw = 1;
long  SCT_time =0;
long  SCT_duration;     

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

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
  
  myArray["cards"][0]["c_text"] = HOSTNAME;
  myArray["cards"][1]["c_text"] = willTopic;
  myArray["cards"][2]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][3]["c_text"] = String(Mqtt_sendInterval) + "ms";
  myArray["cards"][4]["c_text"] = String(SCT_raw);
  myArray["cards"][5]["c_text"] = String(SCT_duration) + "ms";
  myArray["cards"][6]["c_text"] = String(SCT_time);
  myArray["cards"][7]["c_text"] = " ";
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

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// init WiFi
void setup_wifi() {

  delay(10);
  digitalWrite(esp32LED, 0); 
  delay(500);
  digitalWrite(esp32LED, 1); 
  delay(500);
  digitalWrite(esp32LED, 0);
  delay(500);
  digitalWrite(esp32LED, 1);
  log_i("Connecting to ");
  log_i("%s",ssid);
  log_i("%s",password);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
     if(led == 0){
       digitalWrite(esp32LED, 1);  // LED aus
       led = 1;
     }else{
       digitalWrite(esp32LED, 0);  // LED ein
       led = 0;
     }
    log_i(".");
  }

  digitalWrite(esp32LED, 1);  // LED aus
  led = 1;
  log_i("WiFi connected - IP address: ");
  log_i("%s",WiFi.localIP().toString().c_str());

  // get time from NTP-server
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // update ESP-systemtime to UTC
  delay(50);                                                 // udate takes some time
  time(&UTC_time);
  log_i("%s","Unix-timestamp =");
  itoa(UTC_time,strtime,10);
  log_i("%s",strtime);
}


// reconnect to WiFi 
void reconnect_wifi() {
  log_i("%s\n","WiFi try reconnect"); 
  WiFi.begin();
  delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    // Once connected, publish an announcement...
    log_i("%s\n","WiFi reconnected"); 
  }
}

// This functions reconnects your ESP32 to your MQTT broker

void reconnect_mqtt() {
  if (client.connect(gateway.c_str(), willTopic, willQoS, willRetain, willMessage)) {
    // Once connected, publish an announcement...
    log_i("%s\n","Mqtt connected"); 
    mqtt_tag = gateway + "/connect";
    client.publish(mqtt_tag.c_str(),"connected");
    log_i("%s",mqtt_tag.c_str());
    log_i("%s\n","connected");
    mqtt_tag = "tele/" + gateway  + "/LWT";
    client.publish(mqtt_tag.c_str(),"Online",willRetain);
    log_i("%s",mqtt_tag.c_str());
    log_i("%s\n","Online");
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

  time(&UTC_time);
  itoa(UTC_time,strtime,10);

  SCT_time = UTC_time;   

  notifyClients(getOutputStates());
}


// send data using Mqtt 
/*  old version.  Now using Arduino_JSON instead
void MQTTsend () {

  char strdata[20];
  char strrssi[8];
  mqtt_tag = "tele/" + gateway + "/SENSOR";
  log_i("%s",mqtt_tag.c_str());

  sprintf(strdata,"%4.0f",SCT_raw);
  std::string str1(strdata);
  mqtt_data = "{\"Raw_value\":" + str1;
  itoa(SCT_time,strtime,10);
  mqtt_data = mqtt_data + ",\"Time\":" + strtime;
  log_i("%s",mqtt_data.c_str());

  itoa(WiFi.RSSI(),strrssi,10);
  mqtt_data = mqtt_data + ",\"RSSI\":" + strrssi + "}";
  log_i("%s",mqtt_data.c_str());

  client.publish(mqtt_tag.c_str(), mqtt_data.c_str());
}
*/

void MQTTsend () {
  JSONVar mqtt_data; 
  
  mqtt_tag = "tele/" + gateway + "/SENSOR";
  log_i("%s",mqtt_tag.c_str());

  mqtt_data["Raw_value"] =SCT_raw;
  mqtt_data["Time"] = SCT_time;
  mqtt_data["RSSI"] = WiFi.RSSI();
  String mqtt_string = JSON.stringify(mqtt_data);

  log_i("%s",mqtt_string.c_str()); 

  client.publish(mqtt_tag.c_str(), mqtt_string.c_str());
}

// setup 
void setup() {
  
  SERIALINIT                                 
  
  log_i("setup device\n");

  pinMode(esp32LED, OUTPUT);
  digitalWrite(esp32LED,led);

  log_i("setup WiFi\n");
  setup_wifi();

  log_i("setup MQTT\n");
  client.setServer(mqtt_server, 1883);

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
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html",false);
  });

  server.serveStatic("/", SPIFFS, "/");

  // Start ElegantOTA
  AsyncElegantOTA.begin(&server);
  
  // Start server
  server.begin();

}

void loop() {

  AsyncElegantOTA.loop();
  ws.cleanupClients();

  now = millis();

  // LED blinken
  if (now - LEDblink > 2000) {
    LEDblink = now;
    if(led == 0) {
      digitalWrite(esp32LED, 1);
      led = 1;
    }else{
      digitalWrite(esp32LED, 0);
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

  // perform UTC sync
  if (now - UTC_lastSync > UTC_syncIntervall) {
    UTC_lastSync = now;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // update ESP-systemtime to UTC
    delay(50);                                                 // udate takes some time
    time(&UTC_time);
    log_i("%s","Re-sync ESP-time!! Unix-timestamp =");
    itoa(UTC_time,strtime,10);
    log_i("%s",strtime);
  }   

  // check if MQTT broker is still connected
  if (!client.connected()) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      reconnect_mqtt();
    }
  } else {
    // Client connected

    client.loop();

    // send data to MQTT broker
    if (now - Mqtt_lastScan > Mqtt_sendInterval) {
    Mqtt_lastScan = now;
    MQTTsend();
    } 
  }   
}