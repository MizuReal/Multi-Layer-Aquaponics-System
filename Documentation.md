# Multi-Layer Aquaponic System — Documentation

## 1. System Overview

ESP32-based monitoring and control system for a multi-layer aquaponic setup housing crayfish and zebra danios. The firmware reads water quality, air quality, and water level sensors; controls five actuators via relays; sends SMS alerts for critical pH deviations; and serves a real-time web dashboard.

### 1.1 Monitored Parameters

| Parameter         | Sensor      | Unit  |
|-------------------|-------------|-------|
| TDS               | Analog probe| ppm   |
| pH                | Analog probe| —     |
| Water Temperature | DS18B20     | °C    |
| Light Intensity   | BH1750      | lux   |
| Air Quality (CO₂) | MQ135       | ppm   |
| Water Level       | AJ-SR04M    | cm    |

### 1.2 Actuators

| Actuator          | GPIO | Trigger Condition          |
|-------------------|------|----------------------------|
| Flush pump        | 26   | TDS ≥ 300 ppm              |
| UV lamp           | 27   | Lux ≤ 10                   |
| Air quality relay | 25   | MQ135 ≥ 1000 ppm (POOR)    |
| Water fill pump   | 23   | Water level < 10 cm        |
| Temp control relay| 19   | ON at 30°C / OFF at 28°C   |

### 1.3 Alerts

GSM (SIM800L) sends SMS to a configured number when pH falls outside the 6.0–8.0 range. Alerts repeat at most once every 10 minutes.

---

## 2. Hardware Pin Mapping

```
ESP32 GPIO     Function              Notes
──────────     ────────              ─────
GPIO4          DS18B20 data          4.7 kΩ pull-up to 3.3V
GPIO5          Ultrasonic TRIG       AJ-SR04M trigger
GPIO16         GSM TX                ESP32 TX → SIM800L RXD (UART2)
GPIO17         GSM RX                ESP32 RX ← SIM800L TXD (UART2)
GPIO18         Ultrasonic ECHO       AJ-SR04M echo (voltage divider: 2.2kΩ + 1kΩ, 5V→3.3V)
GPIO19         Temp control relay    Active LOW
GPIO21         I²C SDA               BH1750 light sensor
GPIO22         I²C SCL               BH1750 light sensor
GPIO23         Water fill pump relay Active LOW
GPIO25         Air quality relay     Active LOW
GPIO26         Flush pump relay      Active LOW
GPIO27         UV lamp relay         Active LOW
GPIO32 (ADC1)  MQ135 air quality     Voltage divider: AO→2.2kΩ→GPIO32→1kΩ→GND
GPIO34 (ADC1)  TDS probe             Input-only, no pull resistors
GPIO35 (ADC1)  pH probe              Voltage divider: AO→1kΩ→GPIO35→2.2kΩ→GND
```

All relays: **active LOW** (`RELAY_ON = LOW`, `RELAY_OFF = HIGH`).

---

## 3. Software Architecture

### 3.1 Non-blocking Timer Loop

Seven independent timers run in `loop()` without blocking the main thread:

| Timer | Period | Task                                             |
|-------|--------|--------------------------------------------------|
| 1     | 40 ms  | Sample TDS ADC → ring buffer                     |
| 2     | 800 ms | Compute TDS median, update flush pump relay      |
| 3     | 1200 ms| Read BH1750 lux, update UV relay                 |
| 4     | 2000 ms| Read DS18B20 async, update temp relay            |
| 5     | 1000 ms| Read pH ADC, evaluate GSM alert                  |
| 6     | 5000 ms| Read MQ135 ADC, compute air quality, update relay|
| 7     | 2000 ms| Read ultrasonic median distance, update water pump|

Timer 7 (ultrasonic) uses `pulseIn()` which blocks for up to ~38 ms per sample (×5 samples with 30 ms delays ≈ 310 ms total). This is acceptable within a 2 s window and does not conflict with the other timer intervals.

### 3.2 GSM Initialisation (auto-baud)

The GSM module is initialised once in `setup()` using the SIM800L auto-baud routine ported from `GSM800.ino`:

1. Scan baud rates 9600, 19200, 38400, 57600, 115200.
2. At each rate: three blind `AT` pings to sync the module, then one `AT` probe.
3. On first successful response → lock that baud rate.
4. Disable echo (`ATE0`), enable verbose errors (`AT+CMEE=2`).
5. Check battery voltage via `AT+CBC` (stored for dashboard).
6. Check SIM readiness via `AT+CPIN?`.
7. Wait up to 30 s for network registration (`AT+CREG?`, retries every 3 s).
8. Query signal quality (`AT+CSQ`).
9. `gsmReady` is true only when auto-baud succeeds **and** network registration succeeds.

### 3.3 pH Alert SMS Flow

1. pH read every 1000 ms.
2. If pH outside [6.0, 8.0] and GSM is ready:
   - First detection: send SMS immediately.
   - Continuous out-of-range: resend every `ALERT_INTERVAL` (10 min).
   - When pH returns to normal: reset interval timer.
3. SMS sending (`sendSMS`):
   - Set text mode (`AT+CMGF=1`) and GSM charset (`AT+CSCS="GSM"`).
   - Send `AT+CMGS="<number>"`, wait for `>` prompt (8 s timeout).
   - Send message body + `Ctrl+Z` (0x1A).
   - Wait for `+CMGS:` confirmation (15 s timeout).

### 3.4 Temperature Relay Hysteresis

From `TempSensor.ino` — relay ON when water temp ≥ 30°C, OFF when ≤ 28°C. Uses `setTempRelay()` which is no-op when state hasn't changed.

### 3.5 Ultrasonic Water Level

From `Ultrasonic.ino` — AJ-SR04M ultrasonic sensor with:
- Median filter (5 samples, bubble-sorted).
- `pulseIn()` timeout: 38 ms (range ~450 cm).
- Water level = `SENSOR_HEIGHT_CM` (20 cm) − measured distance.
- Water fill pump relay ON when water level < `TARGET_LEVEL_CM` (10 cm).

---

## 4. Web Dashboard

### 4.1 Endpoints

| Method | Path   | Response           |
|--------|--------|--------------------|
| GET    | /      | HTML dashboard     |
| GET    | /data  | JSON sensor data   |

### 4.2 JSON Fields (`/data`)

| Field         | Type   | Unit  | Description                                |
|---------------|--------|-------|--------------------------------------------|
| tds           | float  | ppm   | Total Dissolved Solids                     |
| ph            | float  | —     | pH level                                   |
| temp          | float  | °C    | Water temperature                          |
| lux           | float  | lux   | Light intensity                            |
| pump          | bool   | —     | Flush pump relay                           |
| uv            | bool   | —     | UV lamp relay                              |
| tds_quality   | string | —     | TDS label (Excellent/Good/Fair/Poor/Bad)   |
| gsm_ready     | bool   | —     | GSM module initialised                     |
| rssi          | int    | —     | GSM signal quality (0–31)                  |
| sms_sent      | int    | —     | Total SMS alerts sent since boot           |
| aq_ppm        | float  | ppm   | Air quality CO₂ equivalent                 |
| aq_pct        | float  | %     | Air quality percentage (100 = best)        |
| aq_label      | string | —     | Air quality label (GOOD/MODERATE/POOR/...) |
| air_relay     | bool   | —     | Air quality relay                          |
| temp_relay    | bool   | —     | Temperature control relay                  |
| sonar_dist    | float  | cm    | Distance from ultrasonic sensor to water   |
| water_level   | float  | cm    | Computed water level from tank bottom      |
| water_pump    | bool   | —     | Water fill pump relay                      |
| gsm_volt      | int    | mV    | GSM module supply voltage                  |

### 4.3 Dashboard Sections

- **Status bar**: quick-glance dot indicators for all 5 relays + GSM.
- **Water Quality**: TDS, pH, temperature, light intensity.
- **Water Level (Ultrasonic)**: sensor distance, computed water level with target comparison.
- **Actuators**: all 5 relay cards with trigger conditions.
- **Air Quality (MQ135)**: CO₂ ppm, percentage bar, label.
- **GSM / Alerts**: module status, RSSI bars, supply voltage, SMS count.

Auto-refresh every 2 seconds via `setInterval(fetchData, 2000)`.

---

## 5. Setup Instructions

1. **Power**: ESP32 via USB or 5V regulator. SIM800L requires 3.7–4.2 V @ 2 A (LiPo or LM2596 buck converter from 5 V).
2. **Wiring**: Follow pin mapping table in Section 2. Voltage dividers on pH and ultrasonic ECHO are mandatory.
3. **WiFi**: Update `WIFI_SSID` and `WIFI_PASS` at the top of `Compilation.ino`.
4. **GSM recipient**: Update `RECIPIENT` (line ~113) to the target phone number in international format.
5. **Calibration**:
   - pH: adjust `PH_SLOPE` and `PH_OFFSET` using known buffer solutions.
   - TDS: calibrate `VREF` against a known-conductivity solution.
   - MQ135: calibrate `MQ135_RO` in clean air (should read ~400 ppm).
6. **Libraries** (Arduino IDE / PlatformIO):
   - `WiFi.h` (built-in ESP32)
   - `ESPAsyncWebServer` (by me-no-dev / ESP32Async)
   - `AsyncTCP` (dependency of ESPAsyncWebServer)
   - `BH1750` (by Christopher Laws)
   - `OneWire` (by Jim Studt et al.)
   - `DallasTemperature` (by Miles Burton)

---

## 6. Build & Flash

```bash
# PlatformIO (recommended)
pio run -t upload -t monitor

# Arduino IDE
# Select board: ESP32 Dev Module
# Partition scheme: Default
# Upload Speed: 921600
```

After boot, the Serial Monitor prints only:

```
IP: <assigned-ip>
```

Open `http://<ip>/` in a browser to access the dashboard.

---

## 7. File Manifest

| File               | Purpose                                         |
|--------------------|-------------------------------------------------|
| `Compilation.ino`  | Main merged firmware (all sensors + actuators)  |
| `GSM800.ino`       | Standalone SIM800L debug/tester (reference)     |
| `TempSensor.ino`   | Standalone DS18B20 + relay tester (reference)   |
| `Ultrasonic.ino`   | Standalone AJ-SR04M water level tester (ref)    |
| `Documentation.md` | This document                                   |

---

## 8. Troubleshooting

| Symptom                      | Likely Cause                                  |
|------------------------------|-----------------------------------------------|
| GSM not ready                | Power < 3.7V, antenna missing, no 2G coverage |
| pH always 7.0                | Probe disconnected or voltage divider wrong   |
| TDS always 0                 | ADC pin floating, check wiring                |
| Ultrasonic shows "No echo"   | Sensor too close to water (<2 cm) or wiring   |
| Temp relay never triggers    | Threshold mismatch, check aquarium heater     |
| Dashboard not loading        | WiFi not connected, check IP in Serial Monitor|
| SMS not sending              | SIM not activated, no credit, or 2G only      |
