# ThermostatESP

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

### Two-Stage OTA Updates for 1MB Flash Boards

Because all boards in this system (both the Boiler Controller and the Sensor Node) are configured with **1MB of flash memory**, they lack the physical space to hold two production firmwares at once (the currently running one + the new one downloading). 

Therefore, both nodes use a robust **Two-Stage OTA** process to upgrade:

1. **Stage 1 (Flash Tiny Updater):** Compile and upload the `tiny_ota_updater.ino` sketch. If the device already has OTA, you can upload the `tiny_ota.bin` update binary via the node's local update page (e.g., `http://boiler.local/update` or `http://sensor.local/update`). This strips out heavy libraries (like SinricPro) and leaves a tiny 230KB web server.
2. **Stage 2 (Flash Production):** The device will reboot and expose a temporary, clean updater page at `http://ota-updater.local/update`. Now, select your large production binary (`boiler.bin` or `sensor.bin`) and upload it. It will write over the empty flash and boot directly into the main production firmware.

## Automated CI/CD (GitHub Actions)

This repository is equipped with a GitHub Action (`.github/workflows/release.yml`) for automated builds:
- When you create a **Release** on GitHub, the action compiles `.bin` files for the Boiler, Sensor, and Tiny OTA updater.
- It dynamically injects the Release Tag (e.g., `v1.2`) into the `FIRMWARE_VERSION` variable inside the sketches.
- The compiled binaries are attached directly to your GitHub Release for easy downloading and flashing.
