#!/usr/bin/env python3
"""
Aquaponic Web Dashboard — Flask + MQTT + Live MJPEG Video
Frontend polls /data every 2s. Video streamed as MJPEG from rpicam-vid.
Classification reads frames from the live video pipe — no second camera process.
"""

import json
import os
import subprocess
import threading

import paho.mqtt.client as mqtt
from flask import Flask, Response, render_template, jsonify

from classifier import get_counts, start_classifier

# ── Configuration ─────────────────────────────────────────────────
MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT   = int(os.getenv("MQTT_PORT",   "1883"))
MQTT_TOPIC  = os.getenv("MQTT_TOPIC",  "aquaponic/data")
WEB_PORT    = int(os.getenv("WEB_PORT",  "5500"))

# ── Shared frame buffer ───────────────────────────────────────────
LATEST_FRAME = "/tmp/aquaponic_latest.jpg"
frame_lock = threading.Lock()

# ── Thread-safe MQTT state ────────────────────────────────────────
latest_data = {}
data_lock = threading.Lock()

# ── Flask ─────────────────────────────────────────────────────────
app = Flask(__name__)

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/data")
def data():
    with data_lock:
        return jsonify(latest_data)

@app.route("/count")
def fish_count():
    return jsonify(get_counts())

# ── Live MJPEG video (hardware-encoded, ~5% CPU) ──────────────────
def _video_generator():
    cmd = [
        "rpicam-vid",
        "-t", "0",               # run indefinitely
        "--codec", "mjpeg",       # hardware MJPEG encoder
        "--width", "640",
        "--height", "480",
        "--framerate", "10",
        "-o", "-",               # stdout
        "-n"                      # no preview window
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, bufsize=0)

    buf = bytearray()
    try:
        while True:
            chunk = proc.stdout.read(4096)
            if not chunk:
                break
            buf.extend(chunk)

            # Extract complete JPEG frames (SOI FF D8 … EOI FF D9)
            while True:
                soi = buf.find(b'\xff\xd8')
                if soi == -1:
                    break
                eoi = buf.find(b'\xff\xd9', soi + 2)
                if eoi == -1:
                    break

                frame = bytes(buf[soi:eoi + 2])
                buf = buf[eoi + 2:]

                # Atomic write — classifier reads a complete frame
                tmp = LATEST_FRAME + ".tmp"
                with open(tmp, "wb") as f:
                    f.write(frame)
                os.replace(tmp, LATEST_FRAME)

                yield (b"--frame\r\n"
                       b"Content-Type: image/jpeg\r\n\r\n" +
                       frame + b"\r\n")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

@app.route("/video")
def video():
    return Response(_video_generator(),
                    mimetype="multipart/x-mixed-replace; boundary=frame")

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
        d = json.loads(msg.payload.decode())
    except json.JSONDecodeError:
        return
    with data_lock:
        latest_data = d

def mqtt_loop():
    client = mqtt.Client(client_id="aquaponic_web")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
    client.loop_forever()

# ── Main ──────────────────────────────────────────────────────────
if __name__ == "__main__":
    threading.Thread(target=mqtt_loop, daemon=True).start()
    start_classifier()
    print(f"Dashboard → http://0.0.0.0:{WEB_PORT}")
    app.run(host="0.0.0.0", port=WEB_PORT, debug=False, threaded=True)
