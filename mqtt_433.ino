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
      client.publish(topic, "1");
    else
      client.publish(topic, "0"); 
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

  if (strcmp(topic, "nodemcu/switch/0/command") == 0)
    execSwitchCmd(15, (char)payload[0] == '1', "nodemcu/switch/0/state");

  if (strcmp(topic, "nodemcu/switch/1/command") == 0)
    execSwitchCmd(14, (char)payload[0] == '1', "nodemcu/switch/1/state");

  if (strcmp(topic, "nodemcu/switch/2/command") == 0)
    execSwitchCmd(13, (char)payload[0] == '1', "nodemcu/switch/2/state");

  if (strcmp(topic, "nodemcu/switch/3/command") == 0)
    execSwitchCmd(12, (char)payload[0] == '1', "nodemcu/switch/3/state");

  if (strcmp(topic, "nodemcu/switch/4/command") == 0)
    execSwitchCmd(11, (char)payload[0] == '1', "nodemcu/switch/4/state");

  if (strcmp(topic, "nodemcu/switch/5/command") == 0)
    execSwitchCmd(10, (char)payload[0] == '1', "nodemcu/switch/5/state");
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
    if (client.connect(mqtt_clientid, mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      client.subscribe("nodemcu/switch/0/command");
      client.subscribe("nodemcu/switch/1/command");
      client.subscribe("nodemcu/switch/2/command");
      client.subscribe("nodemcu/switch/3/command");
      client.subscribe("nodemcu/switch/4/command");
      client.subscribe("nodemcu/switch/5/command");
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
      snprintf(topic, 50, "nodemcu/temp/%d/state", i);
      dtostrf(sensors.getTempC(deviceAddresses[i]), 0, 2, tempStr);
      Serial.printf("Publishing value %s to topic %s.", tempStr, topic);
      Serial.println();
      client.publish(topic, tempStr);
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
