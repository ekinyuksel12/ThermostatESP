# Smart Thermostat System

A dual-node ESP8266-based smart thermostat system designed to control a Viessmann Vitopend 100 gas boiler via local temperature sensors, with an elegant built-in Dark Web UI and SinricPro cloud integration.

## Architecture

The system is distributed across two main hardware nodes:

1. **Boiler Controller (`esp8266_boiler.ino`)**
   - **Hardware:** ESP8266 (typically NodeMCU/Wemos D1 Mini with 4MB flash) connected directly to the boiler's dry-contact relay.
   - **Role:** The brain of the operation. It hosts the local Web Dashboard (`dashboard.h`), calculates the *Apparent Temperature* using heat-index formulas, manages the Time-Proportional Control (PWM) logic for the boiler, fetches outdoor weather via OpenWeatherMap, and syncs status to Sinric Pro.
   - **Network:** Connects to WiFi, exposes an HTTP server (`boiler.local`).

2. **Remote Sensor Node (`esp8266_sensor.ino`)**
   - **Hardware:** ESP-01 (1MB flash constraint) paired with a DHT11 temperature and humidity sensor.
   - **Role:** Reads environmental data in the living space.
   - **Network:** Pushes data to the boiler controller via an HTTP POST JSON payload (`/sensor` endpoint) every 30 seconds. It also synchronizes directly with Sinric Pro to expose the room temperature to voice assistants (Alexa/Google Home).

### Communication Flow
- **Sensor -> Boiler:** `HTTP POST /sensor` containing JSON (Temp, Hum, RSSI, Uptime, Diagnostics).
- **Boiler -> User:** Local Network HTML Dashboard (`/`) built with vanilla JS and CSS, fetching dynamic metrics from `GET /api/status`.
- **Boiler/Sensor -> Cloud:** Both nodes maintain WebSockets to SinricPro for remote app control and voice integration.

## Updating Over The Air (OTA)

This system is fully OTA-compatible, meaning you can update the firmware wirelessly through your web browser without connecting a USB cable.

### For 4MB Boards (Boiler Controller)
Because 4MB boards have plenty of flash memory, you can perform direct OTA updates:
1. In Arduino IDE, go to **Sketch -> Export Compiled Binary** to generate your `.bin` file.
2. Open your web browser and navigate to: `http://boiler.local/update` (or the IP address).
3. Upload the `.bin` file and click Update. The device will reboot automatically.

### For 1MB Boards (Sensor Node / ESP-01)
1MB boards lack the space to hold two firmwares at once (the current one + the new one downloading). We use a **Two-Stage OTA** process:
1. **Stage 1 (Flash Tiny Updater):** Compile and upload the `tiny_ota_updater.ino` sketch. If the device already has OTA, you can upload `tiny_ota.bin` via `http://sensor.local/update`. This strips out heavy libraries (like SinricPro) and leaves a tiny 230KB web server.
2. **Stage 2 (Flash Production):** The device will reboot and expose a new updater at `http://ota-updater.local/update`. Now, select your large production `sensor.bin` and upload it. It will write over the empty space and boot into the main firmware.

## Automated CI/CD (GitHub Actions)

This repository is equipped with a GitHub Action (`.github/workflows/release.yml`) for automated builds:
- When you create a **Release** on GitHub, the action compiles `.bin` files for the Boiler, Sensor, and Tiny OTA updater.
- It dynamically injects the Release Tag (e.g., `v1.2`) into the `FIRMWARE_VERSION` variable inside the sketches.
- The compiled binaries are attached directly to your GitHub Release for easy downloading and flashing.
