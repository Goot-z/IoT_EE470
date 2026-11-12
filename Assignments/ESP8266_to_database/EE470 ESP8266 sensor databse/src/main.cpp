/*
 * Project/Program Name: EE 470 ESP8266 Sensor Node
 * File Name: main.cpp
 * Author: Steve Gutierrez
 * Date: 11/11/2026
 * Version: 1.0
 *
 * Purpose:
 * Read the DHT11 and photoresistor when the button or tilt switch activates.
 *
 * Inputs:
 * temp/humidity on D5, photoresistor on A0, button on D6, tilt switch on D7.
 *
 * Outputs:
 * Sensor readings and node name through the USB serial interface.
 *
 * Dependencies:
 * ESP8266 Arduino framework, Adafruit DHT sensor library.
 *
 * Usage Notes:
 * Assumes button and tilt signals are active-low.
 */

#include <Arduino.h>
#include <DHT.h>
#include <math.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <cstring>
#include <cstdio>

// NodeMCU ESP-12E pin assignments
constexpr uint8_t DHT_PIN = D5;
constexpr uint8_t PHOTORESISTOR_PIN = A0;
constexpr uint8_t BUTTON_PIN = D6;
constexpr uint8_t TILT_PIN = D7;

constexpr uint8_t DHT_TYPE = DHT11;
constexpr uint8_t BUTTON_ACTIVE_LEVEL = LOW;
constexpr uint8_t TILT_ACTIVE_LEVEL = LOW;
constexpr unsigned long DEBOUNCE_MS = 40;

const char *WIFI_SSID = "Gutierrez Network";
const char *WIFI_PASSWORD = "7074904840";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;

const char *INSERT_URL =
    "https://mediumspringgreen-meerkat-888583.hostingersite.com/insert_sensor.php";


struct TimeZoneOption {
    const char *label;
    const char *ianaName;
};

const TimeZoneOption TIME_ZONES[] = {
    {"ET - New York",          "America/New_York"},
    {"CT - Chicago",            "America/Chicago"},
    {"MT - Denver",             "America/Denver"},
    {"PT - Los Angeles",        "America/Los_Angeles"},
    {"AKT - Anchorage",         "America/Anchorage"},
    {"HAT - Honolulu",          "Pacific/Honolulu"},
    {"AT - San Juan",           "America/Puerto_Rico"}
};

constexpr uint8_t TIME_ZONE_COUNT =
    sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);
constexpr uint8_t DEFAULT_TIME_ZONE_INDEX = 3; // Option 4: Pacific Time
constexpr unsigned long TIME_ZONE_MENU_TIMEOUT_MS = 10000UL;

uint8_t selectedTimeZoneIndex = DEFAULT_TIME_ZONE_INDEX;

DHT dht(DHT_PIN, DHT_TYPE);

struct SensorReadings {
    float temperatureC;
    float humidityPercent;
    uint16_t lightAdc;
};

constexpr uint8_t MAX_PENDING_MESSAGES = 4;
constexpr unsigned long RETRY_INTERVAL_MS = 30000UL;

struct PendingMeasurement {
    char nodeName[8];
    char timestamp[20];
    SensorReadings readings;
    unsigned long lastAttemptMs;
    bool attempted;
};

PendingMeasurement pendingMeasurements[MAX_PENDING_MESSAGES];
uint8_t pendingCount = 0;

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

void beginSwitch(DebouncedSwitch &input);
bool activationDetected(DebouncedSwitch &input);
bool captureSensorReadings(SensorReadings &readings);
void processActivationEvents();
void captureAndReport(const char *nodeName);
void printTimeZoneMenu();
void printSelectedTimeZone();
void selectTimeZone();
bool connectToWiFi();
bool getCurrentTimestamp(char *timestamp, size_t bufferSize);

bool sendMeasurementToDatabase(
    const char *nodeName,
    const SensorReadings &readings,
    const char *timestamp
);

void queueMeasurement(
    const char *nodeName,
    const SensorReadings &readings,
    const char *timestamp
);
void servicePendingMeasurements();
void removePendingMeasurement(uint8_t index);

void setup() {
    Serial.begin(115200);
    delay(100);

    selectTimeZone();
    connectToWiFi();

    dht.begin();

    pinMode(PHOTORESISTOR_PIN, INPUT);
    beginSwitch(buttonInput);
    beginSwitch(tiltInput);

    Serial.println();
    Serial.println("Sensor node ready.");
    Serial.println("Press the button or activate the tilt switch.");
}

void loop() {
    processActivationEvents();
    servicePendingMeasurements();
}

bool connectToWiFi() {
    WiFi.mode(WIFI_STA);
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

bool getCurrentTimestamp(char *timestamp, size_t bufferSize) {
    if (bufferSize < 20 || WiFi.status() != WL_CONNECTED) {
        Serial.println("Cannot get timestamp: Wi-Fi is unavailable.");
        return false;
    }

    BearSSL::WiFiClientSecure secureClient;

    // Simplifies this classroom API test, but does not verify the server certificate.
    secureClient.setInsecure();

    HTTPClient https;

    const String url =
        String("https://timeapi.io/api/Time/current/zone?timeZone=")
        + TIME_ZONES[selectedTimeZoneIndex].ianaName;

    if (!https.begin(secureClient, url)) {
        Serial.println("Could not start the TimeAPI request.");
        return false;
    }

    https.setTimeout(10000);
    const int httpCode = https.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.print("TimeAPI HTTP error: ");
        Serial.println(httpCode);
        https.end();
        return false;
    }

    const String response = https.getString();
    https.end();

    JsonDocument document;
    const DeserializationError jsonError =
        deserializeJson(document, response);

    if (jsonError) {
        Serial.print("Could not parse TimeAPI JSON: ");
        Serial.println(jsonError.c_str());
        return false;
    }

    const char *apiDateTime = document["dateTime"];

    // Expected format begins YYYY-MM-DDTHH:mm:ss
    if (apiDateTime == nullptr
        || strlen(apiDateTime) < 19
        || apiDateTime[10] != 'T') {
        Serial.println("TimeAPI response has no usable dateTime field.");
        return false;
    }

    // Convert ISO format to the database format: YYYY-MM-DD HH:mm:ss
    const int written = snprintf(
        timestamp,
        bufferSize,
        "%.10s %.8s",
        apiDateTime,
        apiDateTime + 11
    );

    return written == 19;
}

bool sendMeasurementToDatabase(
    const char *nodeName,
    const SensorReadings &readings,
    const char *timestamp
) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Cannot send measurement: Wi-Fi is disconnected.");
        return false;
    }

    BearSSL::WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient https;

    String encodedTimestamp = String(timestamp);
    encodedTimestamp.replace(" ", "%20");

    String url = String(INSERT_URL)
        + "?node_name=" + nodeName
        + "&temperature=" + String(readings.temperatureC, 1)
        + "&humidity=" + String(readings.humidityPercent, 1)
        + "&light_adc=" + String(readings.lightAdc)
        + "&time_received=" + encodedTimestamp;

    if (!https.begin(secureClient, url)) {
        Serial.println("Could not start the database request.");
        return false;
    }

    https.setTimeout(10000);
    const int httpCode = https.GET();

    String response;
    if (httpCode > 0) {
        response = https.getString();
    }

    https.end();
    response.trim();

    Serial.print("Database HTTP status: ");
    Serial.println(httpCode);
    Serial.print("Database response: ");
    Serial.println(response);

    if (httpCode == HTTP_CODE_OK
        && (response == "STORED" || response == "ALREADY_STORED")) {
        Serial.println("Measurement accepted by the server.");
        return true;
    }

    Serial.println("Measurement was not confirmed by the server.");
    return false;
}

void queueMeasurement(
    const char *nodeName,
    const SensorReadings &readings,
    const char *timestamp
) {
    if (pendingCount >= MAX_PENDING_MESSAGES) {
        Serial.println("Pending queue is full; record was not queued.");
        return;
    }

    PendingMeasurement &pending = pendingMeasurements[pendingCount];

    snprintf(pending.nodeName, sizeof(pending.nodeName), "%s", nodeName);
    snprintf(pending.timestamp, sizeof(pending.timestamp), "%s", timestamp);
    pending.readings = readings;
    pending.lastAttemptMs = 0;
    pending.attempted = false;

    pendingCount++;
    Serial.println("Measurement queued for delivery.");
}

void servicePendingMeasurements() {
    uint8_t index = 0;

    while (index < pendingCount) {
        PendingMeasurement &pending = pendingMeasurements[index];

        const bool retryIsDue =
            !pending.attempted
            || millis() - pending.lastAttemptMs >= RETRY_INTERVAL_MS;

        if (!retryIsDue) {
            index++;
            continue;
        }

        const bool accepted = sendMeasurementToDatabase(
            pending.nodeName,
            pending.readings,
            pending.timestamp
        );

        if (accepted) {
            removePendingMeasurement(index);
            // The next item shifts into this index, so check it next.
        } else {
            pending.attempted = true;
            pending.lastAttemptMs = millis();
            Serial.println("Delivery failed; will retry in 30 seconds.");
            index++;
        }
    }
}

void removePendingMeasurement(uint8_t index) {
    for (uint8_t i = index; i + 1 < pendingCount; i++) {
        pendingMeasurements[i] = pendingMeasurements[i + 1];
    }

    pendingCount--;
}

void printTimeZoneMenu() {
    Serial.println();
    Serial.println("Select Your Time Zone (default is PT):");

    for (uint8_t i = 0; i < TIME_ZONE_COUNT; i++) {
        Serial.print(i + 1);
        Serial.print(") ");
        Serial.println(TIME_ZONES[i].label);
    }

    Serial.println("Enter 1-7 and press Enter within 10 seconds.");
}

void printSelectedTimeZone() {
    Serial.print("Selected time zone: ");
    Serial.println(TIME_ZONES[selectedTimeZoneIndex].label);

    Serial.print("IANA zone ID: ");
    Serial.println(TIME_ZONES[selectedTimeZoneIndex].ianaName);
}

void selectTimeZone() {
    printTimeZoneMenu();

    const unsigned long startMs = millis();
    char input[4] = {0};
    uint8_t inputLength = 0;

    while (millis() - startMs < TIME_ZONE_MENU_TIMEOUT_MS) {
        while (Serial.available() > 0) {
            const char incoming = static_cast<char>(Serial.read());

            if (incoming == '\r' || incoming == '\n') {
                if (inputLength == 1
                    && input[0] >= '1'
                    && input[0] <= '7') {
                    selectedTimeZoneIndex = input[0] - '1';
                    printSelectedTimeZone();
                    return;
                }

                if (inputLength > 0) {
                    Serial.println("Invalid choice. Enter a number from 1 to 7.");
                }

                inputLength = 0;
                input[0] = '\0';
            } else if (incoming >= '0' && incoming <= '9') {
                if (inputLength < sizeof(input) - 1) {
                    input[inputLength++] = incoming;
                    input[inputLength] = '\0';
                }
            }
        }

        delay(10);
    }

    selectedTimeZoneIndex = DEFAULT_TIME_ZONE_INDEX;
    Serial.println("No valid choice received; using the default.");
    printSelectedTimeZone();
}

void beginSwitch(DebouncedSwitch &input) {
    // The kit's switch modules are expected to pull the signal LOW when active.
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

    // Accept a state change only after it has remained stable through debounce.
    if (rawActive != input.stableActive
        && millis() - input.lastRawChangeMs >= DEBOUNCE_MS) {
        input.stableActive = rawActive;

        // Return true only on the transition into the active state.
        return input.stableActive;
    }

    return false;
}

bool captureSensorReadings(SensorReadings &readings) {
    const float humidity = dht.readHumidity();
    const float temperature = dht.readTemperature(); // Celsius by default

    if (isnan(humidity) || isnan(temperature)) {
        return false;
    }

    readings.temperatureC = temperature;
    readings.humidityPercent = humidity;
    readings.lightAdc = analogRead(PHOTORESISTOR_PIN);

    return true;
}

void processActivationEvents() {
    if (activationDetected(buttonInput)) {
        captureAndReport("node_1");
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
    
    char timestamp[20];

    if (!getCurrentTimestamp(timestamp, sizeof(timestamp))) {
        Serial.println("No valid timestamp; this activation cannot be transmitted.");
        return;
    }

    Serial.print("Timestamp: ");
    Serial.println(timestamp);

    Serial.print("Node: ");
    Serial.println(nodeName);

    Serial.print("Temperature (C): ");
    Serial.println(readings.temperatureC, 1);

    Serial.print("Humidity (%): ");
    Serial.println(readings.humidityPercent, 1);

    Serial.print("Photoresistor ADC (0-1023): ");
    Serial.println(readings.lightAdc);

    Serial.println();

    queueMeasurement(nodeName, readings, timestamp);
}