/*
 * esp8266_boiler.ino - ThermostatESP boiler controller
 * -----------------------------------------------------
 * Switches the boiler (LC-Tech style serial relay board) from the room temperature that
 * the sensor node pushes every 30 s, and serves a local dashboard plus a Sinric Pro
 * thermostat for Google Home / Alexa.
 *
 * Features:
 * - Controls on the measured temperature of the coldest room (feels-like is display only)
 * - Learned thermal model of the house (heat-up rate + heat loss), updated every hour
 * - Weather-aware control: model feed-forward from outdoor temp + 3 h forecast
 * - Time-to-target by simulating the learned model with the real control law (incl. ECO)
 * - NTP clock (Europe/Istanbul): gas statistics roll over at local midnight
 * - Gas estimate with optional calibration against the real gas meter
 * - Boiler fault + sensor offline alerts (dashboard + Sinric Pro push notifications)
 * - Settings, gas history and the learned model survive reboots (EEPROM)
 * - Google Home sees HEAT / OFF only; ECO is a local dashboard mode
 * - Relay forced OFF on boot and re-sent every minute; hardware watchdog
 *
 * README.md explains the control logic in detail.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#define FIRMWARE_VERSION "1.0"

#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <stddef.h>
#include <time.h>
#include <TZ.h>
#include <coredecls.h>   // settimeofday_cb
#include "SinricPro.h"
#include "SinricProThermostat.h"
#include "dashboard.h"

// --- Hardcoded Credentials ---
#define WIFI_SSID         "Dont Connect to Strangers"
#define WIFI_PASS         "ESTyukselEST3118889"
#define APP_KEY           "1095cf7a-6478-4e37-8eb4-f7d471cdda88"
#define APP_SECRET        "4abb2e5e-a64f-4f3f-b189-2d5878564fd6-4ea6537e-9fa7-412c-a5d1-6de2a2459ad3"
#define THERMOSTAT_ID     "6a18fb45baa50bf9bf416c52"
#define WEATHER_API_KEY   "2d466f8a030d9622255b96a4243b4d3b"
#define LATITUDE          "40.992422" // Trabzon
#define LONGITUDE         "39.781729"

// ============================ Configuration ============================

// --- Clock (NTP) ---
#define TIMEZONE            TZ_Europe_Istanbul
#define NTP_SERVER_1        "pool.ntp.org"
#define NTP_SERVER_2        "time.google.com"
#define NTP_SERVER_3        "time.cloudflare.com"

// --- Push notifications (Sinric Pro app): true = Turkish, false = English ---
#define NOTIFY_IN_TURKISH   false

// --- Hardware & timing ---
#define BAUD_RATE               9600        // LC-Tech relay board UART
#define SENSOR_TIMEOUT_MS       600000UL    // 10 min without sensor data -> heating OFF
#define WEATHER_SYNC_MS         900000UL    // 15 min
#define WEATHER_RETRY_MS        120000UL    // 2 min after a failed fetch
#define WEATHER_FRESH_MS        3600000UL   // older weather is not used for learning
#define FORECAST_FRESH_MS       10800000UL  // forecast older than 3 h is ignored
#define MIN_ON_TIME_MS          180000UL    // boiler protection: min burner run
#define MIN_OFF_TIME_MS         300000UL    // boiler protection: min pause
#define PWM_PERIOD_MS           1200000UL   // 20 min control cycle
#define RELAY_REASSERT_MS       60000UL     // re-send relay state every minute
#define CLOUD_TEMP_MIN_MS       65000UL     // Sinric drops temperature events < 60 s apart
#define CLOUD_TEMP_KEEPALIVE_MS 600000UL
#define CLOUD_STATE_GAP_MS      1100UL      // Sinric allows 1 state event per type per second
#define NOTIFY_GAP_MS           65000UL     // Sinric allows 1 push notification per minute
#define SETTINGS_SAVE_DELAY_MS  10000UL     // debounce settings writes
#define STATS_SAVE_MS           1800000UL   // save gas/model every 30 min (<= 48 writes/day)

// --- Targets & control ---
#define MIN_TARGET_C        5.0f
#define MAX_TARGET_C        30.0f
#define DEFAULT_TARGET_C    21.0f
#define BAND_HEAT_C         0.5f    // proportional band: burner share changes 100% over this
#define BAND_ECO_C          1.0f
#define ECO_SETBACK_C       1.0f
#define ECO_MAX_DUTY        0.5f
#define CONTROL_SMOOTH_S    180.0f  // control uses a ~3 min smoothed temperature (fewer relay clicks)
#define TARGET_TOLERANCE_C  0.3f    // "reached" for the time-to-target estimate

// --- Learned thermal model ---
#define RADIATOR_LAG_MIN      15.0f  // time constant of radiator warm-up / cool-down
#define MODEL_WINDOW_MIN      60     // one learning sample per hour
#define MODEL_FORGET          0.98f  // per sample -> roughly 2 days of memory
#define DEFAULT_HEAT_RATE     2.5f   // °C/h with the burner fully on, before losses
#define DEFAULT_LOSS_K        0.08f  // 1/h: share of the indoor-outdoor gap lost per hour
#define MODEL_PRIOR_WINDOWS   3.0f   // how strongly the defaults are trusted
#define MODEL_LEARNED_WINDOWS 12.0f  // "learned" after ~12 hours of usable data

// --- Gas ---
#define GAS_RATE_M3_HR        2.6f   // Viessmann Vitopend 100 (24 kW) at full load
#define GAS_CALIB_MIN_BURN_H  5.0f   // burner hours needed between two meter readings

// --- Fault detection ---
#define FAULT_CHECK_MIN          60     // check after this many minutes of continuous burning
#define FAULT_MIN_EXPECTED_RISE  0.4f   // only judge when the model expects at least this rise
#define FAULT_RENOTIFY_MS        21600000UL // remind every 6 h while the fault persists

#define TREND_LEN 30                     // minutes used for the temperature trend

const byte REL_ON[]  = {0xA0, 0x01, 0x01, 0xA2};
const byte REL_OFF[] = {0xA0, 0x01, 0x00, 0xA1};

const float Q_ALPHA = 1.0f - expf(-1.0f / RADIATOR_LAG_MIN); // per-minute radiator filter

// ============================ Persistent data ============================

#define PERSIST_MAGIC   0x54534531UL // "TSE1"
#define PERSIST_VERSION 1

struct PersistData {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    // Settings
    float    targetTemp;
    uint8_t  mode;          // 0 = OFF, 1 = HEAT, 2 = ECO
    uint8_t  lastOnMode;    // mode restored by "turn on" (1 or 2)
    uint8_t  reserved[2];
    // Gas statistics
    int32_t  todayDay;      // local calendar day (days since 1970-01-01), 0 = unknown
    uint32_t todayBurnSec;
    uint32_t historyBurnSec[7]; // [0] = yesterday ... [6] = 7 days ago
    uint32_t lifetimeBurnSec;
    float    gasFactor;     // meter calibration factor, 1.0 = uncalibrated
    uint32_t calibActive;
    double   calibMeterM3;  // meter reading when the current calibration started
    uint32_t calibBurnSec;  // lifetimeBurnSec at that moment
    // Learned model: exponentially weighted least-squares sums
    float    Suu, Sud, Sdd, Suy, Sdy, Sn;
    uint32_t crc;
};
static_assert(sizeof(PersistData) <= 512, "PersistData too large for EEPROM area");

PersistData P;
bool persistLoadedFromFlash = false;

// ============================ Runtime state ============================

enum TttState : uint8_t { TTT_UNKNOWN, TTT_OFF, TTT_REACHED, TTT_HEATING, TTT_UNREACHABLE };

struct RemoteSensorDiagnostics {
    String ip = "0.0.0.0";
    int rssi = 0;
    uint32_t freeHeap = 0;
    float dhtFailRate = 0.0f;
    unsigned long uptime = 0;
    unsigned long lastReceivedMs = 0;
    String firmwareVersion = "--";
} sensorDiag;

struct State {
    // Room
    float indoorTemp = 20.0f;
    float indoorHum = 45.0f;
    float ctlTemp = NAN;                  // smoothed indoor temperature used for control
    unsigned long lastSensorUpdate = 0;   // 0 = never received

    // Weather
    float outdoorTemp = 10.0f;
    bool weatherEverOk = false;
    bool lastWeatherOk = false;
    unsigned long lastWeatherUpdate = 0;
    unsigned long lastWeatherAttempt = 0;
    long weatherObsEpoch = 0;
    float forecast3h = NAN;
    unsigned long lastForecastUpdate = 0;

    // Relay & control
    bool relayActive = false;
    unsigned long lastRelayChange = 0;
    unsigned long lastRelayReassert = 0;
    unsigned long pwmCycleStart = 0;
    float duty = 0.0f;
    float feedForward = 0.0f;
    const char *reason = "starting";

    // Learned model
    float heatRate = DEFAULT_HEAT_RATE;   // a [°C/h]
    float lossK = DEFAULT_LOSS_K;         // k [1/h]
    float heatQ = 0.0f;                   // filtered burner output 0..1 (radiator lag)
    unsigned long relayOnMsThisMinute = 0;
    unsigned long lastMinuteTick = 0;

    // Current learning window (centred sums for a least-squares slope)
    int wN = 0;
    float wT0 = 0, wSt = 0, wStt = 0, wST = 0, wStT = 0, wSq = 0, wSd = 0;

    // Temperature trend (one sample per minute)
    float trendBuf[TREND_LEN];
    int trendIdx = 0;

    // Time to target
    TttState tttState = TTT_UNKNOWN;
    int tttMinutes = -1;
    bool tttDirty = true;

    // Fault detection (per continuous burn)
    bool burnTracking = false;
    int burnMinutes = 0;
    float burnStartTemp = 0, burnSimT = 0, burnSimQ = 0;
    bool faultActive = false;
    time_t faultSince = 0;
    float faultExpected = 0, faultActual = 0;
    unsigned long lastFaultNotifyMs = 0;
    bool sensorAlert = false;
    time_t sensorAlertSince = 0;

    // Persistence
    bool settingsDirty = false;
    unsigned long settingsDirtyAt = 0;
    bool statsDirty = false;
    unsigned long lastStatsSave = 0;

    // Cloud sync (local changes -> Sinric Pro)
    bool pendingTarget = false, pendingMode = false, pendingPower = false;
    unsigned long lastTargetEvt = 0, lastModeEvt = 0, lastPowerEvt = 0;
    bool forceTempReport = false;

    // Clock / uptime
    time_t lastNtpSync = 0;
    unsigned long uptimeSec = 0;
    unsigned long lastMillis = 0;
} S;

// Diagnostic counters
uint32_t boilerWifiDisconnectCount = 0;
uint32_t weatherFetchAttemptCount = 0;
uint32_t weatherFetchFailCount = 0;
uint32_t relaySwitchCount = 0;
uint32_t sensorTimeoutCount = 0;
uint32_t boilerFaultCount = 0;
uint32_t modelSamplesCount = 0;
uint32_t ntpSyncCount = 0;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
SinricProThermostat *thermo = nullptr;

// ============================ Small helpers ============================

float r1(float v) { return roundf(v * 10.0f) / 10.0f; }
float r2(float v) { return roundf(v * 100.0f) / 100.0f; }

float clampTarget(float t) {
    if (isnan(t)) return P.targetTemp;
    return constrain(t, MIN_TARGET_C, MAX_TARGET_C);
}

const char *modeName(uint8_t m) { return m == 1 ? "HEAT" : m == 2 ? "ECO" : "OFF"; }
// Google Home only knows HEAT and OFF. Local ECO is reported as HEAT.
const char *cloudMode() { return P.mode == 0 ? "OFF" : "HEAT"; }

float effectiveTarget() { return P.targetTemp - (P.mode == 2 ? ECO_SETBACK_C : 0.0f); }
float bandForMode()     { return P.mode == 2 ? BAND_ECO_C : BAND_HEAT_C; }
float maxDutyForMode()  { return P.mode == 2 ? ECO_MAX_DUTY : 1.0f; }

bool sensorFresh(unsigned long now) {
    return S.lastSensorUpdate != 0 && now - S.lastSensorUpdate < SENSOR_TIMEOUT_MS;
}

bool forecastFresh() {
    return !isnan(S.forecast3h) && millis() - S.lastForecastUpdate < FORECAST_FRESH_MS;
}

bool timeValid() { return time(nullptr) > 1700000000; }

// Days since 1970-01-01 for a civil date (H. Hinnant's algorithm)
int32_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int32_t localDayNumber() {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

float burnToM3(uint32_t sec) { return sec / 3600.0f * GAS_RATE_M3_HR * P.gasFactor; }

uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
    return ~crc;
}

// --- Feels-Like (DISPLAY ONLY - heating control uses the real temperature) ---
// Full US NWS heat index algorithm: Steadman's simple formula first, and the Rothfusz
// regression only when that result (averaged with T) reaches 80F / ~27C.
float computeFeelsLike(float tC, float rh) {
    float tF = tC * 1.8f + 32.0f;
    float hi = 0.5f * (tF + 61.0f + ((tF - 68.0f) * 1.2f) + (rh * 0.094f));
    if ((hi + tF) / 2.0f >= 80.0f) {
        hi = -42.379f + 2.04901523f * tF + 10.14333127f * rh
             - 0.22475541f * tF * rh - 0.00683783f * tF * tF
             - 0.05481717f * rh * rh + 0.00122874f * tF * tF * rh
             + 0.00085282f * tF * rh * rh - 0.00000199f * tF * tF * rh * rh;
        if (rh < 13.0f && tF >= 80.0f && tF <= 112.0f) {
            hi -= ((13.0f - rh) / 4.0f) * sqrtf((17.0f - fabsf(tF - 95.0f)) / 17.0f);
        } else if (rh > 85.0f && tF >= 80.0f && tF <= 87.0f) {
            hi += ((rh - 85.0f) / 10.0f) * ((87.0f - tF) / 5.0f);
        }
    }
    return (hi - 32.0f) / 1.8f;
}

// ============================ Persistence ============================

void setPersistDefaults() {
    memset(&P, 0, sizeof(P));
    P.targetTemp = DEFAULT_TARGET_C;
    P.mode = 1;
    P.lastOnMode = 1;
    P.gasFactor = 1.0f;
}

void loadPersist() {
    EEPROM.get(0, P);
    uint32_t c = crc32((const uint8_t *)&P, offsetof(PersistData, crc));
    if (P.magic != PERSIST_MAGIC || P.version != PERSIST_VERSION ||
        P.size != sizeof(PersistData) || P.crc != c) {
        setPersistDefaults();
        persistLoadedFromFlash = false;
        return;
    }
    // Sanitize anything that would break control
    P.targetTemp = isnan(P.targetTemp) ? DEFAULT_TARGET_C : constrain(P.targetTemp, MIN_TARGET_C, MAX_TARGET_C);
    if (P.mode > 2) P.mode = 1;
    if (P.lastOnMode != 1 && P.lastOnMode != 2) P.lastOnMode = 1;
    if (!(P.gasFactor >= 0.2f && P.gasFactor <= 2.0f)) P.gasFactor = 1.0f;
    persistLoadedFromFlash = true;
}

void savePersist() {
    P.magic = PERSIST_MAGIC;
    P.version = PERSIST_VERSION;
    P.size = sizeof(PersistData);
    P.crc = crc32((const uint8_t *)&P, offsetof(PersistData, crc));
    EEPROM.put(0, P);
    EEPROM.commit();
    S.settingsDirty = false;
    S.statsDirty = false;
    S.lastStatsSave = millis();
}

void markSettingsDirty() {
    S.settingsDirty = true;
    S.settingsDirtyAt = millis();
}

// ============================ Notifications ============================

#define NOTIFY_QUEUE_LEN 4
String notifyQueue[NOTIFY_QUEUE_LEN];
uint8_t notifyCount = 0;
unsigned long lastNotifyAttempt = 0;
bool notifyAttempted = false;

void queueNotification(const char *msg) {
    if (notifyCount == NOTIFY_QUEUE_LEN) {          // drop the oldest
        for (int i = 1; i < NOTIFY_QUEUE_LEN; i++) notifyQueue[i - 1] = notifyQueue[i];
        notifyCount--;
    }
    notifyQueue[notifyCount++] = msg;
}

void processNotifications(unsigned long now) {
    if (notifyCount == 0 || !SinricPro.isConnected()) return;
    if (notifyAttempted && now - lastNotifyAttempt < NOTIFY_GAP_MS) return;
    notifyAttempted = true;
    lastNotifyAttempt = now;
    if (thermo->sendPushNotification(notifyQueue[0])) {
        for (int i = 1; i < notifyCount; i++) notifyQueue[i - 1] = notifyQueue[i];
        notifyCount--;
        notifyQueue[notifyCount] = String();
    }
}

void notifyBoilerFault(float expected, float actual) {
    char msg[220];
    if (NOTIFY_IN_TURKISH) {
        snprintf(msg, sizeof msg, "Kombi ısıtmıyor olabilir: brülör %d dk açık, oda %+.1f °C değişti (beklenen %+.1f °C). Kombi ekranını, su basıncını ve gazı kontrol edin.",
                 FAULT_CHECK_MIN, actual, expected);
    } else {
        snprintf(msg, sizeof msg, "Boiler may not be heating: burner on for %d min, room changed %+.1f °C (expected %+.1f °C). Check the boiler display, water pressure and gas.",
                 FAULT_CHECK_MIN, actual, expected);
    }
    queueNotification(msg);
}

// ============================ Relay ============================

void writeRelay(bool on) {
    Serial.write(on ? REL_ON : REL_OFF, 4);
    Serial.flush();
    S.lastRelayReassert = millis();
}

void startBurnTracking() {
    S.burnTracking = true;
    S.burnMinutes = 0;
    S.burnStartTemp = S.indoorTemp;
    S.burnSimT = S.indoorTemp;
    S.burnSimQ = S.heatQ;
}

// Returns false when blocked by the minimum ON/OFF protection times.
bool updateRelay(bool on, bool force = false) {
    if (on == S.relayActive) return true;
    unsigned long since = millis() - S.lastRelayChange;
    if (!force && S.lastRelayChange != 0) {
        if (on && since < MIN_OFF_TIME_MS) return false;
        if (!on && since < MIN_ON_TIME_MS) return false;
    }
    writeRelay(on);
    S.relayActive = on;
    S.lastRelayChange = millis();
    relaySwitchCount++;
    if (on) startBurnTracking();
    else S.burnTracking = false;
    return true;
}

// ============================ Thermal model ============================
//
// First-order model of the room, in hours:
//     dT/dt = a * q  -  k * (T - Tout)
//   a = heat-up rate with the burner fully on [°C/h], k = heat-loss coefficient [1/h],
//   q = burner output filtered by the radiator lag (0..1).
// Every hour the measured slope of T is regressed on (q, T - Tout) with exponential
// forgetting and a weak prior toward the defaults (ridge), giving a and k.

void solveModel() {
    const float wa = MODEL_PRIOR_WINDOWS * 0.25f;   // typical q^2
    const float wk = MODEL_PRIOR_WINDOWS * 100.0f;  // typical (T - Tout)^2 (10 °C)
    float A11 = P.Suu + wa, A12 = -P.Sud, A22 = P.Sdd + wk;
    float b1 = P.Suy + wa * DEFAULT_HEAT_RATE;
    float b2 = -P.Sdy + wk * DEFAULT_LOSS_K;
    float det = A11 * A22 - A12 * A12;
    if (det <= 1e-6f) return;
    float a = (b1 * A22 - A12 * b2) / det;
    float k = (A11 * b2 - A12 * b1) / det;
    S.heatRate = constrain(a, 0.3f, 15.0f);
    S.lossK = constrain(k, 0.01f, 0.6f);
}

void resetWindow() {
    S.wN = 0;
    S.wT0 = S.wSt = S.wStt = S.wST = S.wStT = S.wSq = S.wSd = 0;
}

void learnFromWindow() {
    float n = S.wN;
    float den = n * S.wStt - S.wSt * S.wSt;
    if (den <= 0) return;
    float y = (n * S.wStT - S.wSt * S.wST) / den * 60.0f; // measured slope [°C/h]
    float u = S.wSq / n;                                  // mean radiator output
    float d = S.wSd / n;                                  // mean indoor - outdoor
    if (fabsf(y) > 6.0f || d < 2.0f) return;              // implausible, or too mild to learn
    const float lam = MODEL_FORGET;
    P.Suu = P.Suu * lam + u * u;
    P.Sud = P.Sud * lam + u * d;
    P.Sdd = P.Sdd * lam + d * d;
    P.Suy = P.Suy * lam + u * y;
    P.Sdy = P.Sdy * lam + d * y;
    P.Sn  = P.Sn  * lam + 1.0f;
    solveModel();
    modelSamplesCount++;
    S.statsDirty = true;
}

void updateLearningWindow(unsigned long now, bool fresh) {
    bool weatherFresh = S.weatherEverOk && now - S.lastWeatherUpdate < WEATHER_FRESH_MS;
    if (!fresh || !weatherFresh) { resetWindow(); return; }   // gap in data: start over
    if (S.wN == 0) S.wT0 = S.indoorTemp;
    float t = S.wN;
    float T = S.indoorTemp - S.wT0;
    S.wSt += t; S.wStt += t * t; S.wST += T; S.wStT += t * T;
    S.wSq += S.heatQ;
    S.wSd += S.indoorTemp - S.outdoorTemp;
    if (++S.wN >= MODEL_WINDOW_MIN) {
        learnFromWindow();
        resetWindow();
    }
}

// Outdoor temperature used for control: average of now and 3 h ahead, because the
// house reacts slowly. NAN when no weather has ever been received.
float outdoorForControl() {
    if (!S.weatherEverOk) return NAN;
    return forecastFresh() ? 0.5f * (S.outdoorTemp + S.forecast3h) : S.outdoorTemp;
}

// Burner share needed to just hold `target` at `tout` (weather feed-forward).
float feedForwardDuty(float target, float tout) {
    if (isnan(tout)) return 0.0f;
    return constrain(S.lossK * (target - tout) / S.heatRate, 0.0f, 1.0f);
}

// The control law, shared by the real controller and the time-to-target simulation.
float controlDuty(float tin, float q, float target, float tout, float band, float maxDuty, float *ffOut) {
    float ff = feedForwardDuty(target, tout);
    // Anticipation: heat stored in the radiators beyond what holding needs will keep
    // warming the room for about RADIATOR_LAG_MIN, so act on where the room is heading.
    float anticip = constrain(S.heatRate * (q - ff) * (RADIATOR_LAG_MIN / 60.0f), -0.3f, 0.8f);
    float duty = ff + (target - (tin + anticip)) / band;
    if (ffOut) *ffOut = ff;
    return constrain(duty, 0.0f, maxDuty);
}

float trendPerHour() {
    float n = 0, st = 0, stt = 0, sT = 0, stT = 0, T0 = NAN;
    for (int i = 0; i < TREND_LEN; i++) {
        float T = S.trendBuf[(S.trendIdx + i) % TREND_LEN];   // oldest first
        if (isnan(T)) continue;
        if (isnan(T0)) T0 = T;
        float x = i, y = T - T0;
        n++; st += x; stt += x * x; sT += y; stT += x * y;
    }
    if (n < 10) return NAN;
    float den = n * stt - st * st;
    if (den <= 0) return NAN;
    return (n * stT - st * sT) / den * 60.0f;
}

// Simulates the learned model minute by minute with the same control law (and the
// ECO setback / duty cap, min-off wait and forecast) until the target is reached.
void computeTimeToTarget(unsigned long now) {
    S.tttMinutes = -1;
    if (P.mode == 0) { S.tttState = TTT_OFF; return; }
    if (!sensorFresh(now)) { S.tttState = TTT_UNKNOWN; return; }
    float target = effectiveTarget();
    if (S.ctlTemp >= target - TARGET_TOLERANCE_C) { S.tttState = TTT_REACHED; S.tttMinutes = 0; return; }

    float band = bandForMode(), maxDuty = maxDutyForMode();
    float toutCtl = outdoorForControl();
    float toutNow = S.weatherEverOk ? S.outdoorTemp : 10.0f;
    float toutLater = forecastFresh() ? S.forecast3h : toutNow;

    int wait = 0; // minutes the burner still has to stay off (protection time)
    if (!S.relayActive && S.lastRelayChange != 0) {
        unsigned long off = now - S.lastRelayChange;
        if (off < MIN_OFF_TIME_MS) wait = (MIN_OFF_TIME_MS - off + 59999UL) / 60000UL;
    }

    float T = S.ctlTemp, q = S.heatQ;
    for (int m = 1; m <= 24 * 60; m++) {
        float frac = m < 180 ? m / 180.0f : 1.0f;
        float tout = toutNow + (toutLater - toutNow) * frac;
        float u = (m <= wait) ? 0.0f : controlDuty(T, q, target, toutCtl, band, maxDuty, nullptr);
        q += (u - q) * Q_ALPHA;
        T += (S.heatRate * q - S.lossK * (T - tout)) / 60.0f;
        if (T >= target - TARGET_TOLERANCE_C) {
            S.tttState = TTT_HEATING;
            S.tttMinutes = m;
            return;
        }
    }
    S.tttState = TTT_UNREACHABLE;
}

const char *tttStateName(TttState s) {
    switch (s) {
        case TTT_OFF: return "off";
        case TTT_REACHED: return "reached";
        case TTT_HEATING: return "heating";
        case TTT_UNREACHABLE: return "unreachable";
        default: return "unknown";
    }
}

// ============================ Faults & alerts ============================

void raiseBoilerFault(float expected, float actual) {
    S.faultActive = true;
    S.faultSince = timeValid() ? time(nullptr) : 0;
    S.faultExpected = expected;
    S.faultActual = actual;
    S.lastFaultNotifyMs = millis();
    boilerFaultCount++;
    notifyBoilerFault(expected, actual);
}

void clearBoilerFault(bool notify) {
    if (!S.faultActive) return;
    S.faultActive = false;
    if (notify) queueNotification(NOTIFY_IN_TURKISH ? "Kombi yeniden ısıtıyor." : "Boiler is heating again.");
}

// Called once per minute. Compares the real temperature rise during a continuous burn
// with what the learned model expects.
void checkBoilerFault(bool fresh) {
    if (!S.relayActive || !S.burnTracking || !fresh) return;
    S.burnMinutes++;
    float actual = S.indoorTemp - S.burnStartTemp;

    if (S.faultActive && actual >= 0.3f) { clearBoilerFault(true); return; }

    float expected = NAN;
    if (S.weatherEverOk) {
        S.burnSimQ += (1.0f - S.burnSimQ) * Q_ALPHA;
        S.burnSimT += (S.heatRate * S.burnSimQ - S.lossK * (S.burnSimT - S.outdoorTemp)) / 60.0f;
        expected = S.burnSimT - S.burnStartTemp;
    }

    if (S.burnMinutes == FAULT_CHECK_MIN && !S.faultActive) {
        bool noRise = !isnan(expected) && expected >= FAULT_MIN_EXPECTED_RISE &&
                      actual < 0.25f * expected && actual < 0.15f;
        bool falling = actual <= -0.5f;
        if (noRise || falling) raiseBoilerFault(isnan(expected) ? 0.0f : expected, actual);
    }

    if (S.faultActive && millis() - S.lastFaultNotifyMs >= FAULT_RENOTIFY_MS) {
        S.lastFaultNotifyMs = millis();
        notifyBoilerFault(S.faultExpected, actual);
    }
}

void checkSensorAlert(unsigned long now, bool fresh) {
    // Never heard from the sensor: alert once we've waited longer than the timeout
    bool offline = (S.lastSensorUpdate == 0) ? (now > SENSOR_TIMEOUT_MS + 300000UL) : !fresh;
    if (offline && !S.sensorAlert) {
        S.sensorAlert = true;
        S.sensorAlertSince = timeValid() ? time(nullptr) : 0;
        sensorTimeoutCount++;
        queueNotification(NOTIFY_IN_TURKISH
            ? "Sıcaklık sensöründen 10 dakikadır veri yok. Güvenlik için ısıtma durduruldu."
            : "Temperature sensor offline for 10 minutes. Heating is paused for safety.");
    } else if (!offline && S.sensorAlert) {
        S.sensorAlert = false;
        queueNotification(NOTIFY_IN_TURKISH ? "Sıcaklık sensörü yeniden çevrimiçi." : "Temperature sensor is back online.");
    }
}

// ============================ Calendar days ============================

void checkDayRollover() {
    if (!timeValid()) return;
    int32_t today = localDayNumber();
    if (P.todayDay == 0) { P.todayDay = today; S.statsDirty = true; return; }
    if (today <= P.todayDay) return;            // same day (or clock went backwards)
    int32_t gap = today - P.todayDay;
    for (int32_t s = 0; s < gap && s < 7; s++) {
        for (int i = 6; i > 0; i--) P.historyBurnSec[i] = P.historyBurnSec[i - 1];
        P.historyBurnSec[0] = (s == 0 && gap <= 7) ? P.todayBurnSec : 0;
    }
    P.todayBurnSec = 0;
    P.todayDay = today;
    savePersist();
}

// ============================ Once per minute ============================

void minuteTick(unsigned long now) {
    unsigned long span = now - S.lastMinuteTick;
    if (span == 0) span = 60000UL;
    S.lastMinuteTick = now;

    float u = constrain((float)S.relayOnMsThisMinute / span, 0.0f, 1.0f);
    S.relayOnMsThisMinute = 0;
    S.heatQ += (u - S.heatQ) * Q_ALPHA;

    bool fresh = sensorFresh(now);
    S.trendBuf[S.trendIdx] = fresh ? S.indoorTemp : NAN;
    S.trendIdx = (S.trendIdx + 1) % TREND_LEN;

    updateLearningWindow(now, fresh);
    checkBoilerFault(fresh);
    checkSensorAlert(now, fresh);
    checkDayRollover();
    S.tttDirty = true;
}

// ============================ Control ============================

void controlStep(unsigned long now) {
    if (S.lastSensorUpdate == 0) {
        updateRelay(false, true); S.duty = 0; S.feedForward = 0; S.reason = "waiting_sensor"; return;
    }
    if (!sensorFresh(now)) {
        updateRelay(false, true); S.duty = 0; S.feedForward = 0; S.reason = "sensor_timeout"; return;
    }
    if (P.mode == 0) {
        updateRelay(false, true); S.duty = 0; S.feedForward = 0; S.reason = "off"; return;
    }

    float ff;
    float duty = controlDuty(S.ctlTemp, S.heatQ, effectiveTarget(), outdoorForControl(),
                             bandForMode(), maxDutyForMode(), &ff);
    S.duty = duty;
    S.feedForward = ff;

    // Time-proportional output over a 20 min cycle, respecting the protection times
    if (now - S.pwmCycleStart >= PWM_PERIOD_MS) S.pwmCycleStart = now;
    unsigned long inCycle = now - S.pwmCycleStart;
    unsigned long onMs = (unsigned long)(duty * PWM_PERIOD_MS);
    if (onMs < MIN_ON_TIME_MS) onMs = (onMs >= MIN_ON_TIME_MS / 2) ? MIN_ON_TIME_MS : 0;
    if (onMs > 0 && PWM_PERIOD_MS - onMs < MIN_OFF_TIME_MS) onMs = PWM_PERIOD_MS;

    bool want = inCycle < onMs;
    bool applied = updateRelay(want);
    if (!applied)                     S.reason = want ? "min_off_wait" : "min_on_hold";
    else if (onMs == 0)               S.reason = "satisfied";
    else if (onMs >= PWM_PERIOD_MS)   S.reason = "heating_full";
    else                              S.reason = want ? "pwm_on" : "pwm_off";
}

// Local + cloud setters. fromLocal = change made on the dashboard (must be pushed to the cloud).
void setTarget(float t, bool fromLocal) {
    P.targetTemp = clampTarget(t);
    markSettingsDirty();
    S.pwmCycleStart = millis();   // start a fresh cycle with the new demand
    S.tttDirty = true;
    if (fromLocal) S.pendingTarget = true;
}

void setMode(uint8_t m, bool fromLocal) {
    P.mode = m;
    if (m != 0) P.lastOnMode = m;
    markSettingsDirty();
    S.pwmCycleStart = millis();
    S.tttDirty = true;
    if (fromLocal) { S.pendingMode = true; S.pendingPower = true; }
}

// ============================ Weather ============================

bool httpGetString(const String &url, String &out) {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    bool ok = code == HTTP_CODE_OK;
    if (ok) out = http.getString();
    http.end();
    return ok;
}

void fetchForecast(const String &base, const String &query) {
    String body;
    if (!httpGetString(base + "forecast" + query + "&cnt=3", body)) return;
    JsonDocument filter;
    filter["list"][0]["dt"] = true;
    filter["list"][0]["main"]["temp"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return;

    long ref = S.weatherObsEpoch > 0 ? S.weatherObsEpoch : (timeValid() ? (long)time(nullptr) : 0);
    if (ref == 0) return;
    long want = ref + 3L * 3600L;
    long prevDt = ref;
    float prevT = S.outdoorTemp, result = NAN;
    for (JsonObject e : doc["list"].as<JsonArray>()) {
        long dt = e["dt"] | 0L;
        float t = e["main"]["temp"] | NAN;
        if (dt <= 0 || isnan(t)) continue;
        if (dt >= want) {
            result = (dt == prevDt) ? t : prevT + (t - prevT) * (float)(want - prevDt) / (float)(dt - prevDt);
            break;
        }
        prevDt = dt; prevT = t; result = t;   // fall back to the latest point we have
    }
    if (!isnan(result)) {
        S.forecast3h = result;
        S.lastForecastUpdate = millis();
    }
}

// Current weather (+ 3 h forecast). Returns true when the current weather succeeded.
bool fetchWeather() {
    S.lastWeatherAttempt = millis();
    weatherFetchAttemptCount++;
    if (WiFi.status() != WL_CONNECTED) { weatherFetchFailCount++; return false; }

    const String base = "http://api.openweathermap.org/data/2.5/";
    const String query = String("?lat=") + LATITUDE + "&lon=" + LONGITUDE +
                         "&appid=" + WEATHER_API_KEY + "&units=metric";
    String body;
    if (!httpGetString(base + "weather" + query, body)) { weatherFetchFailCount++; return false; }
    {
        JsonDocument doc;
        if (deserializeJson(doc, body) || !doc["main"]["temp"].is<float>()) {
            weatherFetchFailCount++;
            return false;
        }
        S.outdoorTemp = doc["main"]["temp"].as<float>();
        S.weatherObsEpoch = doc["dt"] | 0L;
        S.weatherEverOk = true;
        S.lastWeatherUpdate = millis();
    }
    body = String();
    fetchForecast(base, query);
    S.tttDirty = true;
    return true;
}

// ============================ Cloud (Sinric Pro) ============================

void syncCloudState(unsigned long now) {
    if (!SinricPro.isConnected()) return;
    if (S.pendingTarget && now - S.lastTargetEvt >= CLOUD_STATE_GAP_MS) {
        S.lastTargetEvt = now;
        if (thermo->sendTargetTemperatureEvent(P.targetTemp)) S.pendingTarget = false;
    }
    if (S.pendingPower && now - S.lastPowerEvt >= CLOUD_STATE_GAP_MS) {
        S.lastPowerEvt = now;
        if (thermo->sendPowerStateEvent(P.mode != 0)) S.pendingPower = false;
    }
    if (S.pendingMode && now - S.lastModeEvt >= CLOUD_STATE_GAP_MS) {
        S.lastModeEvt = now;
        if (thermo->sendThermostatModeEvent(cloudMode())) S.pendingMode = false;
    }
}

// Sends the measured temperature when it changed (or as a keepalive), never faster than
// Sinric allows. The timer always advances so a failed send can't become a tight loop
// (the library would then print warnings onto Serial = the relay UART).
void reportTemperatureToCloud(unsigned long now) {
    static unsigned long lastSendMs = 0;
    static bool sentOnce = false;
    static float lastSentTemp = NAN, lastSentHum = NAN;

    if (S.lastSensorUpdate == 0 || !SinricPro.isConnected()) return;
    if (sentOnce && now - lastSendMs < CLOUD_TEMP_MIN_MS) return;

    bool changed = S.forceTempReport || isnan(lastSentTemp) ||
                   fabsf(S.indoorTemp - lastSentTemp) >= 0.1f ||
                   fabsf(S.indoorHum - lastSentHum) >= 1.0f;
    if (!changed && now - lastSendMs < CLOUD_TEMP_KEEPALIVE_MS) return;

    lastSendMs = now;
    sentOnce = true;
    if (thermo->sendTemperatureEvent(S.indoorTemp, S.indoorHum)) {
        lastSentTemp = S.indoorTemp;
        lastSentHum = S.indoorHum;
        S.forceTempReport = false;
    }
}

bool onPowerState(const String &deviceId, bool &state) {
    setMode(state ? P.lastOnMode : 0, false);   // "turn on" restores HEAT or ECO
    S.pendingMode = true;                       // keep Google's mode in step with power
    return true;
}

bool onThermostatMode(const String &deviceId, String &mode) {
    String m = mode;
    m.toUpperCase();
    if (m == "OFF") {
        setMode(0, false);
    } else if (m == "HEAT") {
        if (P.mode == 0) setMode(P.lastOnMode, false);   // a local ECO stays ECO
    } else {
        SinricPro.setResponseMessage(String("This thermostat supports HEAT and OFF only"));
        return false;
    }
    mode = cloudMode();          // respond with the mode actually applied
    S.pendingPower = true;
    return true;
}

bool onTargetTemperature(const String &deviceId, float &temp) {
    setTarget(temp, false);
    temp = P.targetTemp;         // respond with the value actually applied
    return true;
}

bool onAdjustTargetTemperature(const String &deviceId, float &temperatureDelta) {
    setTarget(P.targetTemp + temperatureDelta, false);
    temperatureDelta = P.targetTemp;   // Sinric expects the new absolute value back
    return true;
}

// ============================ Web handlers ============================

void sendJson(int code, JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    server.send(code, "application/json", out);
}

void sendError(int code, const char *msg) {
    JsonDocument doc;
    doc["status"] = "error";
    doc["message"] = msg;
    sendJson(code, doc);
}

void handleRoot() {
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", dashboard_html);
}

void handleStatus() {
    unsigned long now = millis();
    bool fresh = sensorFresh(now);
    JsonDocument doc;

    JsonObject sys = doc["system"].to<JsonObject>();
    sys["relay"] = S.relayActive;
    sys["mode"] = modeName(P.mode);
    sys["reason"] = S.reason;
    sys["duty_pct"] = (int)roundf(S.duty * 100.0f);
    sys["base_load_pct"] = (int)roundf(S.feedForward * 100.0f);
    sys["uptime_s"] = S.uptimeSec;
    sys["firmware"] = FIRMWARE_VERSION;

    JsonObject clk = doc["time"].to<JsonObject>();
    bool tv = timeValid();
    clk["synced"] = tv;
    if (tv) {
        time_t t = time(nullptr);
        struct tm lt;
        localtime_r(&t, &lt);
        char buf[20];
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &lt);
        clk["local"] = buf;
        clk["epoch"] = (uint32_t)t;
    }
    if (S.lastNtpSync) clk["last_sync_epoch"] = (uint32_t)S.lastNtpSync;

    JsonObject env = doc["env"].to<JsonObject>();
    env["sensor_ok"] = fresh;
    if (S.lastSensorUpdate != 0) {
        env["indoor"] = r1(S.indoorTemp);
        env["humidity"] = r1(S.indoorHum);
        env["feels_like"] = r1(computeFeelsLike(S.indoorTemp, S.indoorHum)); // info only
    }
    if (!isnan(S.ctlTemp)) env["control_temp"] = r1(S.ctlTemp);
    float trend = trendPerHour();
    if (!isnan(trend)) env["trend_c_per_h"] = r1(trend);
    env["target"] = r1(P.targetTemp);
    env["effective_target"] = r1(effectiveTarget());
    if (S.weatherEverOk) {
        env["outdoor"] = r1(S.outdoorTemp);
        env["weather_age_min"] = (now - S.lastWeatherUpdate) / 60000UL;
    }
    if (forecastFresh()) env["outdoor_3h"] = r1(S.forecast3h);
    float toutCtl = outdoorForControl();
    if (!isnan(toutCtl)) env["outdoor_control"] = r1(toutCtl);

    JsonObject ttt = doc["time_to_target"].to<JsonObject>();
    ttt["state"] = tttStateName(S.tttState);
    if (S.tttMinutes >= 0) ttt["minutes"] = S.tttMinutes;

    JsonObject model = doc["model"].to<JsonObject>();
    model["heat_rate_c_per_h"] = r2(S.heatRate);
    model["loss_per_h"] = roundf(S.lossK * 1000.0f) / 1000.0f;
    model["hours_learned"] = r1(P.Sn);
    model["hours_needed"] = MODEL_LEARNED_WINDOWS;
    model["learned"] = P.Sn >= MODEL_LEARNED_WINDOWS;
    model["radiator_lag_min"] = RADIATOR_LAG_MIN;

    JsonObject gas = doc["gas"].to<JsonObject>();
    uint32_t week = P.todayBurnSec;
    for (int i = 0; i < 6; i++) week += P.historyBurnSec[i];
    gas["today_m3"] = r2(burnToM3(P.todayBurnSec));
    gas["today_burn_min"] = P.todayBurnSec / 60;
    gas["yesterday_m3"] = r2(burnToM3(P.historyBurnSec[0]));
    gas["week_m3"] = r2(burnToM3(week));
    gas["week_burn_min"] = week / 60;
    JsonArray hist = gas["history_m3"].to<JsonArray>();   // [0] = yesterday
    for (int i = 0; i < 7; i++) hist.add(r2(burnToM3(P.historyBurnSec[i])));
    gas["rate_m3_per_h"] = GAS_RATE_M3_HR;
    gas["factor"] = roundf(P.gasFactor * 1000.0f) / 1000.0f;
    gas["calibrating"] = P.calibActive != 0;
    if (P.calibActive) {
        gas["calib_start_meter"] = P.calibMeterM3;
        gas["calib_burn_h"] = r1((P.lifetimeBurnSec - P.calibBurnSec) / 3600.0f);
        gas["calib_need_h"] = GAS_CALIB_MIN_BURN_H;
    }
    gas["day_known"] = P.todayDay != 0;

    JsonArray alerts = doc["alerts"].to<JsonArray>();
    if (S.faultActive) {
        JsonObject a = alerts.add<JsonObject>();
        a["code"] = "boiler_not_heating";
        a["since"] = (uint32_t)S.faultSince;
        a["expected"] = r1(S.faultExpected);
        a["actual"] = r1(S.faultActual);
        a["minutes"] = FAULT_CHECK_MIN;
    }
    if (S.sensorAlert) {
        JsonObject a = alerts.add<JsonObject>();
        a["code"] = "sensor_offline";
        a["since"] = (uint32_t)S.sensorAlertSince;
    }

    JsonObject lim = doc["limits"].to<JsonObject>();
    lim["min_target"] = MIN_TARGET_C;
    lim["max_target"] = MAX_TARGET_C;
    lim["eco_setback"] = ECO_SETBACK_C;
    lim["eco_max_duty_pct"] = (int)(ECO_MAX_DUTY * 100);
    lim["target_tolerance"] = TARGET_TOLERANCE_C;

    JsonObject net = doc["network"].to<JsonObject>();
    net["ssid"] = WiFi.SSID();
    net["ip"] = WiFi.localIP().toString();
    net["rssi_dbm"] = WiFi.RSSI();
    net["mac"] = WiFi.macAddress();

    JsonObject hw = doc["hardware"].to<JsonObject>();
    hw["chip_id"] = String(ESP.getChipId());
    hw["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    hw["flash_real_size_bytes"] = ESP.getFlashChipRealSize();
    hw["free_heap_bytes"] = ESP.getFreeHeap();
    hw["sketch_size_bytes"] = ESP.getSketchSize();
    hw["free_sketch_space_bytes"] = ESP.getFreeSketchSpace();
    hw["reset_reason"] = ESP.getResetReason();

    JsonObject diag = doc["diagnostics"].to<JsonObject>();
    diag["wifi_disconnect_count"] = boilerWifiDisconnectCount;
    diag["relay_switch_count"] = relaySwitchCount;
    diag["sensor_timeouts"] = sensorTimeoutCount;
    diag["boiler_faults"] = boilerFaultCount;
    diag["weather_fetch_attempts"] = weatherFetchAttemptCount;
    diag["weather_fetch_fails"] = weatherFetchFailCount;
    diag["model_samples"] = modelSamplesCount;
    diag["ntp_syncs"] = ntpSyncCount;

    JsonObject sens = doc["sensor"].to<JsonObject>();
    sens["connected"] = fresh;
    if (S.lastSensorUpdate != 0) {
        sens["ip"] = sensorDiag.ip;
        sens["rssi_dbm"] = sensorDiag.rssi;
        sens["free_heap_bytes"] = sensorDiag.freeHeap;
        sens["dht_fail_rate_pct"] = r1(sensorDiag.dhtFailRate);
        sens["uptime_seconds"] = sensorDiag.uptime;
        sens["firmware_version"] = sensorDiag.firmwareVersion;
        sens["last_seen_seconds_ago"] = (now - sensorDiag.lastReceivedMs) / 1000UL;
    }

    sendJson(200, doc);
}

void handleSet() {
    // Validate everything before applying anything
    float newTarget = NAN;
    if (server.hasArg("target")) {
        String s = server.arg("target");
        s.replace(',', '.');
        newTarget = s.toFloat();   // 0 for garbage -> rejected below
        if (newTarget < MIN_TARGET_C || newTarget > MAX_TARGET_C) return sendError(400, "target out of range");
    }
    int newMode = -1;
    if (server.hasArg("mode")) {
        String m = server.arg("mode");
        m.toUpperCase();
        if (m == "HEAT") newMode = 1;
        else if (m == "ECO") newMode = 2;
        else if (m == "OFF") newMode = 0;
        else return sendError(400, "unknown mode");
    }
    if (!isnan(newTarget)) setTarget(newTarget, true);
    if (newMode >= 0) setMode((uint8_t)newMode, true);

    JsonDocument doc;
    doc["status"] = "ok";
    doc["target"] = r1(P.targetTemp);
    doc["mode"] = modeName(P.mode);
    sendJson(200, doc);
}

// Gas meter calibration: POST /api/gas?meter=12345.678  (twice, days apart)
//                        POST /api/gas?reset=1
void handleGas() {
    JsonDocument doc;
    if (server.hasArg("reset")) {
        P.gasFactor = 1.0f;
        P.calibActive = 0;
        savePersist();
        doc["status"] = "ok";
        doc["result"] = "reset";
        return sendJson(200, doc);
    }
    if (!server.hasArg("meter")) return sendError(400, "missing meter");
    String s = server.arg("meter");
    s.replace(',', '.');
    double meter = s.toDouble();
    if (!(meter > 0)) {
        doc["status"] = "error"; doc["result"] = "invalid";
        return sendJson(400, doc);
    }
    if (!P.calibActive) {
        P.calibActive = 1;
        P.calibMeterM3 = meter;
        P.calibBurnSec = P.lifetimeBurnSec;
        savePersist();
        doc["status"] = "ok"; doc["result"] = "started";
        return sendJson(200, doc);
    }
    double used = meter - P.calibMeterM3;
    float burnH = (P.lifetimeBurnSec - P.calibBurnSec) / 3600.0f;
    if (used <= 0) {
        doc["status"] = "error"; doc["result"] = "meter_not_higher"; doc["start_meter"] = P.calibMeterM3;
        return sendJson(400, doc);
    }
    if (burnH < GAS_CALIB_MIN_BURN_H) {
        doc["status"] = "error"; doc["result"] = "need_more_burn";
        doc["burn_h"] = r1(burnH); doc["need_h"] = GAS_CALIB_MIN_BURN_H;
        return sendJson(400, doc);
    }
    float factor = constrain((float)(used / (burnH * GAS_RATE_M3_HR)), 0.2f, 2.0f);
    P.gasFactor = factor;
    P.calibMeterM3 = meter;                 // keep calibrating from this reading,
    P.calibBurnSec = P.lifetimeBurnSec;     // so the factor can follow the season
    savePersist();
    doc["status"] = "ok"; doc["result"] = "calibrated";
    doc["factor"] = roundf(factor * 1000.0f) / 1000.0f;
    doc["used_m3"] = used;
    doc["burn_h"] = r1(burnH);
    sendJson(200, doc);
}

void handleAlertsClear() {
    clearBoilerFault(false);
    JsonDocument doc;
    doc["status"] = "ok";
    sendJson(200, doc);
}

void handleSensor() {
    if (!server.hasArg("plain")) return server.send(400);
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) return server.send(400);

    // Reject payloads without a plausible temperature instead of inventing one
    float t;
    if (doc["indoorTemp"].is<float>())       t = doc["indoorTemp"].as<float>();
    else if (doc["temperature"].is<float>()) t = doc["temperature"].as<float>();
    else return sendError(400, "missing temperature");
    if (isnan(t) || t < -20.0f || t > 60.0f) return sendError(400, "temperature out of range");
    S.indoorTemp = t;

    // Smoothed copy for control: averages out the sensor's 0.1 °C flicker so the burner
    // doesn't click on/off on noise. Restart it after a sensor outage.
    unsigned long nowMs = millis();
    if (isnan(S.ctlTemp) || !sensorFresh(nowMs)) {
        S.ctlTemp = t;
    } else {
        float dtS = (nowMs - S.lastSensorUpdate) / 1000.0f;
        S.ctlTemp += (t - S.ctlTemp) * (1.0f - expf(-dtS / CONTROL_SMOOTH_S));
    }

    float h = NAN;
    if (doc["indoorHum"].is<float>())     h = doc["indoorHum"].as<float>();
    else if (doc["humidity"].is<float>()) h = doc["humidity"].as<float>();
    if (!isnan(h) && h >= 0.0f && h <= 100.0f) S.indoorHum = h;

    S.lastSensorUpdate = millis();

    sensorDiag.ip = server.client().remoteIP().toString();
    sensorDiag.rssi = doc["sensorRSSI"] | 0;
    sensorDiag.freeHeap = doc["sensorHeap"] | 0;
    sensorDiag.dhtFailRate = doc["dhtFailRate"] | 0.0f;
    sensorDiag.uptime = doc["sensorUptime"] | 0;
    if (doc["sensorFwVersion"].is<const char *>()) sensorDiag.firmwareVersion = doc["sensorFwVersion"].as<String>();
    sensorDiag.lastReceivedMs = millis();

    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ============================ Setup & loop ============================

void setup() {
    Serial.begin(BAUD_RATE);

    // Force the relay OFF on boot. After a watchdog/OTA reset the relay board may still be
    // ON while relayActive starts as false, which would leave the boiler running.
    writeRelay(false);

    EEPROM.begin(512);
    loadPersist();
    solveModel();
    for (int i = 0; i < TREND_LEN; i++) S.trendBuf[i] = NAN;

    ESP.wdtEnable(8000);

    WiFi.mode(WIFI_STA);
    WiFi.hostname("thermostat-boiler");
    WiFi.setSleepMode(WIFI_NONE_SLEEP);   // mains powered: stay responsive
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        ESP.wdtFeed();
    }

    // Clock: NTP with the Turkey time zone (DST-free, UTC+3)
    settimeofday_cb([](bool fromSntp) {
        if (fromSntp) {
            S.lastNtpSync = time(nullptr);
            ntpSyncCount++;
        }
    });
    configTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

    if (MDNS.begin("boiler")) {
        MDNS.addService("http", "tcp", 80);
    }

    // Sinric Pro
    SinricProThermostat &t = SinricPro[THERMOSTAT_ID];
    thermo = &t;
    t.onPowerState(onPowerState);
    t.onTargetTemperature(onTargetTemperature);
    t.onAdjustTargetTemperature(onAdjustTargetTemperature);
    t.onThermostatMode(onThermostatMode);
    SinricPro.onConnected([]() {
        // The device is the source of truth once settings are stored locally
        if (persistLoadedFromFlash) S.pendingTarget = S.pendingMode = S.pendingPower = true;
        S.forceTempReport = true;
    });
    // First boot without stored settings: take the last state from the cloud. Deprecated
    // in favour of a portal switch, but still functional and we only want it on first boot.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    SinricPro.restoreDeviceStates(!persistLoadedFromFlash);
#pragma GCC diagnostic pop
    SinricPro.begin(APP_KEY, APP_SECRET);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/set", HTTP_POST, handleSet);
    server.on("/api/gas", HTTP_POST, handleGas);
    server.on("/api/alerts/clear", HTTP_POST, handleAlertsClear);
    server.on("/sensor", HTTP_POST, handleSensor);
    httpUpdater.setup(&server);   // /update
    server.begin();

    S.lastWeatherOk = fetchWeather();
    S.lastMillis = millis();
    S.lastMinuteTick = millis();
    S.lastStatsSave = millis();
}

void loop() {
    ESP.wdtFeed();
    MDNS.update();
    SinricPro.handle();
    server.handleClient();

    static bool wasConnected = false;
    bool isConnected = WiFi.status() == WL_CONNECTED;
    if (!isConnected && wasConnected) boilerWifiDisconnectCount++;
    wasConnected = isConnected;

    unsigned long now = millis();
    unsigned long delta = now - S.lastMillis;
    S.lastMillis = now;

    // Uptime (carry the leftover milliseconds; one loop is only ~10 ms)
    static unsigned long uptimeMsAcc = 0;
    uptimeMsAcc += delta;
    S.uptimeSec += uptimeMsAcc / 1000UL;
    uptimeMsAcc %= 1000UL;

    // Burner time -> gas statistics and the model's burner input
    if (S.relayActive) {
        S.relayOnMsThisMinute += delta;
        static unsigned long gasMsAcc = 0;
        gasMsAcc += delta;
        if (gasMsAcc >= 1000UL) {
            uint32_t sec = gasMsAcc / 1000UL;
            gasMsAcc %= 1000UL;
            P.todayBurnSec += sec;
            P.lifetimeBurnSec += sec;
            S.statsDirty = true;
        }
    }

    // Weather (based on the last attempt, so failures don't retry every loop)
    unsigned long weatherInterval = S.lastWeatherOk ? WEATHER_SYNC_MS : WEATHER_RETRY_MS;
    if (now - S.lastWeatherAttempt > weatherInterval) S.lastWeatherOk = fetchWeather();

    if (now - S.lastMinuteTick >= 60000UL) minuteTick(now);

    controlStep(now);

    if (S.tttDirty) {
        computeTimeToTarget(now);
        S.tttDirty = false;
    }

    // Re-send the relay state in case the relay board missed a command or reset
    if (now - S.lastRelayReassert >= RELAY_REASSERT_MS) writeRelay(S.relayActive);

    syncCloudState(now);
    reportTemperatureToCloud(now);
    processNotifications(now);

    if (S.settingsDirty && now - S.settingsDirtyAt >= SETTINGS_SAVE_DELAY_MS) savePersist();
    else if (S.statsDirty && now - S.lastStatsSave >= STATS_SAVE_MS) savePersist();

    delay(10);
}
