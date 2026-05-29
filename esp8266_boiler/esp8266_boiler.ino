/*
 * esp8266_boiler.ino
 * 
 * Production-Ready Boiler Firmware (v6.0.0 - Advanced Efficiency)
 * -----------------------------------------
 * Features:
 * - Uptime Kuma Dark Web UI Dashboard
 * - Feels-Like (Apparent Temperature) Control Logic
 * - Time-Proportional Control (PWM over 20 min cycles)
 * - Viessmann Vitopend 100 Gas Usage Tracking (2.6 m3/hr)
 * - Software Open Window Detection (10 min suspend on >0.5C drop in 5 mins)
 * - NTP Sleep Setback (Target -2C between 01:00 and 06:00)
 * - Hardware Watchdog Enabled
 * - (OTA Removed for Maximum Memory)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#define FIRMWARE_VERSION "1.0"

#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPUpdateServer.h>
#include "SinricPro.h"
#include "SinricProThermostat.h"
#include "dashboard.h"

// --- Hardcoded Credentials ---
#define WIFI_SSID         "Don't Connect to Strangers"
#define WIFI_PASS         "ESTyukselEST3118889"
#define APP_KEY           "1095cf7a-6478-4e37-8eb4-f7d471cdda88"
#define APP_SECRET        "4abb2e5e-a64f-4f3f-b189-2d5878564fd6-4ea6537e-9fa7-412c-a5d1-6de2a2459ad3"
#define THERMOSTAT_ID     "6a18fb45baa50bf9bf416c52"
#define WEATHER_API_KEY   "2d466f8a030d9622255b96a4243b4d3b"
#define LATITUDE          "40.992422" // Trabzon
#define LONGITUDE         "39.781729"

// --- Hardware & Timing Constants ---
#define BAUD_RATE         9600
#define SENSOR_TIMEOUT_MS 600000 // 10 Minutes (Relaxed)
#define WEATHER_SYNC_MS   900000 // 15 Minutes
#define MIN_ON_TIME_MS    180000 // 3 Minutes
#define MIN_OFF_TIME_MS   300000 // 5 Minutes
#define PWM_PERIOD_MS     1200000 // 20 Minutes

#define GAS_RATE_M3_HR    2.6f   // Viessmann Vitopend 100 (24kW) max consumption

const byte REL_ON[]  = {0xA0, 0x01, 0x01, 0xA2};
const byte REL_OFF[] = {0xA0, 0x01, 0x00, 0xA1};

// --- Global Diagnostic Counters ---
uint32_t boilerWifiDisconnectCount = 0;
uint32_t weatherFetchAttemptCount = 0;
uint32_t weatherFetchFailCount = 0;
uint32_t relaySwitchCount = 0;
uint32_t sensorTimeoutCount = 0;
uint32_t openWindowTriggers = 0;

// --- Remote Sensor State (Stored Diagnostics) ---
struct RemoteSensorDiagnostics {
    String ip = "0.0.0.0";
    int rssi = 0;
    uint32_t freeHeap = 0;
    float dhtFailRate = 0.0f;
    unsigned long uptime = 0;
    unsigned long lastReceivedMs = 0;
    String firmwareVersion = "--";
} sensorDiag;

// --- Global Configuration ---
struct Config {
    float targetTemp = 22.0f;
    int mode = 1; // 1 = HEAT, 2 = ECO, 0 = OFF
} config;

// --- Open Window Detection History ---
#define HISTORY_SIZE 5
float tempHistory[HISTORY_SIZE] = {0};
int historyIdx = 0;
unsigned long lastHistoryUpdate = 0;

// --- Controller State ---
struct BoilerController {
    float indoorTemp = 20.0f;
    float indoorHum  = 45.0f;
    float apparentTemp = 20.0f;
    float outdoorTemp = 10.0f;
    bool relayActive = false;
    String decisionReason = "Initializing";
    
    unsigned long lastSensorUpdate = 0;
    unsigned long lastWeatherUpdate = 0;
    unsigned long lastRelayChange = 0;
    float currentHysteresisMinus = 0.4f;
    
    // Gas Tracking & Weekly Reports
    unsigned long todayGasSeconds = 0;
    unsigned long weeklyGasSeconds[7] = {0}; // 7-day rolling history
    int currentDayOfWeek = 0;                // 0 to 6
    unsigned long totalUptimeSeconds = 0;
    unsigned long lastMillis = 0;
    unsigned long lastDayRolloverMs = 0;

    // Thermal Mass / Time-to-Target
    float startHeatingTemp = 0.0f;
    unsigned long startHeatingTime = 0;
    float heatingRatePerMin = 0.02f; // Default guess: 0.02 C per min

    // Window / Suspend
    unsigned long suspendUntil = 0;

    // PWM
    unsigned long pwmCycleStart = 0;

    bool updateRelay(bool on, bool force = false) {
        if (on == relayActive) return true;

        unsigned long timeSinceChange = millis() - lastRelayChange;
        if (!force) {
            if (on && !relayActive && timeSinceChange < MIN_OFF_TIME_MS && lastRelayChange != 0) {
                return false;
            }
            if (!on && relayActive && timeSinceChange < MIN_ON_TIME_MS && lastRelayChange != 0) {
                return false;
            }
        }

        if (on && !relayActive) {
            startHeatingTime = millis();
            startHeatingTemp = indoorTemp;
        }

        Serial.write(on ? REL_ON : REL_OFF, 4);
        Serial.flush();
        relayActive = on;
        lastRelayChange = millis();
        relaySwitchCount++;
        return true;
    }
} boiler;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// --- Heat Index Formula ---
float calculateApparentTemp(float t, float rh) {
    // Steadman's Apparent Temperature / Heat Index
    float c1 = -8.78469475556, c2 = 1.61139411, c3 = 2.33854883889;
    float c4 = -0.14611605, c5 = -0.012308094, c6 = -0.0164248277778;
    float c7 = 0.002211732, c8 = 0.00072546, c9 = -0.000003582;
    return c1 + c2*t + c3*rh + c4*t*rh + c5*t*t + c6*rh*rh + c7*t*t*rh + c8*t*rh*rh + c9*t*t*rh*rh;
}

// --- Weather Integration (Dynamic Hysteresis) ---
void fetchWeather() {
    WiFiClient client;
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(LATITUDE) + 
                 "&lon=" + String(LONGITUDE) + "&appid=" + String(WEATHER_API_KEY) + "&units=metric";
    
    weatherFetchAttemptCount++;
    if (http.begin(client, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            deserializeJson(doc, payload);
            boiler.outdoorTemp = doc["main"]["temp"];
            boiler.lastWeatherUpdate = millis();

            // Calculate Dynamic Hysteresis Minus for Trabzon Climate (0C to 20C -> 0.2 to 0.6)
            float h = 0.2f + ((boiler.outdoorTemp - 0.0f) / 20.0f) * (0.6f - 0.2f);
            if (h < 0.2f) h = 0.2f;
            if (h > 0.6f) h = 0.6f;
            boiler.currentHysteresisMinus = h;
        } else {
            weatherFetchFailCount++;
        }
        http.end();
    } else {
        weatherFetchFailCount++;
    }
}

// --- Web Server Handlers ---
void handleRoot() {
    server.send_P(200, "text/html", dashboard_html);
}

void handleStatus() {
    JsonDocument doc;
    
    // 1. System State
    JsonObject sys = doc.createNestedObject("system");
    sys["relay"] = boiler.relayActive ? "ON" : "OFF";
    sys["mode"] = (config.mode == 1) ? "HEAT" : (config.mode == 2) ? "ECO" : "OFF";
    sys["decision_reason"] = boiler.decisionReason;
    sys["uptime_seconds"] = boiler.totalUptimeSeconds;
    sys["last_relay_change_ms"] = boiler.lastRelayChange;

    // 2. Environmental Data
    JsonObject env = doc.createNestedObject("environment");
    env["indoor_temp_c"] = boiler.indoorTemp;
    env["indoor_hum_pct"] = boiler.indoorHum;
    env["apparent_temp_c"] = boiler.apparentTemp;
    env["outdoor_temp_c"] = boiler.outdoorTemp;
    env["target_temp_c"] = config.targetTemp;
    env["dynamic_hysteresis_minus"] = boiler.currentHysteresisMinus;

    // 3. Analytics
    JsonObject analytics = doc.createNestedObject("analytics");
    analytics["gas_used_m3_today"] = (boiler.todayGasSeconds / 3600.0f) * GAS_RATE_M3_HR;
    analytics["heating_minutes_today"] = boiler.todayGasSeconds / 60;
    
    // Calculate Weekly Total
    unsigned long totalWeeklySeconds = boiler.todayGasSeconds;
    for(int i=0; i<7; i++) totalWeeklySeconds += boiler.weeklyGasSeconds[i];
    analytics["gas_used_m3_this_week"] = (totalWeeklySeconds / 3600.0f) * GAS_RATE_M3_HR;
    
    float minsToTarget = 0;
    if (config.mode == 1 && boiler.indoorTemp < config.targetTemp) {
        minsToTarget = (config.targetTemp - boiler.indoorTemp) / boiler.heatingRatePerMin;
    }
    analytics["estimated_mins_to_target"] = minsToTarget > 0 ? (int)minsToTarget : 0;
    analytics["heating_rate_c_per_min"] = boiler.heatingRatePerMin;

    // 4. Network Info
    JsonObject net = doc.createNestedObject("network");
    net["ssid"] = WiFi.SSID();
    net["ip"] = WiFi.localIP().toString();
    net["rssi_dbm"] = WiFi.RSSI();
    net["mac"] = WiFi.macAddress();

    // 5. Boiler Hardware Info
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

    // 6. Boiler Diagnostics
    JsonObject diag = doc.createNestedObject("diagnostics");
    diag["wifi_disconnect_count"] = boilerWifiDisconnectCount;
    diag["relay_switch_count"] = relaySwitchCount;
    diag["open_window_triggers"] = openWindowTriggers;
    diag["sensor_safety_timeouts"] = sensorTimeoutCount;
    diag["weather_fetch_attempts"] = weatherFetchAttemptCount;
    diag["weather_fetch_fails"] = weatherFetchFailCount;
    diag["weather_fail_rate_pct"] = (weatherFetchAttemptCount > 0) ? ((float)weatherFetchFailCount / weatherFetchAttemptCount * 100.0f) : 0.0f;

    // 7. Remote Sensor Info (Forwarded)
    JsonObject sens = doc.createNestedObject("sensor_health");
    if (boiler.lastSensorUpdate == 0) {
        sens["connected"] = false;
    } else {
        sens["connected"] = (millis() - boiler.lastSensorUpdate < SENSOR_TIMEOUT_MS);
        sens["ip"] = sensorDiag.ip;
        sens["rssi_dbm"] = sensorDiag.rssi;
        sens["free_heap_bytes"] = sensorDiag.freeHeap;
        sens["dht_fail_rate_pct"] = sensorDiag.dhtFailRate;
        sens["uptime_seconds"] = sensorDiag.uptime;
        sens["firmware_version"] = sensorDiag.firmwareVersion;
        sens["last_seen_seconds_ago"] = (millis() - sensorDiag.lastReceivedMs) / 1000;
    }

    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void handleSet() {
    SinricProThermostat &myT = SinricPro[THERMOSTAT_ID];
    if (server.hasArg("target")) {
        config.targetTemp = server.arg("target").toFloat();
        myT.sendTargetTemperatureEvent(config.targetTemp);
    }
    if (server.hasArg("mode")) {
        String m = server.arg("mode");
        String cloudMode = m;
        if (m == "HEAT") config.mode = 1;
        else if (m == "ECO") {
            config.mode = 2;
            cloudMode = "COOL"; // Map local ECO to Sinric Pro standard COOL
        }
        else if (m == "OFF") config.mode = 0;
        myT.sendThermostatModeEvent(cloudMode);
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleSensor() {
    if (!server.hasArg("plain")) return server.send(400);
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) return server.send(400);

    boiler.indoorTemp = doc["indoorTemp"] | doc["temperature"] | 20.0f;
    boiler.indoorHum = doc["indoorHum"] | doc["humidity"] | 45.0f;
    boiler.apparentTemp = calculateApparentTemp(boiler.indoorTemp, boiler.indoorHum);
    boiler.lastSensorUpdate = millis();

    // Parse incoming sensor diagnostics!
    sensorDiag.ip = server.client().remoteIP().toString();
    sensorDiag.rssi = doc["sensorRSSI"] | 0;
    sensorDiag.freeHeap = doc["sensorHeap"] | 0;
    sensorDiag.dhtFailRate = doc["dhtFailRate"] | 0.0f;
    sensorDiag.uptime = doc["sensorUptime"] | 0;
    if (doc.containsKey("sensorFwVersion")) {
        sensorDiag.firmwareVersion = doc["sensorFwVersion"].as<String>();
    }
    sensorDiag.lastReceivedMs = millis();

    SinricProThermostat &myT = SinricPro[THERMOSTAT_ID];
    myT.sendTemperatureEvent(boiler.apparentTemp, boiler.indoorHum);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// --- Sinric Pro Callbacks ---
bool onPowerState(const String &deviceId, bool &state) {
    config.mode = state ? 1 : 0;
    
    // Explicitly synchronize thermostat mode on Google Home when power changes
    SinricProThermostat &myT = SinricPro[THERMOSTAT_ID];
    myT.sendThermostatModeEvent(state ? "HEAT" : "OFF");
    
    return true; 
}

bool onTargetTemperature(const String &deviceId, float &temp) {
    config.targetTemp = temp;
    return true;
}

bool onAdjustTargetTemperature(const String &deviceId, float &temperatureDelta) {
    config.targetTemp += temperatureDelta;
    temperatureDelta = config.targetTemp;
    return true;
}

bool onThermostatMode(const String &deviceId, String &mode) {
    SinricProThermostat &myT = SinricPro[THERMOSTAT_ID];
    if (mode == "HEAT") {
        config.mode = 1;
        myT.sendPowerStateEvent(true);
    }
    else if (mode == "ECO" || mode == "COOL" || mode == "AUTO") {
        config.mode = 2;
        myT.sendPowerStateEvent(true);
        // Normalize custom modes (COOL, AUTO) to Sinric's expected COOL (ECO) state representation
        myT.sendThermostatModeEvent("COOL");
    }
    else if (mode == "OFF") {
        config.mode = 0;
        myT.sendPowerStateEvent(false);
    }
    return true;
}

void setup() {
    Serial.begin(BAUD_RATE);

    // Hardware Watchdog
    ESP.wdtEnable(8000);

    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_LIGHT_SLEEP); // Power optimization
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        ESP.wdtFeed();
    }

    // mDNS Setup
    if (MDNS.begin("boiler")) {
        MDNS.addService("http", "tcp", 80);
    }

    // SinricPro Initialization
    SinricProThermostat &myT = SinricPro[THERMOSTAT_ID];
    myT.onPowerState(onPowerState);
    myT.onTargetTemperature(onTargetTemperature);
    myT.onAdjustTargetTemperature(onAdjustTargetTemperature);
    myT.onThermostatMode(onThermostatMode);

    SinricPro.restoreDeviceStates(true);
    SinricPro.begin(APP_KEY, APP_SECRET);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/set", HTTP_POST, handleSet);
    server.on("/sensor", HTTP_POST, handleSensor);
    
    // Register the Web OTA Updater (Registers the /update route)
    httpUpdater.setup(&server);
    
    server.begin();

    fetchWeather();
    boiler.lastMillis = millis();
    boiler.lastDayRolloverMs = millis();
}

void loop() {
    ESP.wdtFeed();
    MDNS.update();
    SinricPro.handle();
    server.handleClient();

    // WiFi Connection Disconnect Tracking
    static bool wasConnected = false;
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    if (!isConnected && wasConnected) {
        boilerWifiDisconnectCount++;
    }
    wasConnected = isConnected;

    unsigned long now = millis();
    unsigned long delta = now - boiler.lastMillis;
    boiler.lastMillis = now;
    
    boiler.totalUptimeSeconds += (delta / 1000);
    
    // Day Rollover logic (every 24 hours)
    if (now - boiler.lastDayRolloverMs > 86400000) {
        boiler.weeklyGasSeconds[boiler.currentDayOfWeek] = boiler.todayGasSeconds;
        boiler.currentDayOfWeek = (boiler.currentDayOfWeek + 1) % 7;
        boiler.todayGasSeconds = 0; // Reset for the new day
        boiler.lastDayRolloverMs = now;
    }

    if (boiler.relayActive) {
        boiler.todayGasSeconds += (delta / 1000);
        
        // Calculate Thermal Mass Rate (Wait 5 minutes for boiler to warm up before measuring)
        unsigned long elapsedHeatingMs = now - boiler.startHeatingTime;
        if (elapsedHeatingMs > 300000) { 
            float tempRise = boiler.indoorTemp - boiler.startHeatingTemp;
            if (tempRise > 0.1f) {
                boiler.heatingRatePerMin = tempRise / (elapsedHeatingMs / 60000.0f);
            }
        }
    }

    // 1. Weather Sync
    if (now - boiler.lastWeatherUpdate > WEATHER_SYNC_MS || boiler.lastWeatherUpdate == 0) {
        fetchWeather();
    }

    // 2. Open Window Detection (History every 1 minute)
    if (now - lastHistoryUpdate > 60000 && boiler.lastSensorUpdate != 0) {
        tempHistory[historyIdx] = boiler.indoorTemp;
        historyIdx = (historyIdx + 1) % HISTORY_SIZE;
        lastHistoryUpdate = now;

        // Check if oldest to newest dropped > 0.5C
        float oldest = tempHistory[historyIdx]; // The one we are about to overwrite next is the oldest
        float newest = boiler.indoorTemp;
        if (oldest > 0 && (oldest - newest) > 0.5f) {
            boiler.suspendUntil = now + 600000; // Suspend 10 mins
            openWindowTriggers++;
            // Clear history so we don't keep triggering
            for(int i=0; i<HISTORY_SIZE; i++) tempHistory[i] = 0;
        }
    }

    // 3. Control Logic
    if (now - boiler.lastSensorUpdate < SENSOR_TIMEOUT_MS && boiler.lastSensorUpdate != 0) {
        
        float effectiveTarget = config.targetTemp;

        if (now < boiler.suspendUntil) {
            boiler.updateRelay(false, true); // Force OFF
            boiler.decisionReason = "SUSPENDED (Open Window Draft Detected)";
        }
        else if (config.mode == 1 || config.mode == 2) { 
            // PWM Control Logic over 20 Min Period
            if (now - boiler.pwmCycleStart > PWM_PERIOD_MS) boiler.pwmCycleStart = now;
            unsigned long timeInCycle = now - boiler.pwmCycleStart;

            float activeHysteresis = boiler.currentHysteresisMinus;
            float maxDutyCycle = 1.0f;
            float derivativeCutoffLimit = 0.2f;

            if (config.mode == 2) {
                // ECO Mode overrides
                effectiveTarget -= 1.0f; // 1C Setback
                activeHysteresis = 1.0f; // Wider hysteresis band
                maxDutyCycle = 0.5f; // Cap duty cycle to 50%
                derivativeCutoffLimit = 0.5f; // Shut off earlier when rising
            }

            float error = effectiveTarget - boiler.apparentTemp;
            
            if (error >= activeHysteresis) {
                // Far below target
                if (1.0f <= maxDutyCycle) {
                    bool applied = boiler.updateRelay(true);
                    boiler.decisionReason = applied ? ((config.mode == 2 ? String("ECO ") : String("")) + "Heating 100%") : "Waiting Min OFF Time";
                } else {
                    unsigned long onTimeLimit = PWM_PERIOD_MS * maxDutyCycle;
                    if (timeInCycle < onTimeLimit) {
                        bool applied = boiler.updateRelay(true);
                        boiler.decisionReason = applied ? "ECO Capped Heating (50%)" : "Waiting Min OFF Time";
                    } else {
                        bool applied = boiler.updateRelay(false);
                        boiler.decisionReason = applied ? "ECO Duty Cap Reached" : "Waiting Min ON Time";
                    }
                }
            } 
            else if (error <= 0) {
                // Reached or exceeded target, 0% duty cycle
                bool applied = boiler.updateRelay(false);
                boiler.decisionReason = applied ? "Target Reached" : "Target Reached (Min ON Delay)";
            } 
            else {
                // Proportional Control (Duty cycle between 0% and 100%)
                if (boiler.heatingRatePerMin > 0.05f && error < derivativeCutoffLimit) {
                    bool applied = boiler.updateRelay(false);
                    boiler.decisionReason = applied ? "Derivative Early Cutoff (Coasting)" : "Coasting (Min ON Delay)";
                } else {
                    float dutyCycle = error / activeHysteresis;
                    if (dutyCycle > maxDutyCycle) dutyCycle = maxDutyCycle;
                    unsigned long onTimeLimit = PWM_PERIOD_MS * dutyCycle;
                    
                    if (timeInCycle < onTimeLimit) {
                        bool applied = boiler.updateRelay(true);
                        boiler.decisionReason = applied ? ((config.mode == 2 ? String("ECO ") : String("")) + "PWM Heating (" + String(dutyCycle*100, 0) + "%)") : "Waiting Min OFF Time";
                    } else {
                        bool applied = boiler.updateRelay(false);
                        boiler.decisionReason = applied ? ((config.mode == 2 ? String("ECO ") : String("")) + "PWM Idling (" + String(dutyCycle*100, 0) + "%)") : "Waiting Min ON Time";
                    }
                }
            }
        } 
        else {
            boiler.updateRelay(false, true); // Force OFF immediately
            boiler.decisionReason = "System Manual OFF / Mode OFF";
        }
    } else {
        if (boiler.lastSensorUpdate == 0) {
            boiler.updateRelay(false, true);
            boiler.decisionReason = "Waiting for first sensor reading";
        } else {
            static unsigned long lastTimeoutReport = 0;
            if (boiler.relayActive || boiler.decisionReason != "Safety OFF: Sensor Timeout (>10 mins)") {
                if (now - lastTimeoutReport > 60000) { // Limit to once per minute to avoid spamming
                    sensorTimeoutCount++;
                    lastTimeoutReport = now;
                }
            }
            boiler.updateRelay(false, true);
            boiler.decisionReason = "Safety OFF: Sensor Timeout (>10 mins)";
        }
    }

    delay(10);
}
