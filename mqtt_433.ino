#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <NewRemoteTransmitter.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "mqtt_433_credentials.h"

#define PIN433 15

#define ONE_WIRE_BUS 2
#define TEMP_REQUEST_DELAY 60000
#define TEMP_REQUESTED 1
#define TEMP_WAITING 2

#define MAX_DEVICECOUNT 16

#define HOMIE_DEVICE_PREFIX "homie/nodemcu"
#define SWITCH_COUNT 6
const char * const switchNames[SWITCH_COUNT] = {
  "Beleuchtung Terrarien",
  "Licht Flur EG",
  "Licht Wohnzimmer Stehlampe",
  "Licht Flur OG",
  "Weihnachtsbaum",
  "Licht Wohnzimmer Tiffany"
};

WiFiClient espClient;
PubSubClient client(espClient);

// Create a transmitter on address address_433, using digital pin PIN433 to transmit,
// with a period duration of 260ms (default), repeating the transmitted
// code 2^3=8 times.
NewRemoteTransmitter transmitter(address_433, PIN433, 260, 3);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress *deviceAddresses = new DeviceAddress[MAX_DEVICECOUNT];
unsigned int themoDeviceCount = 0;
unsigned int  maxResolution = 0;
unsigned long lastTempRequest = 0;
unsigned int delayInMillis = 0;
unsigned int tempReqState = TEMP_WAITING;

char topicTempString[100];

uint32_t uptime() {
    static uint32_t low32, high32;
    uint32_t new_low32 = millis();
    if (new_low32 < low32) high32++;
    low32 = new_low32;
    return (uint32_t) (high32 << 32 | low32)/1000;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.begin(115200);
  Serial.println("Booting");
  setup_onewire();
  setup_wifi();
  digitalWrite(LED_BUILTIN, HIGH);

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

void setup_onewire() {
  sensors.begin();
  Serial.print("Locating devices...Found ");
  themoDeviceCount = _min(sensors.getDeviceCount(), MAX_DEVICECOUNT);
  Serial.print(themoDeviceCount);
  Serial.println(" devices.");
  for (int i=0; i<themoDeviceCount; i++) {
    sensors.getAddress(deviceAddresses[i], i);
    Serial.printf("Device %d Address: ", i);
    printAddress(deviceAddresses[i]);
    Serial.print(". Resolution: ");
    unsigned int resolution = sensors.getResolution(deviceAddresses[i]);
    Serial.print(resolution);
    Serial.println(); 
    if (resolution > maxResolution) 
      maxResolution = resolution;
  }
  Serial.print("Max resolution: ");
  Serial.println(maxResolution);
  sensors.setWaitForConversion(false);
  delayInMillis = 750 / (1 << (12 - maxResolution));
  Serial.print("Calculated delay: ");
  Serial.print(delayInMillis);
  Serial.println(" ms.");
}

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.setPort(ota_port);
  ArduinoOTA.setHostname(ota_hostname);
  ArduinoOTA.setPassword(ota_pass);
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void lightShow() {
  analogWriteRange(1000);
  analogWrite(LED_BUILTIN, 990);
  for (int i=0;i<30;i++)
  {
    analogWrite(LED_BUILTIN, (i*100) % 1001);
    delay(50);
  }
  analogWrite(LED_BUILTIN, 0);
  digitalWrite(LED_BUILTIN, HIGH);  
}

void execSwitchCmd(byte unit, boolean switchOn, char* topic) {
    Serial.printf("send to unit %d: %d", unit, switchOn);
    Serial.println();
    digitalWrite(LED_BUILTIN, LOW);
    transmitter.sendUnit(unit, switchOn);
    if (switchOn)
      client.publish(topic, "true", true);
    else
      client.publish(topic, "false", true);
    digitalWrite(LED_BUILTIN, HIGH);
}

char* deviceTopic(const char* postfix) {
  // e.g. "homie/nodemcu/$implementation"
  snprintf(topicTempString, sizeof(topicTempString), "%s/%s", HOMIE_DEVICE_PREFIX, postfix);
  return topicTempString;
}

char* nNodeTopic(const char* node, unsigned int i, const char* postfix) {
  // e.g. "homie/nodemcu/switch-0/power/set"
  snprintf(topicTempString, sizeof(topicTempString), "%s/%s-%d/%s", HOMIE_DEVICE_PREFIX, node, i, postfix);
  return topicTempString;
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Message arrived with length %d [", length);
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  for (int i = 0; i < SWITCH_COUNT; i++) {
    if (strcmp(topic, nNodeTopic("switch", i, "power/set")) == 0) {
      execSwitchCmd(15-i, (char)payload[0] == 't', nNodeTopic("switch", i, "power"));
      break;
    }
  }
}

void publish_homie_device_info() {
  client.publish(deviceTopic("$state"), "init", true);
  client.publish(deviceTopic("$homie"), "3.0.0", true);
  client.publish(deviceTopic("$name"), "NodeMCU", true);
  client.publish(deviceTopic("$localip"), WiFi.localIP().toString().c_str(), true);
  client.publish(deviceTopic("$mac"), WiFi.macAddress().c_str(), true);
  client.publish(deviceTopic("$fw/name"), "ScholliFW", true);
  client.publish(deviceTopic("$fw/version"), "1.0", true);
  client.publish(deviceTopic("$implementation"), "esp8266", true);
  client.publish(deviceTopic("$stats"), "uptime", true);

  char tempStr1[100] = "";
  for (int i=0; i<themoDeviceCount; i++) {
    char tempStr2[20];
    snprintf(tempStr2, sizeof(tempStr2), "thermometer-%d,", i);
    strlcat(tempStr1, tempStr2, sizeof(tempStr1));
  }
  for (int i=0; i<SWITCH_COUNT; i++) {
    char tempStr2[20];
    snprintf(tempStr2, sizeof(tempStr2), "switch-%d", i);
    strlcat(tempStr1, tempStr2, sizeof(tempStr1));
    if (i<SWITCH_COUNT-1)
      strlcat(tempStr1, ",", sizeof(tempStr1));
  }
  client.publish(deviceTopic("$nodes"), tempStr1, true);

  for (int i=0; i<themoDeviceCount; i++)
    publish_homie_temperature(i);
  for (int i=0; i<SWITCH_COUNT; i++)
    publish_homie_switch(i, switchNames[i]);

  client.publish(deviceTopic("$state"), "ready", true);
}

void publish_homie_temperature(unsigned int thermometer_index) {
  char tempStr[20];
  snprintf(tempStr, sizeof(tempStr), "Terrarium %d", thermometer_index+1);
  client.publish(nNodeTopic("thermometer", thermometer_index, "$name"), tempStr, true);
  client.publish(nNodeTopic("thermometer", thermometer_index, "$properties"), "temperature", true);
  client.publish(nNodeTopic("thermometer", thermometer_index, "temperature/$name"), tempStr, true);
  client.publish(nNodeTopic("thermometer", thermometer_index, "temperature/$unit"), "°C", true);
  client.publish(nNodeTopic("thermometer", thermometer_index, "temperature/$datatype"), "float", true);
}

void publish_homie_switch(unsigned int switch_index, const char* name) {
  client.publish(nNodeTopic("switch", switch_index, "$name"), name, true);
  client.publish(nNodeTopic("switch", switch_index, "$properties"), "power", true);
  client.publish(nNodeTopic("switch", switch_index, "power/$name"), name, true);
  client.publish(nNodeTopic("switch", switch_index, "power/$settable"), "true", true);
  client.publish(nNodeTopic("switch", switch_index, "power/$datatype"), "boolean", true);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Connection Failed! Rebooting...");
      ESP.restart();
     }
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_clientid, mqtt_user, mqtt_pass, deviceTopic("$state"), MQTTQOS1, true, "lost")) {
      Serial.println("connected");
      for (int i=0; i<SWITCH_COUNT; i++) {
        client.subscribe(nNodeTopic("switch", i, "power/set"));
      }
      publish_homie_device_info();
      lightShow();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void publish_homie_stats() {
  char tempStr[10];
  snprintf(tempStr, sizeof(tempStr), "%d", TEMP_REQUEST_DELAY/1000);
  client.publish(deviceTopic("$stats/interval"), tempStr, true);
  snprintf(tempStr, sizeof(tempStr), "%d", uptime());
  client.publish(deviceTopic("$stats/uptime"), tempStr, true);
}

void oneWireLoop() {
  char tempStr[10];
  
  if ( millis() - lastTempRequest >= TEMP_REQUEST_DELAY ) {
    Serial.println("Requesting temperatures...");
    sensors.requestTemperatures();
    tempReqState = TEMP_REQUESTED;
    lastTempRequest = millis();
  }

  if ( (tempReqState == TEMP_REQUESTED) && (millis() - lastTempRequest >= delayInMillis) )
  {
    publish_homie_stats();
    digitalWrite(LED_BUILTIN, LOW);
    for (int i=0; i<themoDeviceCount; i++) {
      char* topic = nNodeTopic("thermometer", i, "temperature");
      snprintf(tempStr, sizeof(tempStr), "%f", sensors.getTempC(deviceAddresses[i]));
      Serial.printf("Publishing value %s to topic %s.", tempStr, topic);
      Serial.println();
      client.publish(topic, tempStr, true);
    }
    digitalWrite(LED_BUILTIN, HIGH);
    tempReqState = TEMP_WAITING;
  }
}

void loop() {
  ArduinoOTA.handle();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  oneWireLoop();
}
