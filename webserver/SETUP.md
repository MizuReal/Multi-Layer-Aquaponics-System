# Aquaponic Web Dashboard — Setup Guide

Self-hosted dashboard on Raspberry Pi. Reads MQTT directly — no InfluxDB or Grafana dependency. One Python file, one HTML file.

---

## 1. Install Dependencies

On the Pi:

```bash
pip3 install flask paho-mqtt --break-system-packages
```

## 2. Copy Files from Laptop

From your Fedora laptop:

```bash
scp -r ~/Micro/webserver sap2-ns@raspberrypi.local:~/
```

This copies the entire `webserver/` directory to the Pi's home.

If you're already SSH'd into the Pi, open a second terminal on Fedora and run the `scp` above. Or paste the files manually into `~/aquaponic-web/`.

## 3. Verify Files Exist

On the Pi:

```bash
ls ~/webserver/
# You should see: app.py  aquaponic-web.service  templates/

ls ~/webserver/templates/
# You should see: index.html
```

## 4. Test Run

```bash
cd ~/webserver
python3 app.py
```

Expected output:

```
MQTT: connected to localhost:1883
MQTT: subscribed to aquaponic/data
Web dashboard → http://0.0.0.0:5500
```

Open `http://<pi-ip>:5500/` in a browser. If the ESP32 is publishing, you'll see live data + charts.

Press `Ctrl+C` to stop the test run.

## 5. Install as Autostart Service

```bash
sudo cp ~/webserver/aquaponic-web.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now aquaponic-web
sudo systemctl status aquaponic-web
```

The status should show `active (running)` with a green dot.

## 6. Verify

Open `http://<pi-ip>:5500/` — the dashboard loads with:

| Section | Content |
|---------|---------|
| Header | Title + developers |
| Status bar | 5 relay dots (live) |
| Water Quality | TDS, pH, Temp, Lux — each with line chart (5 min window) |
| Water Level | Distance + level text + chart |
| Air Quality | CO₂ chart + percentage bar |
| Actuators | 5 relay cards with ON/OFF badges |
| GSM | Module status, RSSI, voltage, SMS count |

Auto-refresh every 2 seconds via SSE. No page reload needed.

## 7. File Locations on Pi

```
/home/sap2-ns/webserver/
├── app.py                        # Flask + MQTT thread + SSE
├── templates/
│   └── index.html                # Dashboard + Chart.js
└── aquaponic-web.service         # systemd unit (→ /etc/systemd/system/)

/etc/systemd/system/
└── aquaponic-web.service         # Autostart service
```

## 8. Commands Reference

```bash
# Start / stop / status
sudo systemctl start aquaponic-web
sudo systemctl stop aquaponic-web
sudo systemctl status aquaponic-web

# View logs
sudo journalctl -u aquaponic-web -f

# Restart after code changes
sudo systemctl restart aquaponic-web

# Test manually (stop service first)
sudo systemctl stop aquaponic-web
cd ~/webserver && python3 app.py
```

## 9. Port & Access

| URL | What |
|-----|------|
| `http://<pi-ip>:5500/` | Dashboard (SSE live) |
| `http://<pi-ip>:5500/data` | JSON snapshot (same as ESP32 `/data`) |
| `http://<esp32-ip>/` | ESP32 fallback dashboard |

## 10. Troubleshooting

| Symptom | Fix |
|---------|-----|
| "Connection lost — reconnecting" | ESP32 not publishing; check `mosquitto_sub -t 'aquaponic/data' -v` |
| Charts empty but values update | Browser cache; hard-refresh (Ctrl+Shift+R) |
| `pip3 install` fails | Use `--break-system-packages` flag |
| Service won't start | Check logs: `sudo journalctl -u aquaponic-web -f` |
| Port 5500 blocked | Firewall: `sudo ufw allow 5500/tcp` |
| Chart.js not loading | Pi needs internet for CDN; or download `chart.umd.min.js` locally |
