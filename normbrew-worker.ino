#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

// Configurable Items
#define WIFI_SSID "REPLACE_WITH_YOUR_SSID"
#define WIFI_PASSWORD "REPLACE_WITH_YOUR_PASSWORD"
#define MQTT_HOST IPAddress(192, 168, 1, 1)
#define MQTT_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
String mqttPubTemp = "normbrew/temperature";

// Buzzer
#define BUZZER_PIN D7

// OLED
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Output pins
int outputPins[] = { D0, D1, D2, D3, D4, D5, D6, D8 };

// Wifi
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

// MQTT
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

// ds18B20
#define ONE_WIRE_BUS 4
#define TEMPERATURE_PRECISION 9
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress tempDeviceAddress;
int numberOfTempDevices;
unsigned long previousMillis = 0;
const long interval = 10000;

void doBeeps(int numberOfBeeps) {
  for (int i = 0; i < numberOfBeeps; i++) {
    digitalWrite (BUZZER_PIN, HIGH);
    delay(250);
    digitalWrite (BUZZER_PIN, LOW);
    delay(250);
  }
}

void connectToWifi() {
  clearScreen();
  display.println(F("Connecting to "));
  display.print(String(WIFI_SSID).c_str());
  display.display();
  Serial.println("Connecting to wireless network");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  display.print(F(" - Connected!"));
  display.display();
  Serial.println("Connected to wireless network");
  
  doBeeps(2);
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from wireless network");
  mqttReconnectTimer.detach();
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
  display.println(F("Connecting to MQTT broker"));
  display.display();
  Serial.println("Connecting to MQTT");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  display.print(F(" - Connected!"));
  display.display();
  Serial.println("Connected to MQTT");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);

  doBeeps(3);
  delay(1000);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.print("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void clearScreen() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
}

void setupScreen() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  clearScreen();
  display.println(F("Normbrew Worker v0.1"));
  display.println(F("By MrNorm"));
  display.println(F("github.com/MrNorm/normbrew-worker"));
  display.display();
  delay(5000);
}

void setup() {
  Serial.begin(9600);
  Serial.println();

  // Set buzzer output
  pinMode (BUZZER_PIN, OUTPUT);
  doBeeps(1);

  // Set OLED screen
  setupScreen();

  // Set up sensors
  sensors.begin();
  numberOfTempDevices = sensors.getDeviceCount();
  Serial.print("Found ");
  Serial.print(numberOfTempDevices, DEC);
  Serial.println(" devices.");

  for (int i = 0; i < numberOfTempDevices; i++) {
    // Search the wire for address
    if (sensors.getAddress(tempDeviceAddress, i)) {
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      Serial.print(getAddress(tempDeviceAddress));
      Serial.println();
      sensors.setResolution(tempDeviceAddress, TEMPERATURE_PRECISION);
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }
  }

  // Wireless 
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  // Setup MQTT
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCredentials(MQTT_USERNAME, MQTT_PASSWORD);

  // Setup outputs
  for (int i=0; i<sizeof outputPins/sizeof outputPins[0]; i++) {
    pinMode(outputPins[i], OUTPUT);
    digitalWrite(outputPins[i], LOW);
  }

  // Connect
  connectToWifi();
}

void loop() {

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {

    previousMillis = currentMillis;
    sensors.requestTemperatures();

    for (int i = 0; i < numberOfTempDevices; i++) {
      if (sensors.getAddress(tempDeviceAddress, i)) {

        Serial.print("Temperature for device: ");
        Serial.println(i, DEC);

        float tempC = sensors.getTempC(tempDeviceAddress);
        mqttPubTemp = mqttPubTemp + "/" + getAddress(tempDeviceAddress);
        uint16_t packetIdPub1 = mqttClient.publish(String(mqttPubTemp).c_str(), 1, true, String(tempC).c_str());
        Serial.printf("Publishing on topic %s at QoS 1, packetId: %i ", String(mqttPubTemp).c_str(), packetIdPub1);
        Serial.printf("Message: %.2f \n", tempC);
      }
    }
  }
}

String getAddress(DeviceAddress deviceAddress) {
  String address;

  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
    address = address + deviceAddress[i];
  }
}
