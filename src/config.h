// set hostname used for MQTT tag and WiFi
#define HOSTNAME "ESP-SCT013"
#define MQTT_BROKER "mqttserver.vring"
#define VERSION "v 0.9.6"

#define MQTT_INTERVAL 120000
#define RECONNECT_INTERVAL 5000
#define LED_BLINK_INTERVAL 500
#define RELAY_RESET_INTERVAL 5000

#define GPIO_LED 1

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#define SERIALINIT Serial.begin(115200);
#else
#define SERIALINIT
#endif