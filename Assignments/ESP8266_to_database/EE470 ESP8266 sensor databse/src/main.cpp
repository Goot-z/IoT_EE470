/*
Project/Program Name: EE 470 ESP8266 Sensor Node
File Name: main.cpp
Author: Steve Gutierrez
Date: 11/12/2026
Version: 1.0

Purpose:
Read the DHT11 and photoresistor when a switch activates, obtain a timestamp,
and send the measurement to the web database with automatic retries.

Inputs:
DHT11 data on D5, photoresistor output on A0, button on D6, tilt switch on D7,
and Wi-Fi credentials for the access point.

Outputs:
Sensor readings, timestamp, network status, and server result through USB
Serial; sensor records are sent to the PHP endpoint over HTTPS using GET.

Dependencies:
ESP8266 Arduino core (Wi-Fi, HTTP, and TLS), Adafruit DHT library,
ArduinoJson.

Usage Notes:
Button and tilt signals are active-low. Pending messages are kept in RAM and
are lost if the ESP8266 resets. TLS certificate verification is disabled in
this classroom prototype by setInsecure().
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

// NodeMCU ESP-12E pin assignments for the connected sensor modules.
constexpr uint8_t DHT_PIN = D5;
constexpr uint8_t PHOTORESISTOR_PIN = A0;
constexpr uint8_t BUTTON_PIN = D6;
constexpr uint8_t TILT_PIN = D7;

// Sensor type and the electrical level that indicates an activated switch.
constexpr uint8_t DHT_TYPE = DHT11;
constexpr uint8_t BUTTON_ACTIVE_LEVEL = LOW;
constexpr uint8_t TILT_ACTIVE_LEVEL = LOW;
constexpr unsigned long DEBOUNCE_MS = 40;

// Replace these placeholders locally with the 2.4 GHz Wi-Fi network details.
// Do not publish real network passwords in a public source-code repository.
const char *WIFI_SSID = "Sample_Network";
const char *WIFI_PASSWORD = "Sample_Password";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;

// PHP endpoint that validates and inserts one sensor measurement.
const char *INSERT_URL =
"https://mediumspringgreen-meerkat-888583.hostingersite.com/insert_sensor.php";

// Display label is shown in the serial menu; ianaName is sent to TimeAPI.
struct TimeZoneOption {
const char *label;
const char *ianaName;
};

// Keep this order aligned with the numbered choices printed by printTimeZoneMenu().
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
// Array indexes start at zero, so index 3 selects menu option 4 (Pacific Time).
constexpr uint8_t DEFAULT_TIME_ZONE_INDEX = 3; // Option 4: Pacific Time
constexpr unsigned long TIME_ZONE_MENU_TIMEOUT_MS = 10000UL;

// Updated by selectTimeZone(); used when constructing the TimeAPI request.
uint8_t selectedTimeZoneIndex = DEFAULT_TIME_ZONE_INDEX;

// DHT driver object configured for the selected data pin and sensor type.
DHT dht(DHT_PIN, DHT_TYPE);

// Holds one snapshot of all three sensor measurements.
struct SensorReadings {
float temperatureC;
float humidityPercent;
uint16_t lightAdc;
};

// New messages are sent immediately. Unaccepted messages are retried at this interval.
constexpr uint8_t MAX_PENDING_MESSAGES = 4;
constexpr unsigned long RETRY_INTERVAL_MS = 30000UL;

// Stores an unchanged record while it waits for successful delivery.
struct PendingMeasurement {
char nodeName[8];
char timestamp[20];
SensorReadings readings;
unsigned long lastAttemptMs;
bool attempted;
};

// Fixed-size RAM queue; the board reset clears pending records.
PendingMeasurement pendingMeasurements[MAX_PENDING_MESSAGES];
uint8_t pendingCount = 0;

// Tracks raw and debounced state for one active-low switch input.
struct DebouncedSwitch {
uint8_t pin;                    // GPIO pin connected to the switch output
uint8_t activeLevel;            // LOW or HIGH means "activated"
bool lastRawActive;             // Most recently sampled, unfiltered state
bool stableActive;              // State accepted after debounce time
unsigned long lastRawChangeMs;  // Time when the raw input last changed
};

DebouncedSwitch buttonInput = {
BUTTON_PIN, BUTTON_ACTIVE_LEVEL, false, false, 0
};

DebouncedSwitch tiltInput = {
TILT_PIN, TILT_ACTIVE_LEVEL, false, false, 0
};

// Forward declarations keep setup(), loop(), and helper functions easy to scan.
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
// USB serial is used for the time-zone menu and diagnostic output.
Serial.begin(115200);
delay(100);

// Choose the time zone first, then join the configured Wi-Fi access point.
selectTimeZone();
connectToWiFi();

// Initialize sensor and input pins after the startup prompts/network attempt.
dht.begin();

pinMode(PHOTORESISTOR_PIN, INPUT);
beginSwitch(buttonInput);
beginSwitch(tiltInput);

Serial.println();
Serial.println("Sensor node ready.");
Serial.println("Press the button or activate the tilt switch.");

}

void loop() {
// Check for new switch activations, then retry any queued records that are due.
processActivationEvents();
servicePendingMeasurements();
}

bool connectToWiFi() {
// Station mode connects the ESP8266 to an existing router or hotspot.
WiFi.mode(WIFI_STA);
WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

Serial.print("Connecting to Wi-Fi");

// Stop waiting after the configured timeout so sensor code can still run offline.
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

// These values are required for the assignment's network/RSSI questions.
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
// The destination must fit YYYY-MM-DD HH:mm plus the null terminator.
if (bufferSize < 20 || WiFi.status() != WL_CONNECTED) {
Serial.println("Cannot get timestamp: Wi-Fi is unavailable.");
return false;
}

BearSSL::WiFiClientSecure secureClient;

// Simplifies this classroom API test, but does not verify the server certificate.
secureClient.setInsecure();

HTTPClient https;

// Request current local time for the selected IANA time-zone identifier.
const String url =
    String("https://timeapi.io/api/Time/current/zone?timeZone=")
    + TIME_ZONES[selectedTimeZoneIndex].ianaName;

if (!https.begin(secureClient, url)) {
    Serial.println("Could not start the TimeAPI request.");
    return false;
}

// TimeAPI is queried with HTTP GET; only parse a successful HTTP 200 response.
https.setTimeout(10000);
const int httpCode = https.GET();

if (httpCode != HTTP_CODE_OK) {
    Serial.print("TimeAPI HTTP error: ");
    Serial.println(httpCode);
    https.end();
    return false;
}

// Read the JSON response body before closing the HTTP connection.
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

// Extract the ISO-like dateTime value from TimeAPI's JSON object.
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
// No transmission can be attempted without an active Wi-Fi connection.
if (WiFi.status() != WL_CONNECTED) {
Serial.println("Cannot send measurement: Wi-Fi is disconnected.");
return false;
}

BearSSL::WiFiClientSecure secureClient;
secureClient.setInsecure();

HTTPClient https;

// Encode the space so it is safe inside the URL query string.
String encodedTimestamp = String(timestamp);
encodedTimestamp.replace(" ", "%20");

// The PHP script expects these named GET parameters for every record.
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

// Send the request and capture both the HTTP status and response text.
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

// STORED is a new insert; ALREADY_STORED is also success for retry purposes.
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
// Keep the original node, timestamp, and readings so retries resend the same data.
if (pendingCount >= MAX_PENDING_MESSAGES) {
Serial.println("Pending queue is full; record was not queued.");
return;
}

PendingMeasurement &pending = pendingMeasurements[pendingCount];

// Copy strings into fixed-size buffers owned by the queue entry.
snprintf(pending.nodeName, sizeof(pending.nodeName), "%s", nodeName);
snprintf(pending.timestamp, sizeof(pending.timestamp), "%s", timestamp);
pending.readings = readings;
pending.lastAttemptMs = 0;
pending.attempted = false;

pendingCount++;
Serial.println("Measurement queued for delivery.");

}

void servicePendingMeasurements() {
// Attempt each new item immediately; retry failed items every 30 seconds.
uint8_t index = 0;

while (index < pendingCount) {
    PendingMeasurement &pending = pendingMeasurements[index];

    // Unsigned millis() subtraction remains safe when the counter wraps around.
    const bool retryIsDue =
        !pending.attempted
        || millis() - pending.lastAttemptMs >= RETRY_INTERVAL_MS;

    if (!retryIsDue) {
        index++;
        continue;
    }

    // A successful response removes the record; otherwise its retry timer is reset.
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
// Compact the array so later queued records remain contiguous.
for (uint8_t i = index; i + 1 < pendingCount; i++) {
pendingMeasurements[i] = pendingMeasurements[i + 1];
}

pendingCount--;

}

void printTimeZoneMenu() {
// Show the options over USB Serial; the selected number indexes TIME_ZONES[].
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
// Report both the human-readable label and the API's IANA identifier.
Serial.print("Selected time zone: ");
Serial.println(TIME_ZONES[selectedTimeZoneIndex].label);

Serial.print("IANA zone ID: ");
Serial.println(TIME_ZONES[selectedTimeZoneIndex].ianaName);

}

void selectTimeZone() {
// Wait briefly for one menu choice; no valid input selects Pacific Time.
printTimeZoneMenu();

const unsigned long startMs = millis();
// The menu accepts a single digit (1 through 7), followed by Enter.
char input[4] = {0};
uint8_t inputLength = 0;

while (millis() - startMs < TIME_ZONE_MENU_TIMEOUT_MS) {
    while (Serial.available() > 0) {
        const char incoming = static_cast<char>(Serial.read());

        // Serial monitors commonly send CR, LF, or CRLF when Enter is pressed.
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
// Enable the internal pull-up; the module pulls the input LOW when activated.
pinMode(input.pin, INPUT_PULLUP);

// Seed the debounce state from the current pin level to avoid a false startup event.
const bool activeNow = (digitalRead(input.pin) == input.activeLevel);
input.lastRawActive = activeNow;
input.stableActive = activeNow;
input.lastRawChangeMs = millis();

}

bool activationDetected(DebouncedSwitch &input) {
// Detect a stable transition into the active state, not a held switch level.
const bool rawActive = (digitalRead(input.pin) == input.activeLevel);

if (rawActive != input.lastRawActive) {
    input.lastRawActive = rawActive;
    input.lastRawChangeMs = millis();
}

// Accept a change only after the raw state has stayed steady for DEBOUNCE_MS.
if (rawActive != input.stableActive
    && millis() - input.lastRawChangeMs >= DEBOUNCE_MS) {
    input.stableActive = rawActive;

    // One true result per new press/tilt activation prevents repeated sends while held.
    return input.stableActive;
}

return false;

}

bool captureSensorReadings(SensorReadings &readings) {
// DHT11 provides temperature and humidity; reject the snapshot if either read fails.
const float humidity = dht.readHumidity();
const float temperature = dht.readTemperature(); // Celsius by default

if (isnan(humidity) || isnan(temperature)) {
    return false;
}

readings.temperatureC = temperature;
readings.humidityPercent = humidity;
// NodeMCU A0 converts the photoresistor module's analog output to a 0-1023 count.
readings.lightAdc = analogRead(PHOTORESISTOR_PIN);

return true;

}

void processActivationEvents() {
// The button represents node_1; the tilt switch represents node_2.
if (activationDetected(buttonInput)) {
captureAndReport("node_1");
}

if (activationDetected(tiltInput)) {
    captureAndReport("node_2");
}

}

void captureAndReport(const char *nodeName) {
// Capture one sensor snapshot in response to a single new switch activation.
SensorReadings readings;

if (!captureSensorReadings(readings)) {
    Serial.print("DHT11 read failed for ");
    Serial.println(nodeName);
    return;
}

// Fetch the selected-zone timestamp for the database's DATETIME field.
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

// Queue the complete record; loop() sends it now or retries it later if needed.
queueMeasurement(nodeName, readings, timestamp);

}