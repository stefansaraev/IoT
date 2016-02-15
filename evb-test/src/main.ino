/*
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Olimex ESP8266-EVB (modwifi) test.
    On-board button
    On-board relay
    HC-SR501 IR PIR Motion Sensor
    DHT22/AM2302 Digital Temperature And Humidity Sensor
    BH1750FVI Light Sensor
*/

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <PubSubClient.h>

// mqtt
const char* mqtt_out = "/test/evb/in";
const char* mqtt_in = "/test/evb/out";
const char* mqtt_serv;
const char* mqtt_user;
const char* mqtt_pass;
unsigned long mqtt_last_retry = 0;
unsigned long mqtt_retry_interval = 5000;
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

// button
const int PIN_BUTTON = 0;
volatile unsigned long btn_last_pressed = 0;
volatile unsigned long btn_last_released = 0;
enum BTN_EVENTS
{
  EV_NONE = 0, EV_SHORTPRESS, EV_LONGPRESS
};
volatile int btn_state = EV_NONE;

// relay
const int PIN_RELAY = 5;
bool relay_publish_mqtt = false;

// pir
const int PIN_PIR = 13;
bool pir_publish_mqtt = false;
bool pir_armed = false;

// dht
const int PIN_DHT = 12;
const int DHT_TYPE = DHT22;
unsigned long dht_last_read = 0;
unsigned long dht_read_interval = (1000UL * 60 * 1);
DHT dht(PIN_DHT, DHT_TYPE, 28);

// lightsensor
BH1750 light;
unsigned long light_last_read = 0;
unsigned long light_read_interval = (1000UL * 60 * 1);

// i2c
static int PIN_SDA = 2;
static int PIN_SCL = 4;

String ipaddr2str(IPAddress addr)
{
  return  String(addr[0]) + "." +
          String(addr[1]) + "." +
          String(addr[2]) + "." +
          String(addr[3]);
}

String payload2str(byte* byte, unsigned int length)
{
  String str;
  for (int i = 0; i < length; i++)
    str += (char)byte[i];
  return str;
}

void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
  String str = payload2str(payload, length);
  Serial.println("MQTT: Message arrived: topic: " + String(topic) + ", Payload: " + str);

  if (str == "relay/0")
    digitalWrite(PIN_RELAY, LOW);
  if (str == "relay/1")
    digitalWrite(PIN_RELAY, HIGH);
  if (str == "pir/arm")
    pir_armed = true;
  if (str == "pir/disarm")
    pir_armed = false;
}

void mqtt_connect()
{
  if (!mqtt_client.connected() && mqtt_serv)
  {
    if ((millis() - mqtt_last_retry) > mqtt_retry_interval)
    {
      Serial.println("MQTT: Connecting ...");
      if (mqtt_client.connect("EVB", mqtt_user, mqtt_pass))
      {
        Serial.println("MQTT: Connected");
        mqtt_client.publish(mqtt_out, "hello world");
        mqtt_client.subscribe(mqtt_in);
      }
      else
      {
        Serial.println("MQTT: failed, rc="
            + (String)mqtt_client.state()
            + ", reconnect in "
            + mqtt_retry_interval/1000 + "s");
      }
      mqtt_last_retry = millis();
    }
  }
}

void publishPIN(int PIN)
{
  String pin_str;
  switch (PIN)
  {
    case PIN_RELAY:
      pin_str = "relay/";
      break;
    case PIN_PIR:
      pin_str = "pir/";
      break;
    default:
      pin_str = (String)PIN + "/";
  }
  mqtt_client.publish(mqtt_out, (pin_str + String(digitalRead(PIN))).c_str());
}

void checkBUTTONChange()
{
  btn_state = EV_NONE;
  switch (digitalRead(PIN_BUTTON))
  {
    case LOW:
      btn_last_pressed = millis();
      break;
    case HIGH:
      btn_state = EV_SHORTPRESS;
      btn_last_released = millis();
      if ((btn_last_released - btn_last_pressed) > 2000)
        btn_state = EV_LONGPRESS;
      break;
  }
}

void checkRELAYChange()
{
  relay_publish_mqtt = true;
}

void checkPIRChange()
{
  pir_publish_mqtt = true;
}

void initSPIFFS()
{
  if (!SPIFFS.begin())
    Serial.println("SPIFFS: Failed to mount SPIFFS");
}

void initWifi()
{
  File file = SPIFFS.open("/wifi.txt", "r");
  if (!file)
  {
    Serial.println("WIFI: error opening /wifi.txt");
    return;
  }

  String ssid = file.readStringUntil('\n');
  String pass = file.readStringUntil('\n');
  if (!ssid)
  {
    Serial.println("WIFI: SSID not defined");
    return;
  }

  Serial.print("WIFI: Connecting to: " + ssid + " ...");
  WiFi.begin(ssid.c_str(), pass.c_str());
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WIFI: Connected. IP address: " + ipaddr2str(WiFi.localIP()));
}

void initOTA()
{
  File file = SPIFFS.open("/ota.txt", "r");
  if (!file)
  {
    Serial.println("OTA: error opening /ota.txt");
    return;
  }

  String pass = file.readStringUntil('\n');;
  if (!pass)
  {
    Serial.println("OTA: password not defined");
    return;
  }

  ArduinoOTA.setPassword(pass.c_str());
  ArduinoOTA.begin();
}

void initMQTT()
{
  File file = SPIFFS.open("/mqtt.txt", "r");
  if (!file)
  {
    Serial.println("MQTT: error opening /mqtt.txt");
    return;
  }

  String serv = file.readStringUntil('\n');
  String user = file.readStringUntil('\n');
  String pass = file.readStringUntil('\n');
  mqtt_serv = serv.c_str();
  mqtt_user = user.c_str();
  mqtt_pass = pass.c_str();
  if (!mqtt_serv)
  {
    Serial.println("MQTT: server not defined");
    return;
  }

  mqtt_client.setServer(mqtt_serv, 1883);
  mqtt_client.setCallback(mqtt_callback);
  mqtt_connect();
}

void initDHT()
{
  dht.begin();
}

void initLIGHT()
{
  light.begin();
}

void handleOTA()
{
  ArduinoOTA.handle();
}

void handleMQTT()
{
  if (!mqtt_client.connected())
    initMQTT();
  mqtt_client.loop();
}

void handleBUTTON()
{
  if (btn_state > EV_NONE)
  {
    switch (btn_state)
    {
      case EV_SHORTPRESS:
        digitalWrite(PIN_RELAY, !digitalRead(PIN_RELAY));
        break;
      case EV_LONGPRESS:
        digitalWrite(PIN_RELAY, LOW);
        break;
    }
    btn_last_pressed = 0;
    btn_last_released = 0;
    btn_state = EV_NONE;
  }
}

void handleRELAY()
{
  if (relay_publish_mqtt)
  {
    Serial.println("RELAY: State: " + (String)digitalRead(PIN_RELAY));
    publishPIN(PIN_RELAY);
    relay_publish_mqtt = false;
  }
}

void handlePIR()
{
  if (pir_publish_mqtt)
  {
    Serial.println("PIR: State: " + (String)digitalRead(PIN_PIR));
    if (pir_armed)
      publishPIN(PIN_PIR);
    pir_publish_mqtt = false;
  }
}

void handleDHT()
{
  if ((millis() - dht_last_read) > dht_read_interval)
  {
    dht_last_read = millis();
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (isnan(temperature) || isnan(humidity))
    {
      dht_last_read = millis() - dht_read_interval + 5000; // HACK wait 5sec
      return;
    }
    mqtt_client.publish(mqtt_out, String("temperature/" + (String)temperature).c_str());
    mqtt_client.publish(mqtt_out, String("humidity/" + (String)humidity).c_str());
  }
}

void handleLIGHT()
{
  if ((millis() - light_last_read) > light_read_interval)
  {
    light_last_read = millis();
    uint16_t lux = light.readLightLevel();
    mqtt_client.publish(mqtt_out, String("light/" + (String)lux).c_str());
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\n\n");
  Serial.println("Starting up ...");
  Wire.begin(PIN_SDA, PIN_SCL);
  pinMode(PIN_BUTTON, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_PIR, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), checkBUTTONChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_RELAY), checkRELAYChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_PIR), checkPIRChange, CHANGE);
  initSPIFFS();
  initWifi();
  initOTA();
  initMQTT();
  initDHT();
  initLIGHT();
  Serial.println("... startup done");
}

void loop()
{
  handleOTA();
  handleMQTT();
  handleBUTTON();
  handleRELAY();
  handlePIR();
  handleDHT();
  handleLIGHT();
  delay(50);
}
