/*
 * read_memory_sketch.ino
 * 
 * A diagnostic utility to read the physical flash size of your ESP8266 chip 
 * and display it over a clean web interface.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define WIFI_SSID "Don't Connect to Strangers"
#define WIFI_PASS "ESTyukselEST3118889"

ESP8266WebServer server(80);

void handleRoot() {
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t mode = ESP.getFlashChipMode();
  uint32_t speed = ESP.getFlashChipSpeed();

  String html = "<html><head><title>ESP8266 Flash Diagnostics</title>";
  html += "<style>body{font-family:sans-serif;background:#121212;color:#ffffff;padding:40px;}";
  html += ".card{max-width:600px;margin:0 auto;background:#1e1e1e;padding:30px;border-radius:12px;box-shadow:0 8px 20px rgba(0,0,0,0.6);border:1px solid #333;}";
  html += "h1{color:#ff9f43;text-align:center;margin-bottom:30px;}";
  html += ".metric{display:flex;justify-content:space-between;margin:15px 0;padding-bottom:10px;border-bottom:1px solid #2a2a2a;}";
  html += ".label{font-weight:bold;color:#aaa;} .val{color:#fff;font-family:monospace;font-size:1.1em;}";
  html += ".highlight{color:#2ecc71;font-weight:bold;} .warning{color:#e74c3c;font-weight:bold;}</style></head>";
  html += "<body><div class='card'><h1>ESP8266 Flash Info</h1>";

  html += "<div class='metric'><span class='label'>Physical Flash Size (Real):</span><span class='val highlight'>";
  html += String(realSize / 1024.0 / 1024.0, 1) + " MB (" + String(realSize) + " bytes)</span></div>";

  html += "<div class='metric'><span class='label'>IDE Configured Flash Size:</span><span class='val'>";
  html += String(ideSize / 1024.0 / 1024.0, 1) + " MB (" + String(ideSize) + " bytes)</span></div>";

  html += "<div class='metric'><span class='label'>Flash Chip Speed:</span><span class='val'>";
  html += String(speed / 1000000) + " MHz</span></div>";

  html += "<div class='metric'><span class='label'>Flash Chip Mode:</span><span class='val'>";
  html += (mode == FM_QIO ? "QIO" : mode == FM_QOUT ? "QOUT" : mode == FM_DIO ? "DIO" : mode == FM_DOUT ? "DOUT" : "Unknown") + "</span></div>";

  html += "<div class='metric'><span class='label'>Free Heap (RAM):</span><span class='val'>";
  html += String(ESP.getFreeHeap()) + " bytes</span></div>";

  html += "<div class='metric'><span class='label'>OTA Compatibility Status:</span><span class='val ";
  if (realSize >= 4194304) {
    html += "highlight'>EXCELLENT (4MB Chip - Fully supports direct OTA)</span></div>";
  } else {
    html += "warning'>RESTRICTED (1MB Chip - Requires 2-Stage OTA)</span></div>";
  }

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  if (MDNS.begin("esp-flash-info")) {
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", HTTP_GET, handleRoot);
  server.begin();
  
  Serial.println("\nDiagnostics server started at http://esp-flash-info.local/ or http://" + WiFi.localIP().toString() + "/");
}

void loop() {
  server.handleClient();
  MDNS.update();
}
