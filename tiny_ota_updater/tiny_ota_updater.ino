/*
 * tiny_ota_updater.ino
 * 
 * A bare-minimum 2-Stage OTA web updater for 1MB ESP8266 devices.
 * Connects to WiFi and hosts a Web Server on Port 80.
 * Go to http://<ip>/update to upload your production binary.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>

#define FIRMWARE_VERSION "1.0"
#define WIFI_SSID "Don't Connect to Strangers"
#define WIFI_PASS "ESTyukselEST3118889"

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

void handleRoot() {
  String html = "<html><head><title>Tiny OTA Updater</title>";
  html += "<style>body{font-family:sans-serif;background:#121212;color:#ffffff;text-align:center;padding-top:50px;}";
  html += ".container{max-width:500px;margin:0 auto;background:#1e1e1e;padding:30px;border-radius:8px;box-shadow:0 4px 10px rgba(0,0,0,0.5);}";
  html += "h1{color:#00adb5;} p{color:#eeeeee;} a{color:#00adb5;text-decoration:none;font-weight:bold;}</style></head>";
  html += "<body><div class='container'><h1>Tiny OTA Updater Active</h1>";
  html += "<p>This is the intermediate bare-minimum stage.</p>";
  html += "<p>To flash your production binary, navigate to:</p>";
  html += "<h2><a href='/update'>/update</a></h2>";
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  WiFi.mode(WIFI_STA);
  WiFi.hostname("thermostat-ota-updater");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("ota-updater")) {
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", HTTP_GET, handleRoot);
  
  // Setup HTTP Web OTA Updater
  httpUpdater.setup(&server);
  server.begin();
  
  Serial.println("Web server started at http://ota-updater.local/ or http://" + WiFi.localIP().toString() + "/");
  Serial.println("Navigate to /update to perform OTA.");
}

void loop() {
  server.handleClient();
  MDNS.update();
}
