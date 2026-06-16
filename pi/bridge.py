#!/usr/bin/env python3
"""
Aquaponic MQTT-to-InfluxDB Bridge
Subscribes to aquaponic/data, writes all sensor fields to InfluxDB v2.

Usage:
    pip3 install paho-mqtt influxdb-client
    export INFLUX_TOKEN='your-token'
    python3 bridge.py

Configuration via environment variables (see aquaponic-bridge.service).
"""

import json
import logging
import os
import signal
import sys
import threading

import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

# ── Configuration ─────────────────────────────────────────────────
MQTT_BROKER   = os.getenv("MQTT_BROKER",   "localhost")
MQTT_PORT     = int(os.getenv("MQTT_PORT", "1883"))
MQTT_TOPIC    = os.getenv("MQTT_TOPIC",    "aquaponic/data")
INFLUX_URL    = os.getenv("INFLUX_URL",    "http://localhost:8086")
INFLUX_TOKEN  = os.getenv("INFLUX_TOKEN",  "")
INFLUX_ORG    = os.getenv("INFLUX_ORG",    "aquaponic")
INFLUX_BUCKET = os.getenv("INFLUX_BUCKET", "sensor_data")

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s"
)
log = logging.getLogger("bridge")

# ── InfluxDB client ───────────────────────────────────────────────
influx = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
write_api = influx.write_api(write_options=SYNCHRONOUS)

# ── MQTT callbacks ────────────────────────────────────────────────
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        log.info("MQTT connected to %s:%s", MQTT_BROKER, MQTT_PORT)
        client.subscribe(MQTT_TOPIC)
        log.info("Subscribed to %s", MQTT_TOPIC)
    else:
        log.error("MQTT connection failed, rc=%d", rc)

def on_disconnect(client, userdata, rc):
    log.warning("MQTT disconnected, rc=%d (auto-reconnect enabled)", rc)

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
    except json.JSONDecodeError as e:
        log.error("JSON parse error: %s", e)
        return

    try:
        point = Point("sensor_data") \
            .tag("device", "esp32") \
            .field("tds", float(data["tds"])) \
            .field("ph", float(data["ph"])) \
            .field("temp", float(data["temp"])) \
            .field("lux", float(data["lux"])) \
            .field("pump", 1 if data.get("pump", False) else 0) \
            .field("uv", 1 if data.get("uv", False) else 0) \
            .field("aq_ppm", float(data["aq_ppm"])) \
            .field("aq_pct", float(data["aq_pct"])) \
            .field("aq_label", str(data.get("aq_label", ""))) \
            .field("air_relay", 1 if data.get("air_relay", False) else 0) \
            .field("temp_relay", 1 if data.get("temp_relay", False) else 0) \
            .field("sonar_dist", float(data["sonar_dist"])) \
            .field("water_level", float(data["water_level"])) \
            .field("water_pump", 1 if data.get("water_pump", False) else 0) \
            .field("gsm_ready", 1 if data.get("gsm_ready", False) else 0) \
            .field("gsm_rssi", int(data.get("rssi", 0))) \
            .field("gsm_volt", float(data.get("gsm_volt", 0))) \
            .field("sms_sent", int(data.get("sms_sent", 0))) \
            .field("tds_quality", str(data.get("tds_quality", "")))

        write_api.write(bucket=INFLUX_BUCKET, record=point)
        log.debug("Written: TDS=%.1f  pH=%.2f  Temp=%.2f  Lux=%.1f",
                  data["tds"], data["ph"], data["temp"], data["lux"])

    except (KeyError, ValueError, TypeError) as e:
        log.error("Data format error: %s", e)
    except Exception as e:
        log.error("InfluxDB write error: %s", e)

# ── Main ──────────────────────────────────────────────────────────
def main():
    if not INFLUX_TOKEN:
        log.fatal("INFLUX_TOKEN environment variable not set")
        sys.exit(1)

    mqtt_client = mqtt.Client(client_id="aquaponic_bridge")
    mqtt_client.on_connect = on_connect
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.on_message = on_message

    mqtt_client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
    mqtt_client.loop_start()

    log.info("Bridge started — MQTT=%s:%d  InfluxDB=%s  bucket=%s",
             MQTT_BROKER, MQTT_PORT, INFLUX_URL, INFLUX_BUCKET)

    stop = threading.Event()
    signal.signal(signal.SIGINT,  lambda s, f: stop.set())
    signal.signal(signal.SIGTERM, lambda s, f: stop.set())

    stop.wait()
    log.info("Shutting down...")
    mqtt_client.loop_stop()
    mqtt_client.disconnect()
    influx.close()
    log.info("Bridge stopped")

if __name__ == "__main__":
    main()
