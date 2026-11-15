# ThermostatESP

A self-learning smart thermostat for a combi gas boiler (Viessmann Vitopend 100), built from two ESP8266 boards: a **room sensor** and a **boiler controller**. It learns how your home heats up and cools down, adjusts for the weather, estimates gas use, warns you when the boiler stops heating, and works with Google Home through Sinric Pro. A local dashboard (English / Türkçe) shows everything and explains what the thermostat is doing.

---

## Features

- **Controls on real temperature** measured in the coldest room. Feels-like is shown on the dashboard for information only.
- **Learns your home.** Every hour it updates two numbers: how fast the heating warms the house, and how fast the house loses heat.
- **Weather-aware heating.** Uses the outdoor temperature and a 3-hour forecast to start from the right amount of heat instead of waiting for the room to cool.
- **Time to target.** Simulates the learned model with the real control logic (including Eco limits and the outdoor forecast), so you see "reaches 21° in about 45 min — around 07:40".
- **Gas usage estimate.** Counted per calendar day (NTP clock, Turkey time), kept across restarts, and optionally calibrated against your real gas meter.
- **Boiler fault alerts.** Warns if the burner runs for an hour but the room doesn't warm up as expected, or if the room sensor goes silent. Alerts appear on the dashboard and as Sinric Pro push notifications.
- **Google Home / Alexa** via Sinric Pro: Heat / Off, target temperature, real room temperature and humidity.
- **Boiler-friendly.** Time-proportional control with minimum run (3 min) and rest (5 min) times, and smoothed sensor input so the relay doesn't click on noise.
- **Safe by default.** Relay forced OFF on boot and re-sent every minute, heating stops if the sensor is silent for 10 minutes, and a hardware watchdog.
- **Liquid-glass web dashboard.** Mobile-first, light/dark, English and Turkish, with an ⓘ explanation on every card.

---

## How it fits together

```
 ┌──────────────────────┐   HTTP POST /sensor (every 30 s)   ┌──────────────────────────┐
 │  Room sensor          │ ─────────────────────────────────▶ │  Boiler controller        │
 │  ESP-01 + DHT11       │   temperature, humidity, health    │  ESP8266 + serial relay   │──▶ boiler room-thermostat
 │  (coldest room)       │                                    │  boiler.local              │    contacts
 └──────────────────────┘                                    └──────────┬───────────────┘
                                                                         │
            OpenWeatherMap (current + 3 h forecast) ◀────────────────────┤
            NTP (pool.ntp.org, Turkey time) ◀────────────────────────────┤
            Sinric Pro ⇄ Google Home / Alexa, push notifications ◀──────▶┤
            Your phone / browser: http://boiler.local  ◀────────────────┘
```

All decisions are made on the boiler controller. If the internet is down, it keeps heating with the last known weather and the local dashboard keeps working. Only Google Home and notifications need the cloud.

---

## How the heating decisions work

### 1. The temperature it controls on
The sensor sits in the **coldest room**, so the target means "no room goes below this". The controller uses a lightly smoothed copy of the reading (≈3 min average). This removes the DHT11's 0.1 °C flicker without noticeably slowing the response.

### 2. The learned model of your home
The room temperature is modelled as

```
dT/dt = a · q  −  k · (T_inside − T_outside)
```

- **a** – heat-up speed with the burner fully on, before losses (°C per hour)
- **k** – heat loss: the share of the inside–outside difference lost per hour
- **q** – burner output passed through a 15-minute lag, because radiators take time to warm up and cool down

Every hour, the controller fits the measured warming or cooling rate against `q` and `T_inside − T_outside` using weighted least squares. Older hours fade out over about two days, so the model follows the season. Until about 12 hours of usable data exist, sensible defaults (a = 2.5 °C/h, k = 0.08 /h) are blended in. The learned values are stored in flash and survive restarts.

In a simulation against a deliberately different two-stage house model, it learned a = 2.70 / k = 0.068 (true values 2.70 / 0.070) within a day.

### 3. Weather-aware, time-proportional burner control
Every 20 minutes the controller decides what **share of the time** the burner runs:

```
share = base load + (target − predicted room temperature) / 0.5 °C
```

- **Base load** is the share your home needs to just *hold* the target at the outdoor temperature: `k · (target − T_out) / a`. T_out is the average of now and 3 hours ahead, because a house reacts slowly.
- **Predicted room temperature** adds the heat still stored in the radiators, so the burner stops a little early instead of overshooting.
- The share becomes on/off time inside the 20-minute cycle. Very short bursts are skipped or stretched to the 3-minute minimum run, and the burner always rests at least 5 minutes.

In simulation this held the room within ±0.1 °C of the target, with under 0.1 °C overshoot, at about 2.5–3 burner cycles per hour.

### 4. Eco mode (local only)
Eco aims **1 °C below** the target, widens the band to 1 °C and caps the burner at **50%** of the time. If that cap makes the target unreachable at the current outdoor temperature, the dashboard says so.

### 5. Time to target
The controller simulates the learned model minute by minute with the same control law, the Eco limits, any remaining burner rest time, and the outdoor forecast. "Reached" means within 0.3 °C, because the last few tenths are approached gently on purpose. In simulation, heat-up predictions were typically within a few minutes of the real time.

### 6. Safety
- No sensor data for 10 minutes → heating OFF and an alert.
- Relay forced OFF at boot. The relay board keeps its state across ESP resets, so this matters. The current state is also re-sent every minute.
- Hardware watchdog; weather failures retry every 2 minutes instead of blocking.

---

## Gas usage

The thermostat knows when it asks the boiler for heat, not how hard the boiler burns. The estimate is:

```
m³ = burner hours × 2.6 m³/h (Vitopend 100, 24 kW full load) × calibration factor
```

- Days roll over at **local midnight** (NTP clock). Today, yesterday and the last 7 days are shown, and saved to flash every 30 minutes and at midnight.
- **Calibrate with your meter** (Energy tab):
  1. Read your gas meter and save it.
  2. After at least 5 burner-hours (a few days of heating), save the new reading.
  3. The factor `real m³ ÷ estimated m³` is applied from then on. Saving again later re-calibrates from the previous reading.

  The meter also counts hot water and any gas cooker, so calibrated numbers include those.

---

## Alerts

| Alert | When | What to check |
|---|---|---|
| **Boiler may not be heating** | Burner on for 60 min continuously, the model expected ≥ 0.4 °C rise, but the room rose less than a quarter of that (or fell 0.5 °C) | Boiler display for an error code, water pressure (usually 1–2 bar), gas supply |
| **Room sensor offline** | No data for 10 minutes (heating is paused) | Sensor power and Wi-Fi |

Alerts show as a banner on the dashboard and are sent as **Sinric Pro push notifications**. A notification is sent again if the boiler problem persists for 6 hours, and when things recover. Set `NOTIFY_IN_TURKISH` to `true` for Turkish notifications.

---

## Google Home (Sinric Pro)

1. Create a **Thermostat** device in the [Sinric Pro](https://sinric.pro) portal and put its ID in `THERMOSTAT_ID`.
2. In the Google Home app: *Add → Works with Google → Sinric Pro*, and sign in.

What Google Home sees:
- **Modes:** Heat and Off. Other modes (Cool, Auto, Eco) are rejected with a message. Eco is a local dashboard mode, and Google shows it as Heat.
- **"Turn on"** restores the last mode you used (Heat or Eco).
- **Target temperature** (5–30 °C; out-of-range values are clamped and the applied value is reported back).
- **Room temperature and humidity**, the real measurement, sent on change and at least every 10 minutes.

The boiler controller is the source of truth. Its settings are stored in flash, and on every reconnect it pushes its current target and mode to Sinric Pro. On the very first boot (nothing stored yet) it takes the last state from the cloud instead. You can schedule temperatures with Google Home automations, for example "18 °C at 23:00, 21 °C at 06:30".

The room sensor also reports to its own Sinric Pro temperature-sensor device (`SENSOR_ID`). That device is optional now that the thermostat reports temperature and humidity itself.

---

## The dashboard

Open **http://boiler.local** (or the boiler's IP).

| Tab | What's there |
|---|---|
| **Home** | Big dial with the room temperature, target −/+, time to target, Heat / Eco / Off, humidity, feels-like, outdoor + forecast, today's gas, and "What's happening" (the reason, burner share, weather base load) |
| **Energy** | Gas today / yesterday / 7 days, a 7-day chart, and gas-meter calibration |
| **Insights** | Learned heat-up speed and heat loss, cooling rate, temperature trend, learning progress, weather-aware heating and the boiler watchdog |
| **Device** | Network, firmware, uptime, memory, clock, counters for the controller and the room sensor, and a link to the update page |

Language (EN / TR) and the last tab are remembered per browser. The page needs no internet (no external fonts or scripts).

---

## Hardware

**Boiler controller:** ESP8266 with 1 MB flash on a serial relay board (LC-Technology style, 9600 baud; `A0 01 01 A2` = ON, `A0 01 00 A1` = OFF). The relay's dry contact is wired to the boiler's external room-thermostat input.

**Room sensor:** ESP-01 (1 MB) with a DHT11 on GPIO2. It pushes readings every 30 seconds, and polls faster (every 2 s) when the temperature changes.

> The DHT11 is the weakest part: about ±2 °C absolute accuracy. The control loop cares about *changes*, which the DHT11 resolves at 0.1 °C. Absolute accuracy can be corrected with the sensor's offset (below), or improved by switching to an SHT31/SHT4x.

---

## Building and flashing

**Toolchain:** ESP8266 Arduino core **3.1.2**. Libraries: `SinricPro`, `ArduinoJson` (v7), `WebSockets`, `DHT sensor library`, `Adafruit Unified Sensor`.

**Configuration:** edit the `#define`s at the top of each sketch: Wi-Fi, Sinric Pro `APP_KEY` / `APP_SECRET` / device IDs, OpenWeatherMap key and your coordinates.

> ⚠️ These are compiled into the firmware. Don't commit real credentials to a public repository. If they were ever pushed, change them (Wi-Fi password, Sinric Pro app secret, OpenWeatherMap key).

**Board:** Generic ESP8266, 1 MB flash with 64 KB FS:

```bash
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=1M64 esp8266_boiler
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=1M64 esp8266_sensor
arduino-cli compile --fqbn esp8266:esp8266:generic:eesz=1M64 tiny_ota_updater
```

### First flash
Flash each board once over USB/serial.

### Updates over the air (two-stage)
A 1 MB board can't hold the running firmware and a new copy at the same time, so updates go through a tiny updater:

1. Open `http://boiler.local/update` (or `http://sensor.local/update`) and upload **`tiny_ota_<version>.bin`**. The board reboots into the minimal updater.
2. Open `http://ota-updater.local/update` and upload **`boiler_<version>.bin`** (or `sensor_…`). The board boots into the new firmware.

Settings, gas history and the learned model are kept in the EEPROM area and survive both steps.

> **Size limit:** with the tiny updater running there is about 618 KB of space for the new firmware. The boiler binary is about 590 KB, so keep an eye on growth when adding features.

---

## HTTP API

### Boiler controller (`http://boiler.local`)

| Method | Path | Description |
|---|---|---|
| GET | `/` | Dashboard |
| GET | `/api/status` | Everything as JSON: `system`, `time`, `env`, `time_to_target`, `model`, `gas`, `alerts`, `limits`, `network`, `hardware`, `diagnostics`, `sensor` |
| POST | `/api/set?target=21.5` | Set the target (5–30 °C, `.` or `,` decimal) |
| POST | `/api/set?mode=HEAT\|ECO\|OFF` | Set the mode |
| POST | `/api/gas?meter=12345.678` | Gas-meter calibration reading (first call starts, next call calibrates) |
| POST | `/api/gas?reset=1` | Reset the gas calibration to ×1.0 |
| POST | `/api/alerts/clear` | Dismiss the boiler-fault alert |
| POST | `/sensor` | Used by the room sensor (JSON with `indoorTemp`, `indoorHum`, diagnostics) |
| GET/POST | `/update` | Web OTA |

`system.reason` is a code such as `heating_full`, `pwm_on`, `pwm_off`, `satisfied`, `min_off_wait`, `min_on_hold`, `off`, `sensor_timeout` or `waiting_sensor`.

### Room sensor (`http://sensor.local`)

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | Readings, calibration offset, Wi-Fi and DHT statistics |
| GET/POST | `/api/set?offset=-1.0` | Temperature calibration offset (−15…+15 °C, saved to flash) |
| GET/POST | `/update` | Web OTA |

---

## Tuning

The main constants are at the top of `esp8266_boiler.ino`:

| Constant | Default | Meaning |
|---|---|---|
| `PWM_PERIOD_MS` | 20 min | Control cycle length |
| `MIN_ON_TIME_MS` / `MIN_OFF_TIME_MS` | 3 / 5 min | Boiler protection |
| `BAND_HEAT_C` / `BAND_ECO_C` | 0.5 / 1.0 °C | Error that changes the burner share by 100% |
| `ECO_SETBACK_C` / `ECO_MAX_DUTY` | 1.0 °C / 50% | Eco behaviour |
| `CONTROL_SMOOTH_S` | 180 s | Smoothing of the control temperature |
| `RADIATOR_LAG_MIN` | 15 min | Radiator warm-up / cool-down time constant |
| `DEFAULT_HEAT_RATE` / `DEFAULT_LOSS_K` | 2.5 °C/h / 0.08 /h | Starting model before learning |
| `MODEL_FORGET` | 0.98 per hour | ≈ 2-day learning memory |
| `GAS_RATE_M3_HR` | 2.6 m³/h | Boiler gas use at full load |
| `SENSOR_TIMEOUT_MS` | 10 min | Safety cut-off |
| `FAULT_CHECK_MIN` | 60 min | Boiler watchdog check time |
| `TIMEZONE` | `TZ_Europe_Istanbul` | Local time for day boundaries |
| `NOTIFY_IN_TURKISH` | `false` | Push notification language |

Flash wear: statistics are written at most every 30 minutes (≤ 48 writes/day) plus once at midnight. Settings are written 10 seconds after a change.

---

## CI/CD

`.github/workflows/release.yml` builds `boiler_<tag>.bin`, `sensor_<tag>.bin` and `tiny_ota_<tag>.bin` when a GitHub **Release** is published. It injects the tag into `FIRMWARE_VERSION` and reuses the previous binary for sketches that didn't change.

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| "Waiting for the room sensor" | The sensor can't reach `boiler.local`. Check that both are on the same network and that mDNS works on your router. |
| "Clock not synced" | No internet / NTP blocked. Heating still works, but gas days won't roll over until the clock syncs. |
| Time to target says "can't reach" | In Eco, the 50% burner cap isn't enough at today's outdoor temperature. Use Heat. |
| Learning stays below 12 hours | Learning needs fresh weather data and an indoor–outdoor difference of at least 2 °C. It pauses in mild weather. |
| Google Home shows an old target | It updates when the controller reconnects to Sinric Pro. Check the Device tab and Wi-Fi. |
| OTA upload fails on stage 2 | The firmware is larger than the space beside the tiny updater (see the size limit above). |

---

## Project layout

```
esp8266_boiler/
  esp8266_boiler.ino   boiler controller: control, learning, gas, alerts, API, Sinric Pro
  dashboard.h          the web dashboard (HTML/CSS/JS, English + Turkish)
esp8266_sensor/
  esp8266_sensor.ino   room sensor (DHT11), pushes to the boiler every 30 s
tiny_ota_updater/
  tiny_ota_updater.ino minimal updater for two-stage OTA on 1 MB boards
.github/workflows/
  release.yml          builds release binaries
```
