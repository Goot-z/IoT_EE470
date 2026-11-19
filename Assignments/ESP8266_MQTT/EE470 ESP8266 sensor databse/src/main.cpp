/*
 * Project/Program Name: EE 470 MQTT Sensor Node
 * File Name: main.cpp
 * Author: Steve Gutierrez
 *
 * Purpose:
 * Publish DHT11 and photoresistor readings to MQTT, receive MQTT commands for
 * the LED, and publish a short MQTT event when the D6 button is pressed.
 * The Python relay is the only component that inserts MQTT sensor data into
 * the Hostinger database.
 *
 * Inputs:
 * DHT11 data on D5, photoresistor analog output on A0, active-low button on D6,
 * active-low tilt switch on D7, and Wi-Fi credentials.
 *
 * Outputs:
 * Sensor JSON on SENSOR_TOPIC, button values 1 then 0 on SWITCH_TOPIC, and LED
 * state feedback on LED_STATE_TOPIC. Serial prints provide diagnostics.
 *
 * Dependencies:
 * ESP8266 Arduino core, Adafruit DHT library, ArduinoJson, and PubSubClient.
 */

#include <Arduino.h>
#include <DHT.h>
#include <math.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <cstring>
#include <WiFiClient.h>
#include <PubSubClient.h>

// NodeMCU ESP-12E pin assignments for the connected sensor modules.
constexpr uint8_t DHT_PIN = D5;
constexpr uint8_t PHOTORESISTOR_PIN = A0;
constexpr uint8_t BUTTON_PIN = D6;
constexpr uint8_t TILT_PIN = D7;
constexpr uint8_t LED_PIN = D1;

constexpr uint8_t DHT_TYPE = DHT11;
constexpr uint8_t BUTTON_ACTIVE_LEVEL = LOW;
constexpr uint8_t TILT_ACTIVE_LEVEL = LOW;
constexpr unsigned long DEBOUNCE_MS = 40UL;

// Replace these placeholders locally with the 2.4 GHz Wi-Fi network details.
// Do not publish real Wi-Fi credentials in a public source-code repository.
const char *WIFI_SSID = "SampleWifi";
const char *WIFI_PASSWORD = "SamplePassword";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;

// Public HiveMQ broker and this node's MQTT topics.
const char *MQTT_BROKER = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT = 1883;
const char *SENSOR_TOPIC = "testtopic/temp/outTopic/gootz";
const char *LED_COMMAND_TOPIC = "testtopic/temp/inTopic/gootz";
const char *LED_STATE_TOPIC = "testtopic/temp/ledState/gootz";
const char *SWITCH_TOPIC = "testtopic/temp/switch/gootz";

constexpr unsigned long MQTT_PUBLISH_INTERVAL_MS = 15000UL;
constexpr unsigned long SWITCH_PULSE_MS = 5000UL;

WiFiClient mqttNetworkClient;
PubSubClient mqttClient(mqttNetworkClient);
DHT dht(DHT_PIN, DHT_TYPE);

struct SensorReadings {
    float temperatureC;
    float humidityPercent;
    uint16_t lightAdc;
};

struct DebouncedSwitch {
    uint8_t pin;
    uint8_t activeLevel;
    bool lastRawActive;
    bool stableActive;
    unsigned long lastRawChangeMs;
};

DebouncedSwitch buttonInput = {
    BUTTON_PIN, BUTTON_ACTIVE_LEVEL, false, false, 0
};
DebouncedSwitch tiltInput = {
    TILT_PIN, TILT_ACTIVE_LEVEL, false, false, 0
};

unsigned long lastMqttPublishMs = 0;
unsigned long switchPulseStartMs = 0;
bool switchPulseActive = false;

void beginSwitch(DebouncedSwitch &input);
bool activationDetected(DebouncedSwitch &input);
bool captureSensorReadings(SensorReadings &readings);
void processActivationEvents();
void captureAndReport(const char *nodeName);
bool connectToWiFi();
void publishMqttMeasurement();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void connectToMqtt();

void setup() {
    Serial.begin(115200);
    delay(100);

    connectToWiFi();

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    dht.begin();
    pinMode(PHOTORESISTOR_PIN, INPUT);
    beginSwitch(buttonInput);
    beginSwitch(tiltInput);

    Serial.println();
    Serial.println("MQTT sensor node ready.");
    Serial.println("Sensor data is sent to the database through the Python relay.");
}

void loop() {
    processActivationEvents();

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (!mqttClient.connected()) {
        connectToMqtt();
    }
    mqttClient.loop();

    // Complete the button pulse five seconds after it was published.
    if (switchPulseActive
        && millis() - switchPulseStartMs >= SWITCH_PULSE_MS
        && mqttClient.connected()) {
        if (mqttClient.publish(SWITCH_TOPIC, "0")) {
            Serial.println("Published switch value: 0");
            switchPulseActive = false;
        } else {
            Serial.println("Switch release publish failed; will retry.");
        }
    }

    // Publish the current sensor values every 15 seconds.
    if (mqttClient.connected()
        && millis() - lastMqttPublishMs >= MQTT_PUBLISH_INTERVAL_MS) {
        lastMqttPublishMs = millis();
        publishMqttMeasurement();
    }
}

bool connectToWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to Wi-Fi");
    const unsigned long startMs = millis();

    while (WiFi.status() != WL_CONNECTED
           && millis() - startMs < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println();
        Serial.println("Wi-Fi connection failed; continuing offline.");
        return false;
    }

    Serial.println();
    Serial.println("Wi-Fi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
    return true;
}

void beginSwitch(DebouncedSwitch &input) {
    // The switch connects its input pin to GND when activated.
    pinMode(input.pin, INPUT_PULLUP);
    const bool activeNow = (digitalRead(input.pin) == input.activeLevel);
    input.lastRawActive = activeNow;
    input.stableActive = activeNow;
    input.lastRawChangeMs = millis();
}

bool activationDetected(DebouncedSwitch &input) {
    const bool rawActive = (digitalRead(input.pin) == input.activeLevel);

    if (rawActive != input.lastRawActive) {
        input.lastRawActive = rawActive;
        input.lastRawChangeMs = millis();
    }

    if (rawActive != input.stableActive
        && millis() - input.lastRawChangeMs >= DEBOUNCE_MS) {
        input.stableActive = rawActive;
        return input.stableActive;
    }

    return false;
}

bool captureSensorReadings(SensorReadings &readings) {
    const float humidity = dht.readHumidity();
    const float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
        return false;
    }

    readings.temperatureC = temperature;
    readings.humidityPercent = humidity;
    readings.lightAdc = analogRead(PHOTORESISTOR_PIN);
    return true;
}

void publishMqttMeasurement() {
    if (!mqttClient.connected()) {
        return;
    }

    SensorReadings readings;
    if (!captureSensorReadings(readings)) {
        Serial.println("Could not read sensors; MQTT publish skipped.");
        return;
    }

    JsonDocument document;
    document["node_name"] = "node_1";
    document["temperature"] = readings.temperatureC;
    document["humidity"] = readings.humidityPercent;
    document["light_adc"] = readings.lightAdc;

    char payload[160];
    const size_t length = serializeJson(document, payload, sizeof(payload));
    if (length == 0) {
        Serial.println("Could not create MQTT JSON payload.");
        return;
    }

    if (mqttClient.publish(SENSOR_TOPIC, payload)) {
        Serial.print("MQTT published: ");
        Serial.println(payload);
    } else {
        Serial.println("MQTT publish failed.");
    }
}

void processActivationEvents() {
    if (activationDetected(buttonInput)) {
        captureAndReport("node_1");

        if (mqttClient.connected() && mqttClient.publish(SWITCH_TOPIC, "1")) {
            Serial.println("Published switch value: 1");
            switchPulseActive = true;
            switchPulseStartMs = millis();
        } else {
            Serial.println("Could not publish switch value: 1.");
        }
    }

    if (activationDetected(tiltInput)) {
        captureAndReport("node_2");
    }
}

void captureAndReport(const char *nodeName) {
    SensorReadings readings;
    if (!captureSensorReadings(readings)) {
        Serial.print("DHT11 read failed for ");
        Serial.println(nodeName);
        return;
    }

    Serial.print("Node: ");
    Serial.println(nodeName);
    Serial.print("Temperature (C): ");
    Serial.println(readings.temperatureC, 1);
    Serial.print("Humidity (%): ");
    Serial.println(readings.humidityPercent, 1);
    Serial.print("Photoresistor ADC (0-1023): ");
    Serial.println(readings.lightAdc);
    Serial.println();

    // No HTTP/database insert occurs here. Periodic MQTT messages carry the data.
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
    if (strcmp(topic, LED_COMMAND_TOPIC) != 0 || length != 1) {
        return;
    }

    const char command = static_cast<char>(payload[0]);
    if (command == '1') {
        digitalWrite(LED_PIN, HIGH);
        mqttClient.publish(LED_STATE_TOPIC, "1", true);
        Serial.println("MQTT command: LED ON");
    } else if (command == '0') {
        digitalWrite(LED_PIN, LOW);
        mqttClient.publish(LED_STATE_TOPIC, "0", true);
        Serial.println("MQTT command: LED OFF");
    }
}

void connectToMqtt() {
    while (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
        Serial.print("Connecting to MQTT...");

        String clientId = "ee470-gootz-";
        clientId += String(ESP.getChipId(), HEX);

        if (mqttClient.connect(clientId.c_str())) {
            Serial.println("connected");
            mqttClient.subscribe(LED_COMMAND_TOPIC);
            mqttClient.publish(LED_STATE_TOPIC, "0", true);
        } else {
            Serial.print("failed, state=");
            Serial.println(mqttClient.state());
            delay(3000);
        }
    }
}