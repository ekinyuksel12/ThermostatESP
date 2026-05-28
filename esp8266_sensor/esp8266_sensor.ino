/*
 * esp8266_sensor.ino
 * 
 * Production-Ready Sensor Firmware (DHT11 on ESP-01) v2.0
 * -----------------------------------------
 * Features:
 * - DHT11 Temperature & Humidity Reading
 * - SinricPro v3.0+ Compatibility (Acts as Temperature Sensor)
 * - Relay Sync via HTTP POST
 * - Detailed JSON API (/api/status)
 * - Hardware Watchdog Enabled
 * - (OTA Removed for Maximum Flash/Memory Space)
 */

#define FIRMWARE_VERSION "1.0"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
#include "DHT.h"
#include "SinricPro.h"
#include "SinricProTemperaturesensor.h"

// --- Hardcoded Credentials ---
#define WIFI_SSID         "Don't Connect to Strangers"
#define WIFI_PASS         "ESTyukselEST3118889"
#define APP_KEY           "1095cf7a-6478-4e37-8eb4-f7d471cdda88"       // User needs to update
#define APP_SECRET        "4abb2e5e-a64f-4f3f-b189-2d5878564fd6-4ea6537e-9fa7-412c-a5d1-6de2a2459ad3"    // User needs to update
#define SENSOR_ID         "6a14e5aff9b5f15fa7df2249"     // User needs to update
#define RELAY_URL         "http://boiler.local/sensor" // URL of the boiler
// --- Hardware Constants ---
#define DHTPIN            2      // GPIO 2 on ESP-01 is typical for DHT11
#define DHTTYPE           DHT11 
#define BAUD_RATE         115200 

float tempOffset = -1.0f; // Default calibration offset is -1.0°C (customizable via API)

unsigned long currentSensorPollMs = 10000; // Start with 10s slow poll
#define RELAY_SYNC_MS     30000  // Send data to relay every 30 seconds
#define CLOUD_SYNC_MS     30000  // Send to Sinric Pro every 30 seconds

DHT dht(DHTPIN, DHTTYPE);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

struct SensorState {
    float rawTemp = 0.0f;
    float temp = 0.0f; // Calibrated temperature
    float hum = 0.0f;
    bool lastReadSuccess = false;
    unsigned long lastDHTRead = 0;
    unsigned long lastRelaySync = 0;
    unsigned long lastCloudSync = 0;

    // Diagnostic Counters
    uint32_t wifiDisconnectCount = 0;
    uint32_t dhtReadAttemptCount = 0;
    uint32_t dhtReadFailCount = 0;
    uint32_t relaySyncAttemptCount = 0;
    uint32_t relaySyncFailCount = 0;
    uint32_t cloudSyncAttemptCount = 0;
    uint32_t cloudSyncFailCount = 0;
} state;

// --- Web Server Handlers ---
void handleStatus() {
    JsonDocument doc;
    
    // 1. Environmental Data
    JsonObject env = doc.createNestedObject("environment");
    env["raw_temperature_c"] = state.rawTemp;
    env["calibrated_temperature_c"] = state.temp;
    env["temperature_c"] = state.temp; // Backwards compatibility
    env["humidity_pct"] = state.hum;
    env["temp_offset_c"] = tempOffset;
    env["read_success"] = state.lastReadSuccess;

    // 2. Network Metrics
    JsonObject net = doc.createNestedObject("network");
    net["ssid"] = WiFi.SSID();
    net["ip"] = WiFi.localIP().toString();
    net["rssi_dbm"] = WiFi.RSSI();
    net["mac"] = WiFi.macAddress();
    net["wifi_disconnect_count"] = state.wifiDisconnectCount;

    // 3. Hardware Health
    JsonObject hw = doc.createNestedObject("hardware");
    hw["chip_id"] = String(ESP.getChipId());
    hw["firmware_version"] = FIRMWARE_VERSION;
    hw["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    hw["flash_real_size_bytes"] = ESP.getFlashChipRealSize();
    hw["flash_ide_size_bytes"] = ESP.getFlashChipSize();
    hw["free_heap_bytes"] = ESP.getFreeHeap();
    hw["sketch_size_bytes"] = ESP.getSketchSize();
    hw["free_sketch_space_bytes"] = ESP.getFreeSketchSpace();
    hw["reset_reason"] = ESP.getResetReason();
    hw["uptime_seconds"] = millis() / 1000;

    // 4. Diagnostics & Fail Rates
    JsonObject diag = doc.createNestedObject("diagnostics");
    diag["dht_read_attempts"] = state.dhtReadAttemptCount;
    diag["dht_read_fails"] = state.dhtReadFailCount;
    diag["dht_fail_rate_pct"] = (state.dhtReadAttemptCount > 0) ? ((float)state.dhtReadFailCount / state.dhtReadAttemptCount * 100.0f) : 0.0f;
    diag["relay_sync_attempts"] = state.relaySyncAttemptCount;
    diag["relay_sync_fails"] = state.relaySyncFailCount;
    diag["relay_sync_fail_rate_pct"] = (state.relaySyncAttemptCount > 0) ? ((float)state.relaySyncFailCount / state.relaySyncAttemptCount * 100.0f) : 0.0f;
    diag["cloud_sync_attempts"] = state.cloudSyncAttemptCount;
    diag["cloud_sync_fails"] = state.cloudSyncFailCount;
    diag["cloud_sync_fail_rate_pct"] = (state.cloudSyncAttemptCount > 0) ? ((float)state.cloudSyncFailCount / state.cloudSyncAttemptCount * 100.0f) : 0.0f;

    String out; 
    serializeJsonPretty(doc, out);
    server.send(200, "application/json", out);
}

void handleSetOffset() {
    if (server.hasArg("offset")) {
        float val = server.arg("offset").toFloat();
        // Validation: Allow offsets between -15.0 and +15.0 C
        if (val >= -15.0f && val <= 15.0f) {
            tempOffset = val;
            
            // Persist to EEPROM (magic byte 42 at addr 0, float at addr 1)
            EEPROM.put(0, (byte)42);
            EEPROM.put(1, tempOffset);
            EEPROM.commit();
            
            server.send(200, "application/json", "{\"status\":\"ok\",\"temp_offset_c\":" + String(tempOffset, 2) + "}");
            return;
        }
    }
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing or invalid 'offset' parameter (must be between -15.0 and 15.0)\"}");
}

// --- Relay Synchronization ---
void syncWithRelay() {
    if (WiFi.status() != WL_CONNECTED || !state.lastReadSuccess) return;

    state.relaySyncAttemptCount++;
    WiFiClient client;
    HTTPClient http;
    
    if (http.begin(client, RELAY_URL)) {
        http.addHeader("Content-Type", "application/json");
        
        JsonDocument doc;
        doc["indoorTemp"] = state.temp;
        doc["indoorHum"] = state.hum;
        doc["rawTemp"] = state.rawTemp;
        
        // Pass diagnostics to the boiler!
        doc["sensorRSSI"] = WiFi.RSSI();
        doc["sensorHeap"] = ESP.getFreeHeap();
        doc["sensorUptime"] = millis() / 1000;
        doc["sensorFwVersion"] = FIRMWARE_VERSION;
        doc["dhtFailRate"] = (state.dhtReadAttemptCount > 0) ? ((float)state.dhtReadFailCount / state.dhtReadAttemptCount * 100.0f) : 0.0f;

        String payload;
        serializeJson(doc, payload);

        int httpCode = http.POST(payload);
        if (httpCode <= 0 || httpCode != 200) {
            state.relaySyncFailCount++;
        }
        http.end();
    } else {
        state.relaySyncFailCount++;
    }
}

// --- Sinric Pro Synchronization ---
bool syncWithCloud() {
    if (!state.lastReadSuccess) return false;
    state.cloudSyncAttemptCount++;
    SinricProTemperaturesensor &mySensor = SinricPro[SENSOR_ID];
    bool ok = mySensor.sendTemperatureEvent(state.temp, state.hum);
    if (!ok) {
        state.cloudSyncFailCount++;
    }
    return ok;
}

void setup() {
    Serial.begin(BAUD_RATE);
    dht.begin();
    
    // Initialize EEPROM and read saved offset using a magic validation byte
    EEPROM.begin(32);
    byte magic;
    EEPROM.get(0, magic);
    if (magic == 42) {
        float storedOffset;
        EEPROM.get(1, storedOffset);
        if (!isnan(storedOffset) && storedOffset >= -15.0f && storedOffset <= 15.0f) {
            tempOffset = storedOffset;
        }
    } else {
        // EEPROM is uninitialized! Write signature and set default offset to -1.0C
        tempOffset = -1.0f;
        EEPROM.put(0, (byte)42);
        EEPROM.put(1, tempOffset);
        EEPROM.commit();
    }

    // DHT11 needs ~2 seconds to stabilize on boot
    delay(2000); 
    state.hum = dht.readHumidity();
    float raw_t = dht.readTemperature();
    state.rawTemp = isnan(raw_t) ? 0.0f : raw_t;
    state.temp = isnan(raw_t) ? 0.0f : (raw_t + tempOffset);
    state.lastReadSuccess = (!isnan(state.hum) && !isnan(raw_t));
    state.lastDHTRead = millis();

    // Enable Hardware Watchdog (8 seconds timeout)
    ESP.wdtEnable(8000);

    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_LIGHT_SLEEP); // Power optimization
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        ESP.wdtFeed(); // Feed watchdog during blocking WiFi connection
    }

    // mDNS Setup
    if (MDNS.begin("sensor")) {
        MDNS.addService("http", "tcp", 80);
    }

    // SinricPro Setup
    SinricPro.begin(APP_KEY, APP_SECRET);

    // WebServer Setup
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/set", handleSetOffset); // Support setting offset via /api/set?offset=-1.5
    
    // Register the Web OTA Updater (Registers the /update route)
    httpUpdater.setup(&server);
    
    server.begin();
}

void loop() {
    ESP.wdtFeed(); // Feed the watchdog timer
    
    MDNS.update();
    SinricPro.handle();
    server.handleClient();

    // WiFi Connection Disconnect Tracking
    static bool wasConnected = false;
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    if (!isConnected && wasConnected) {
        state.wifiDisconnectCount++;
    }
    wasConnected = isConnected;

    unsigned long now = millis();

    // 1. Read Sensor
    if (now - state.lastDHTRead > currentSensorPollMs) {
        state.lastDHTRead = now;
        state.dhtReadAttemptCount++;
        float h = dht.readHumidity();
        float raw_t = dht.readTemperature();
        
        if (!isnan(h) && !isnan(raw_t)) {
            float t = raw_t + tempOffset;
            // Dynamic Poll Frequency Adjust
            if (abs(t - state.temp) >= 0.2f) {
                currentSensorPollMs = 2000; // Rapid shift detected, poll fast (2s)
            } else {
                currentSensorPollMs = 10000; // Stable, back to slow poll (10s)
            }
            
            state.rawTemp = raw_t;
            state.temp = t;
            state.hum = h;
            state.lastReadSuccess = true;
        } else {
            state.dhtReadFailCount++;
            state.lastReadSuccess = false;
        }
    }

    // 2. Sync Relay
    if (now - state.lastRelaySync > RELAY_SYNC_MS || state.lastRelaySync == 0) {
        state.lastRelaySync = now;
        syncWithRelay();
    }

    // 3. Sync Cloud
    if (now - state.lastCloudSync > CLOUD_SYNC_MS || state.lastCloudSync == 0) {
        if (state.lastCloudSync == 0) {
            if (syncWithCloud()) state.lastCloudSync = now;
            else state.lastCloudSync = now - CLOUD_SYNC_MS + 5000; // retry in 5s
        } else {
            state.lastCloudSync = now;
            syncWithCloud();
        }
    }

    delay(10); // Required to yield to Light Sleep mode
}
