# Multi-Layer Aquaponic System — Installation Guide

This guide covers end-to-end setup: hardware assembly, ESP32 flashing, Raspberry Pi services, sensor calibration, and verification.

---

## 1. Prerequisites

### 1.1 Hardware

| Component            | Qty | Notes                                      |
|----------------------|-----|--------------------------------------------|
| ESP32 Dev Board      | 1   | 30-pin or 38-pin; 16 MB flash recommended  |
| Raspberry Pi 4/5     | 1   | Pi 3 works; 2 GB RAM minimum for Grafana   |
| SIM800L GSM Module   | 1   | Requires 3.7–4.2V @ 2A (LiPo or LM2596)   |
| SIM card             | 1   | 2G-capable, SMS plan active                |
| MQ135 gas sensor     | 1   | With breakout board (5V heater)            |
| TDS probe + module   | 1   | Analog output, 0–3.3V range                |
| pH probe + module    | 1   | Analog output, 0–3.3V range                |
| DS18B20 temp sensor  | 1   | Waterproof version with 4.7kΩ pull-up     |
| BH1750 light sensor  | 1   | I²C breakout (GY-302)                      |
| AJ-SR04M ultrasonic  | 1   | Waterproof; 5V tolerant with divider       |
| 5-channel relay module| 1  | Active LOW; 5V coils, 3.3V logic           |
| Resistors             | 1   | 2.2 kΩ × 3, 1 kΩ × 2, 4.7 kΩ × 1          |
| Breadboard / PCB     | 1   | + jumper wires, screw terminals            |
| Power supply         | 1   | 5V 3A for ESP32 + relays; LiPo for GSM     |

### 1.2 Software (your laptop)

- **PlatformIO** (recommended) — `pip install platformio` — or Arduino IDE 2.x
- ESP32 board package (PlatformIO auto-installs; Arduino: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`)
- USB cable for ESP32 flashing

### 1.3 Tools

- Multimeter (for voltage checks)
- Small flathead screwdriver (relay terminals)
- Label maker or masking tape (pin labelling)

---

## 2. Hardware Assembly

### 2.1 Voltage Dividers (critical — do not skip)

Three sensors output 0–5V and must be scaled to 0–3.3V for the ESP32 ADC:

**pH probe signal (GPIO35)**
```
pH AO ──[1kΩ]──┬── GPIO35
               │
              [2.2kΩ]
               │
               GND
```

**MQ135 output (GPIO32)**
```
MQ135 AO ──[2.2kΩ]──┬── GPIO32
                    │
                   [1kΩ]
                    │
                    GND
```

**Ultrasonic ECHO (GPIO18)**
```
AJ-SR04M ECHO ──[2.2kΩ]──┬── GPIO18
                         │
                        [1kΩ]
                         │
                         GND
```

### 2.2 Full Wiring Table

```
ESP32 Pin     Connects To                     Wire Colour (suggested)
─────────     ────────────                    ──────────────────────
GPIO4   ──→   DS18B20 DATA (yellow)           yellow
3.3V    ──→   DS18B20 VCC                     red
GND     ──→   DS18B20 GND                     black
─→       4.7kΩ between DATA and VCC

GPIO5   ──→   AJ-SR04M TRIG                  orange
GPIO18  ──→   AJ-SR04M ECHO (via divider)    green
5V      ──→   AJ-SR04M VCC                    red
GND     ──→   AJ-SR04M GND                    black

GPIO16  ──→   SIM800L RXD                     white
GPIO17  ──→   SIM800L TXD                     grey
GND     ──→   SIM800L GND                     black
LiPo+   ──→   SIM800L VCC (3.7–4.2V)          red

GPIO21  ──→   BH1750 SDA                      blue
GPIO22  ──→   BH1750 SCL                      purple
3.3V    ──→   BH1750 VCC                      red
GND     ──→   BH1750 GND                      black

GPIO32  ──→   MQ135 AO (via divider)          brown
5V      ──→   MQ135 VCC                        red
GND     ──→   MQ135 GND                       black

GPIO34  ──→   TDS probe AO                    cyan
3.3V    ──→   TDS module VCC                  red
GND     ──→   TDS module GND                  black

GPIO35  ──→   pH probe AO (via divider)       pink
3.3V    ──→   pH module VCC                   red
GND     ──→   pH module GND                   black

GPIO19  ──→   Relay-1 IN (Temp control)       orange
GPIO23  ──→   Relay-2 IN (Water fill pump)    yellow
GPIO25  ──→   Relay-3 IN (Air quality)        green
GPIO26  ──→   Relay-4 IN (Flush pump)         blue
GPIO27  ──→   Relay-5 IN (UV lamp)            purple
5V      ──→   Relay module VCC (JD-VCC)       red
GND     ──→   Relay module GND                black

All GNDs tied together (ESP32 ground = sensor ground = relay ground).
```

### 2.3 Power Notes

- **SIM800L**: use a dedicated 3.7–4.2V LiPo battery or an LM2596 buck converter from 5V. The ESP32's 3.3V regulator **cannot** supply the 2A burst the module needs during transmission. Using 3.3V will cause the module to brown-out and fail to register on the network.
- **Relay module**: use the 5V rail from the ESP32's USB supply (or external 5V PSU). Set the jumper to **JD-VCC** (isolated relay power) if using separate supplies.
- **MQ135 heater**: runs on 5V. Allow at least 24 hours burn-in before calibrating.

---

## 3. ESP32 Firmware

### 3.1 Get the Code

```bash
git clone <your-repo-url> ~/Micro
cd ~/Micro
```

### 3.2 Configure Credentials

Edit `Compilation.ino`:

```cpp
#define WIFI_SSID  "your-wifi-name"       // 2.4 GHz only
#define WIFI_PASS  "your-wifi-password"

#define MQTT_BROKER  "192.168.1.xxx"       // Raspberry Pi IP address
const char RECIPIENT[] = "+639123456789";  // SMS alert recipient
```

### 3.3 Library Dependencies

**PlatformIO** (create `platformio.ini`):

```bash
cat > platformio.ini << 'EOF'
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    me-no-dev/ESPAsyncWebServer @ ^1.2.3
    me-no-dev/AsyncTCP @ ^1.1.1
    claws/BH1750 @ ^1.3.0
    paulstoffregen/OneWire @ ^2.3.7
    milesburton/DallasTemperature @ ^3.11.0
    knolleary/PubSubClient @ ^2.8
EOF
pio run
```

**Arduino IDE** (Library Manager, Sketch → Include Library → Manage Libraries):
- `ESPAsyncWebServer` by me-no-dev
- `AsyncTCP` by me-no-dev
- `BH1750` by Christopher Laws
- `OneWire` by Jim Studt
- `DallasTemperature` by Miles Burton
- `PubSubClient` by Nick O'Leary

### 3.4 Flash

```bash
# PlatformIO
pio run -t upload -t monitor

# Arduino IDE
# Board:   ESP32 Dev Module
# Speed:   921600
# Scheme:  Default 4MB with spiffs
```

After boot, the Serial Monitor shows:

```
IP: 192.168.x.y
```

Open `http://192.168.x.y/` — the dashboard should appear. Sensor values will be defaults or zero until probes are connected and calibrated.

---

## 4. Raspberry Pi Setup

Do each step **in order**. Each step depends on the previous one.

### 4.1 Prepare the Pi

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Set static IP (so ESP32 always finds the broker)
sudo nmtui    # or edit /etc/dhcpcd.conf

# Allow MQTT port through firewall (if enabled)
sudo ufw allow 1883/tcp
sudo ufw allow 8086/tcp   # InfluxDB
sudo ufw allow 3000/tcp   # Grafana
```

### 4.2 Mosquitto MQTT Broker

```bash
sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

Test it:

```bash
# Terminal 1 — subscribe
mosquitto_sub -t 'aquaponic/#' -v

# Terminal 2 — publish a test
mosquitto_pub -t 'aquaponic/data' -m '{"tds":0}' -r
```

If you see `{"tds":0}` in terminal 1, the broker works.

### 4.3 InfluxDB v2

```bash
# Import the InfluxData repository
wget -q https://repos.influxdata.com/influxdata-archive_compat.key
echo '393e8779c89ac8d958f81dee4b1c2f2f6c9eb0e influxdata-archive_compat.key' | sha256sum -c
cat influxdata-archive_compat.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/influxdata-archive_compat.gpg > /dev/null
echo 'deb [signed-by=/etc/apt/trusted.gpg.d/influxdata-archive_compat.gpg] https://repos.influxdata.com/debian stable main' | sudo tee /etc/apt/sources.list.d/influxdata.list

sudo apt update && sudo apt install -y influxdb2
sudo systemctl enable --now influxdb
```

Open `http://<pi-ip>:8086` in a browser:

1. **Get Started** → set username `admin`, password (save it)
2. **Organisation**: `aquaponic`
3. **Bucket**: `sensor_data`, retention → pick one (30 days recommended)
4. Go to **API Tokens** → Generate Token → Read/Write → call it `bridge-token`
5. **Copy the token** — you'll need it in step 4.4

Verify with the CLI:

```bash
influx bucket list --org aquaponic --token <token>
```

### 4.4 Python MQTT → InfluxDB Bridge

```bash
# Install Python dependencies
pip3 install paho-mqtt influxdb-client

# Create working directory
mkdir -p ~/aquaponic-bridge

# Copy the bridge script from the repo
cp ~/Micro/pi/bridge.py ~/aquaponic-bridge/
```

Set the InfluxDB token:

```bash
export INFLUX_TOKEN='your-bridge-token-here'

# Test run (Ctrl+C to stop after confirming it connects)
python3 ~/aquaponic-bridge/bridge.py
```

Expected output:

```
2026-01-01 12:00:00 [INFO] MQTT connected to localhost:1883
2026-01-01 12:00:00 [INFO] Subscribed to aquaponic/data
2026-01-01 12:00:00 [INFO] Bridge started — MQTT=localhost:1883  InfluxDB=http://localhost:8086  bucket=sensor_data
```

Install as a systemd service for auto-start:

```bash
# Copy the service file
sudo cp ~/Micro/pi/aquaponic-bridge.service /etc/systemd/system/

# Edit the service to set your INFLUX_TOKEN
sudo nano /etc/systemd/system/aquaponic-bridge.service
# Change: Environment=INFLUX_TOKEN=your-influxdb-token-here
# Save and exit (Ctrl+X, Y, Enter)

# Also update the WorkingDirectory and ExecStart to match your layout
# Default: WorkingDirectory=/home/pi/aquaponic-bridge
#          ExecStart=/usr/bin/python3 /home/pi/aquaponic-bridge/bridge.py

sudo systemctl daemon-reload
sudo systemctl enable --now aquaponic-bridge
sudo systemctl status aquaponic-bridge
```

### 4.5 Grafana

```bash
sudo apt install -y grafana
sudo systemctl enable --now grafana-server
```

Open `http://<pi-ip>:3000`:

1. Login: `admin` / `admin` (change password when prompted)
2. **Connections → Data Sources → Add data source → InfluxDB**
3. Query language: **Flux**
4. URL: `http://localhost:8086`
5. Organisation: `aquaponic`
6. Token: paste your bridge token
7. Default bucket: `sensor_data`
8. Click **Save & Test** — should show green "OK"

#### Create a Dashboard

**Dashboards → New → Add visualization** → select InfluxDB data source.

Example flux query for a TDS time-series panel:

```
from(bucket: "sensor_data")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "sensor_data")
  |> filter(fn: (r) => r._field == "tds")
  |> aggregateWindow(every: 10s, fn: mean)
```

Recommended panels (replicate your ESP32 dashboard):

| Panel       | Query field    | Type         | Unit  |
|-------------|---------------|--------------|-------|
| TDS         | `tds`         | Gauge        | ppm   |
| pH          | `ph`          | Gauge        | —     |
| Temp        | `temp`        | Gauge        | °C    |
| Air Quality | `aq_ppm`      | Time series  | ppm   |
| Air Quality %| `aq_pct`      | Gauge        | %     |
| Water Level | `water_level` | Gauge        | cm    |
| Lux         | `lux`         | Gauge        | lux   |
| Pump        | `pump`        | Stat         | on/off |
| UV Lamp     | `uv`          | Stat         | on/off |
| Air Relay   | `air_relay`   | Stat         | on/off |
| Water Pump  | `water_pump`  | Stat         | on/off |
| Temp Relay  | `temp_relay`  | Stat         | on/off |
| GSM RSSI    | `gsm_rssi`    | Gauge        | —     |
| GSM Voltage | `gsm_volt`    | Time series  | mV    |
| SMS Sent    | `sms_sent`    | Stat         | count |

---

## 5. Sensor Calibration

### 5.1 pH Probe

1. Remove the probe from the tank. Rinse with distilled water.
2. Submerge in **pH 4.0 buffer solution**. Wait 30 seconds for the reading to stabilise.
3. Note the displayed pH value. If it reads 3.8 instead of 4.0:
   - Adjust `PH_OFFSET` in `Compilation.ino` up by 0.2.
4. Submerge in **pH 7.0 buffer**. Wait 30 seconds.
5. If both 4.0 and 7.0 readings are off linearly, adjust `PH_SLOPE`:
   ```
   PH_SLOPE = (7.0 - 4.0) / (vADC_7 - vADC_4)
   ```
   where vADC values can be obtained by temporarily uncommenting a Serial print in `loop()` Timer 5 to log raw ADC.
6. Re-flash and verify both buffer readings are within ±0.1 pH.

### 5.2 TDS Probe

1. Obtain a **known-conductivity calibration solution** (e.g., 1413 µS/cm = ~700 ppm NaCl).
2. Submerge the probe. Wait for the reading to stabilise.
3. If the reading is off proportionally, adjust `VREF` in `Compilation.ino`:
   ```
   VREF_corrected = VREF * (displayed_ppm / actual_ppm)
   ```
   e.g., if it reads 650 ppm for a 700 ppm standard: `VREF = 3.3 * (650 / 700) = 3.06`
4. Re-flash and re-test.

### 5.3 MQ135 Air Quality

The MQ135 needs at **least 24 hours of continuous burn-in** before calibrating (the heater must stabilise the sensing element).

1. After burn-in, place the sensor in **fresh outdoor air** (~400 ppm CO₂).
2. Let it run for 30 minutes until the `aq_ppm` reading on the dashboard stabilises.
3. Note the average reading.
4. Set `MQ135_RO` in `Compilation.ino` to the current stable Rs value. If you cannot measure Rs directly, use:
   ```
   MQ135_RO_corrected = MQ135_RO * (measured_ppm / 400.0)
   ```
5. Re-flash. The sensor should now read ~400 ppm in clean air.
6. Tune `AQ_GOOD`, `AQ_MODERATE`, `AQ_POOR`, `AQ_BAD` thresholds based on readings in your actual environment.

### 5.4 Ultrasonic Water Level

1. Measure the actual distance from the sensor face to the **tank bottom** in cm.
2. Update `SENSOR_HEIGHT_CM` in `Compilation.ino`.
3. Set `TARGET_LEVEL_CM` to your desired water level (distance from bottom).
4. Empty the tank and fill to a known level. Verify the dashboard reading matches.

---

## 6. Verification Checklist

After completing all steps, verify each subsystem:

| Check                                                      | Expected result                     |
|------------------------------------------------------------|-------------------------------------|
| Open ESP32 IP in browser                                  | Dashboard loads, all cards visible  |
| ESP32 `/data` JSON contains all 20 fields                  | `curl http://<esp32-ip>/data`       |
| `mosquitto_sub -t 'aquaponic/#' -v` shows JSON every 2s   | MQTT data flowing                   |
| `influx query 'from(bucket:"sensor_data") ...'` returns data | Points written to bucket         |
| Grafana dashboard at `http://<pi-ip>:3000` shows live data | Graphs/gauge update every 2s       |
| Blow on MQ135 sensor                                      | aq_ppm rises within 30s             |
| Cover BH1750 with hand                                    | lux drops, UV relay turns ON        |
| Move hand in front of ultrasonic sensor                   | water_level changes                 |
| SMS arrives when pH probe is removed from water           | Phone receives pH alert             |
| Disconnect ESP32 power                                    | `aquaponic/status` → "offline"      |

---

## 7. Troubleshooting

### ESP32

| Symptom                        | Fix                                               |
|--------------------------------|---------------------------------------------------|
| Won't flash                    | Hold BOOT button while plugging in; try 115200 baud |
| Serial shows no IP             | WiFi is 2.4 GHz only; check SSID/password         |
| Dashboard 404                  | Check IP matches Serial output                    |
| All sensors read zero          | Check ADC attenuation (ADC_11db); verify GND shared |
| MQ135 reads millions           | Warm up 24h; check voltage divider resistor values |
| MQTT not publishing            | Verify `MQTT_BROKER` IP; check Pi firewall (port 1883) |
| GSM never ready                | LiPo voltage ≥ 3.7V; antenna connected; 2G coverage |
| SMS not sending                | SIM has credit; `AT+CMGF=1` supported              |
| Random reboots                 | Power supply inadequate; add capacitor on 5V rail  |

### Raspberry Pi

| Symptom                        | Fix                                               |
|--------------------------------|---------------------------------------------------|
| mosquitto_sub shows nothing    | Broker running? `sudo systemctl status mosquitto` |
| bridge.py: connection refused  | Mosquitto not started or wrong port               |
| bridge.py: unauthorized (401)  | Wrong INFLUX_TOKEN; regenerate in InfluxDB UI     |
| bridge.py: bucket not found    | Create bucket `sensor_data` in InfluxDB UI        |
| Grafana "Bad Gateway"          | `sudo systemctl restart grafana-server`            |
| Grafana shows no data          | Check bridge.py is running; verify token in `env` |

### Dashboard

| Symptom                        | Fix                                               |
|--------------------------------|---------------------------------------------------|
| "Connection lost"              | ESP32 WiFi dropped; check RSSI                    |
| Cards don't update             | Browser cache; hard-refresh (Ctrl+Shift+R)        |
| Water level shows "Sensor error" | Ultrasonic no echo; sensor too close to water (< 2cm) |

---

## 8. Maintenance

- **Weekly**: Check relay terminals for corrosion (humid environment)
- **Monthly**: Verify pH calibration with buffer solution; clean probe tip
- **Quarterly**: Replace SIM800L antenna if corroded; check LiPo battery health
- **After power outage**: Grafana and InfluxDB auto-start via systemd; ESP32 reconnects WiFi + MQTT automatically

---

## Appendix A: Quick Reference

### Commands

```bash
# ESP32
pio run -t upload -t monitor           # Flash + serial monitor

# Raspberry Pi
sudo systemctl status mosquitto         # Check MQTT broker
sudo systemctl status influxdb          # Check database
sudo systemctl status aquaponic-bridge  # Check bridge script
sudo systemctl status grafana-server    # Check dashboard
sudo journalctl -u aquaponic-bridge -f  # Follow bridge logs

# Test MQTT
mosquitto_sub -t 'aquaponic/#' -v       # Watch all topics

# Test InfluxDB
influx query --org aquaponic --token <token> \
  'from(bucket:"sensor_data") |> range(start: -5m) |> limit(n: 1)'

# Test ESP32 HTTP
curl http://<esp32-ip>/data | python3 -m json.tool
```

### Default Credentials

| Service    | URL                      | User    | Password (change immediately) |
|------------|--------------------------|---------|-------------------------------|
| ESP32 Web  | `http://<esp32-ip>/`     | —       | —                             |
| InfluxDB   | `http://<pi-ip>:8086/`   | admin   | (set during setup)            |
| Grafana    | `http://<pi-ip>:3000/`   | admin   | admin                         |

### File Locations on Pi

```
/home/pi/aquaponic-bridge/bridge.py          # MQTT → InfluxDB script
/etc/systemd/system/aquaponic-bridge.service # systemd unit
/etc/mosquitto/mosquitto.conf                # MQTT broker config
/etc/influxdb/config.toml                    # InfluxDB config
/etc/grafana/grafana.ini                     # Grafana config
```

---

## Appendix B: Port Map

```
ESP32 (192.168.x.y)
  :80     HTTP dashboard (fallback)

Raspberry Pi (192.168.x.z)
  :1883   Mosquitto MQTT
  :8086   InfluxDB HTTP API + UI
  :3000   Grafana web UI
```
