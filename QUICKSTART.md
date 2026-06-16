# Multi-Layer Aquaponic System — Quickstart

Minimal copy-paste guide. For wiring diagrams, calibration steps, and troubleshooting see `INSTALL.md`. For architecture details see `Documentation.md`.

---

## ESP32 (from your laptop)

### 1. Install Libraries

**PlatformIO** — paste into terminal from the project root:
```bash
pio pkg install -l "me-no-dev/ESPAsyncWebServer"
pio pkg install -l "me-no-dev/AsyncTCP"
pio pkg install -l "claws/BH1750"
pio pkg install -l "paulstoffregen/OneWire"
pio pkg install -l "milesburton/DallasTemperature"
pio pkg install -l "knolleary/PubSubClient"
```

**Arduino IDE** — Sketch → Include Library → Manage Libraries, search and install each:
- `ESPAsyncWebServer` (me-no-dev)
- `AsyncTCP` (me-no-dev)
- `BH1750` (Christopher Laws)
- `OneWire` (Jim Studt)
- `DallasTemperature` (Miles Burton)
- `PubSubClient` (Nick O'Leary)

### 2. Edit Config

Open `Compilation.ino`. Change only these lines:

```cpp
#define WIFI_SSID   "YourWiFiName"         // ← your 2.4 GHz WiFi
#define WIFI_PASS   "YourWiFiPassword"     // ← your WiFi password
#define MQTT_BROKER "192.168.1.xxx"        // ← Raspberry Pi IP address
```

### 3. Flash

```bash
# PlatformIO
pio run -t upload -t monitor

# Arduino IDE
# Board:   ESP32 Dev Module
# Port:    (select your board)
# Click Upload →
```

Serial output after boot:
```
IP: 192.168.1.42
```

Open **`http://192.168.1.42/`** — the dashboard should load. Sensor readings will show defaults until probes are connected and calibrated.

---

## Raspberry Pi (SSH into it)

### Step 1 — Mosquitto MQTT Broker

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

Verify:
```bash
mosquitto_sub -t 'aquaponic/data' -v
# Leave running — you should see JSON arriving every 2 seconds
```

### Step 2 — InfluxDB v2

```bash
# Add repo and install
wget -q https://repos.influxdata.com/influxdata-archive_compat.key
echo '393e8779c89ac8d958f81dee4b1c2f2f6c9eb0e influxdata-archive_compat.key' | sha256sum -c
cat influxdata-archive_compat.key | gpg --dearmor | sudo tee /etc/apt/trusted.gpg.d/influxdata-archive_compat.gpg > /dev/null
echo 'deb [signed-by=/etc/apt/trusted.gpg.d/influxdata-archive_compat.gpg] https://repos.influxdata.com/debian stable main' | sudo tee /etc/apt/sources.list.d/influxdata.list
sudo apt update && sudo apt install -y influxdb2
sudo systemctl enable --now influxdb
```

Open **`http://<pi-ip>:8086`** in a browser:

1. Click **Get Started**
2. Username: `admin`, password: (pick one, save it)
3. Organisation name: `aquaponic`
4. Bucket name: `sensor_data`
5. Go to **API Tokens → Generate Token → Custom API Token**
   - Read + Write
   - Bucket: `sensor_data`
   - Copy the token — you'll use it next

### Step 3 — Python Bridge (MQTT → InfluxDB)

```bash
pip3 install paho-mqtt influxdb-client
mkdir -p ~/aquaponic-bridge
```

Copy the bridge script and service file from the repo:
```bash
cp ~/Micro/pi/bridge.py ~/aquaponic-bridge/
sudo cp ~/Micro/pi/aquaponic-bridge.service /etc/systemd/system/
```

Set your token (from Step 2):
```bash
sudo nano /etc/systemd/system/aquaponic-bridge.service
# Find: Environment=INFLUX_TOKEN=your-influxdb-token-here
# Replace with your actual token
# Ctrl+O, Enter, Ctrl+X

sudo systemctl daemon-reload
sudo systemctl enable --now aquaponic-bridge
sudo systemctl status aquaponic-bridge
# Should show "active (running)" with green dot
```

Verify data is flowing:
```bash
sudo journalctl -u aquaponic-bridge -f
# Should see: "Bridge started — MQTT=localhost:1883 ..."
```

### Step 4 — Grafana

```bash
sudo apt install -y grafana
sudo systemctl enable --now grafana-server
```

Open **`http://<pi-ip>:3000`**:

1. Login: `admin` / `admin` (change password when prompted)
2. **Connections → Data sources → Add data source → InfluxDB**
3. Query language: **Flux**
4. URL: `http://localhost:8086`
5. Organisation: `aquaponic`
6. Token: paste the same token from Step 2
7. Default bucket: `sensor_data`
8. Click **Save & test** — should show green ✓

Create your first panel:
- **Dashboards → New → Add visualization**
- Paste this Flux query and click **Submit**:

```
from(bucket: "sensor_data")
  |> range(start: -1h)
  |> filter(fn: (r) => r._field == "tds")
  |> aggregateWindow(every: 10s, fn: mean)
```

- Choose **Gauge** or **Time series** from the panel type selector
- Click **Apply**

Repeat for other fields: `ph`, `temp`, `lux`, `aq_ppm`, `water_level`, `gsm_volt`.

---

## Verify Everything Works

| Check | Command / URL | Expected |
|-------|---------------|----------|
| ESP32 dashboard | `http://<esp32-ip>/` | All cards visible, values updating |
| MQTT data | `mosquitto_sub -t 'aquaponic/data' -v` | JSON appears every 2s |
| InfluxDB has data | `influx query --org aquaponic --token <token> 'from(bucket:"sensor_data") \|> range(start: -5m) \|> limit(n: 1)'` | Returns a row |
| Bridge running | `sudo systemctl status aquaponic-bridge` | active (running) |
| Grafana panels | `http://<pi-ip>:3000/` | Graphs updating every 2s |

---

## Next Steps

- Calibrate sensors — follow `INSTALL.md` §5
- Wire relays — follow `INSTALL.md` §2
- Insert SIM + antenna for SMS alerts
- Build your Grafana dashboard layout to match the ESP32 dashboard

## Quick Reference

```
ESP32 web:      http://<esp32-ip>/
ESP32 JSON:     http://<esp32-ip>/data
MQTT broker:    <pi-ip>:1883
InfluxDB UI:    http://<pi-ip>:8086/
Grafana:        http://<pi-ip>:3000/      (admin / admin)
Bridge logs:    sudo journalctl -u aquaponic-bridge -f
```
