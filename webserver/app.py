#!/usr/bin/env python3
"""
Aquaponic Web Dashboard — Flask + MQTT
Polls /data every 2s on the frontend — no SSE complexity.

Start manually:   python3 app.py
Start as service: sudo systemctl enable --now aquaponic-web
"""

import json
import os
import threading

import paho.mqtt.client as mqtt
from flask import Flask, render_template, jsonify

# ── Configuration ─────────────────────────────────────────────────
MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT   = int(os.getenv("MQTT_PORT",   "1883"))
MQTT_TOPIC  = os.getenv("MQTT_TOPIC",  "aquaponic/data")
WEB_PORT    = int(os.getenv("WEB_PORT",  "5500"))

# ── Thread-safe state ────────────────────────────────────────────
latest_data = {}
lock = threading.Lock()

# ── Flask routes ──────────────────────────────────────────────────
app = Flask(__name__)

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/data")
def data():
    with lock:
        return jsonify(latest_data)

# ── MQTT subscriber (background thread) ──────────────────────────
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"MQTT connected → {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(MQTT_TOPIC)
        print(f"Subscribed → {MQTT_TOPIC}")
    else:
        print(f"MQTT connection failed (rc={rc})")

def on_message(client, userdata, msg):
    global latest_data
    try:
        data = json.loads(msg.payload.decode())
    except json.JSONDecodeError:
        return
    with lock:
        latest_data = data

def mqtt_loop():
    client = mqtt.Client(client_id="aquaponic_web")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
    client.loop_forever()

# ── Main ──────────────────────────────────────────────────────────
if __name__ == "__main__":
    threading.Thread(target=mqtt_loop, daemon=True).start()
    print(f"Dashboard → http://0.0.0.0:{WEB_PORT}")
    app.run(host="0.0.0.0", port=WEB_PORT, debug=False, threaded=True)
