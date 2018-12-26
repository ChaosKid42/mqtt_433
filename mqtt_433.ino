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

WiFiClient espClient;
PubSubClient client(espClient);

// Create a transmitter on address address_433, using digital pin PIN433 to transmit,
// with a period duration of 260ms (default), repeating the transmitted
// code 2^3=8 times.
NewRemoteTransmitter transmitter(address_433, PIN433, 260, 3);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress *deviceAddresses = new DeviceAddress[MAX_DEVICECOUNT];
unsigned int deviceCount = 0;
unsigned int  maxResolution = 0;
unsigned long lastTempRequest = 0;
unsigned int delayInMillis = 0;
unsigned int tempReqState = TEMP_WAITING;

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
  deviceCount = _min(sensors.getDeviceCount(), MAX_DEVICECOUNT);
  Serial.print(deviceCount);
  Serial.println(" devices.");
  for (int i=0; i<deviceCount; i++) {
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

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("Message arrived with length %d [", length);
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, "homie/nodemcu/switch-0/power/set") == 0)
    execSwitchCmd(15, (char)payload[0] == 't', "homie/nodemcu/switch-0/power");

  if (strcmp(topic, "homie/nodemcu/switch-1/power/set") == 0)
    execSwitchCmd(14, (char)payload[0] == 't', "homie/nodemcu/switch-1/power");

  if (strcmp(topic, "homie/nodemcu/switch-2/power/set") == 0)
    execSwitchCmd(13, (char)payload[0] == 't', "homie/nodemcu/switch-2/power");

  if (strcmp(topic, "homie/nodemcu/switch-3/power/set") == 0)
    execSwitchCmd(12, (char)payload[0] == 't', "homie/nodemcu/switch-3/power");

  if (strcmp(topic, "homie/nodemcu/switch-4/power/set") == 0)
    execSwitchCmd(11, (char)payload[0] == 't', "homie/nodemcu/switch-4/power");

  if (strcmp(topic, "homie/nodemcu/switch-5/power/set") == 0)
    execSwitchCmd(10, (char)payload[0] == 't', "homie/nodemcu/switch-5/power");
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
    if (client.connect(mqtt_clientid, mqtt_user, mqtt_pass, "homie/nodemcu/$state", MQTTQOS1, true, "lost")) {
      Serial.println("connected");
      client.publish("homie/nodemcu/$state", "init", true);
      client.publish("homie/nodemcu/$homie", "3.0.0", true);
      client.publish("homie/nodemcu/$name", "NodeMCU", true);
      client.publish("homie/nodemcu/$localip", WiFi.localIP().toString().c_str(), true);
      client.publish("homie/nodemcu/$mac", WiFi.macAddress().c_str(), true);
      client.publish("homie/nodemcu/$fw/name", "ScholliFW", true);
      client.publish("homie/nodemcu/$fw/version", "1.0", true);
      client.publish("homie/nodemcu/$nodes", "thermometer-0,thermometer-1,switch-0,switch-1,switch-2,switch-3,switch-4,switch-5", true);
      client.publish("homie/nodemcu/$implementation", "esp8266", true);
      client.publish("homie/nodemcu/$state", "ready", true);

      client.publish("homie/nodemcu/thermometer-0/$name", "Themometer 0", true);
      client.publish("homie/nodemcu/thermometer-0/$properties", "temperature", true);
      client.publish("homie/nodemcu/thermometer-0/temperature/$name", "Temperatur Themometer 0", true);
      client.publish("homie/nodemcu/thermometer-0/temperature/$unit", "°C", true);
      client.publish("homie/nodemcu/thermometer-0/temperature/$datatype", "float", true);

      client.publish("homie/nodemcu/thermometer-1/$name", "Themometer 1", true);
      client.publish("homie/nodemcu/thermometer-1/$properties", "temperature", true);
      client.publish("homie/nodemcu/thermometer-1/temperature/$name", "Temperatur Themometer 1", true);
      client.publish("homie/nodemcu/thermometer-1/temperature/$unit", "°C", true);
      client.publish("homie/nodemcu/thermometer-1/temperature/$datatype", "float", true);

      client.publish("homie/nodemcu/switch-0/$name", "Licht-OG-Terrarien", true);
      client.publish("homie/nodemcu/switch-0/$properties", "power", true);
      client.publish("homie/nodemcu/switch-0/power/$name", "Licht-OG-Terrarien", true);
      client.publish("homie/nodemcu/switch-0/power/$settable", "true", true);
      client.publish("homie/nodemcu/switch-0/power/$datatype", "boolean", true);

      client.publish("homie/nodemcu/switch-1/$name", "Licht-EG-Flur", true);
      client.publish("homie/nodemcu/switch-1/$properties", "power", true);
      client.publish("homie/nodemcu/switch-1/power/$name", "Licht-EG-Flur", true);
      client.publish("homie/nodemcu/switch-1/power/$settable", "true", true);
      client.publish("homie/nodemcu/switch-1/power/$datatype", "boolean", true);

      client.publish("homie/nodemcu/switch-2/$name", "Licht-EG-Wohnzimmer-Stehlampe", true);
      client.publish("homie/nodemcu/switch-2/$properties", "power", true);
      client.publish("homie/nodemcu/switch-2/power/$name", "Licht-EG-Wohnzimmer-Stehlampe", true);
      client.publish("homie/nodemcu/switch-2/power/$settable", "true", true);
      client.publish("homie/nodemcu/switch-2/power/$datatype", "boolean", true);

      client.publish("homie/nodemcu/switch-3/$name", "Licht-OG-Flur", true);
      client.publish("homie/nodemcu/switch-3/$properties", "power", true);
      client.publish("homie/nodemcu/switch-3/power/$name", "Licht-OG-Flur", true);
      client.publish("homie/nodemcu/switch-3/power/$settable", "true", true);
      client.publish("homie/nodemcu/switch-3/power/$datatype", "boolean", true);

      client.publish("homie/nodemcu/switch-4/$name", "Licht-Baum", true);
      client.publish("homie/nodemcu/switch-4/$properties", "power", true);
      client.publish("homie/nodemcu/switch-4/power/$name", "Licht-Baum", true);
      client.publish("homie/nodemcu/switch-4/power/$settable", "true", true);
      client.publish("homie/nodemcu/switch-4/power/$datatype", "boolean", true);

      client.publish("homie/nodemcu/switch-5/$name", "Licht-EG-Wohnzimmer-Tiffany", true);
      client.publish("homie/nodemcu/switch-5/$properties", "power", true);
      client.publish("homie/nodemcu/switch-5/power/$name", "Licht-EG-Wohnzimmer-Tiffany", true);
      client.publish("homie/nodemcu/switch-5/power/$settable", "true", true);
      client.publish("homie/nodemcu/switch-5/power/$datatype", "boolean", true);


      client.subscribe("homie/nodemcu/switch-0/power/set");
      client.subscribe("homie/nodemcu/switch-1/power/set");
      client.subscribe("homie/nodemcu/switch-2/power/set");
      client.subscribe("homie/nodemcu/switch-3/power/set");
      client.subscribe("homie/nodemcu/switch-4/power/set");
      client.subscribe("homie/nodemcu/switch-5/power/set");

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

void oneWireLoop() {
  char topic[50];
  char tempStr[6];
  
  if ( millis() - lastTempRequest >= TEMP_REQUEST_DELAY ) {
    Serial.println("Requesting temperatures...");
    sensors.requestTemperatures();
    tempReqState = TEMP_REQUESTED;
    lastTempRequest = millis();
  }

  if ( (tempReqState == TEMP_REQUESTED) && (millis() - lastTempRequest >= delayInMillis) )
  {
    digitalWrite(LED_BUILTIN, LOW);
    for (int i=0; i<deviceCount; i++) {
      snprintf(topic, 50, "homie/nodemcu/thermometer-%d/temperature", i);
      dtostrf(sensors.getTempC(deviceAddresses[i]), 0, 2, tempStr);
      Serial.printf("Publishing value %s to topic %s.", tempStr, topic);
      Serial.println();
      client.publish(topic, tempStr, true);
    }
    client.publish("homie/nodemcu/$stats/interval", String(TEMP_REQUEST_DELAY/1000).c_str(), true);
    client.publish("homie/nodemcu/$stats", "uptime", true);
    client.publish("homie/nodemcu/$stats/uptime", String(uptime()).c_str(), true);
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
