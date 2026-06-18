"""
Fish classifier — reads frames from live MJPEG pipe (no second camera process).
Grabs /tmp/aquaponic_latest.jpg every 30s → Roboflow REST API.
"""

import base64
import json
import os
import threading
import time

try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False
    print("[classifier] 'requests' not installed — classification disabled")

# ── Configuration ─────────────────────────────────────────────────
LATEST_FRAME    = "/tmp/aquaponic_latest.jpg"
CLASSIFY_INTERVAL = 30

CLASSES = ["black_fish", "Fishes", "pink_fish"]

API_KEY     = "EPWdafhgzT40Fp2GUKHG"
API_URL     = "https://serverless.roboflow.com/mlaaw/general-segmentation-api"

# Thread-safe counts
counts = {"black_fish": 0, "Fishes": 0, "pink_fish": 0, "total": 0}
lock = threading.Lock()

# ── Roboflow REST call ────────────────────────────────────────────
def classify():
    if not HAS_REQUESTS:
        return []
    if not os.path.exists(LATEST_FRAME):
        return []
    try:
        with open(LATEST_FRAME, "rb") as f:
            img_b64 = base64.b64encode(f.read()).decode("utf-8")

        payload = {
            "api_key": API_KEY,
            "inputs": {
                "image": {"type": "base64", "value": img_b64}
            },
            "parameters": {"classes": CLASSES}
        }

        resp = requests.post(API_URL, json=payload, timeout=30)
        resp.raise_for_status()
        result = resp.json()
        return result.get("outputs", []) or result.get("predictions", [])
    except Exception as e:
        print(f"[classifier] API error: {e}")
        return []

# ── Background thread ─────────────────────────────────────────────
def _classification_loop():
    global counts
    time.sleep(5)   # wait for first frames to arrive

    while True:
        predictions = classify()

        new_counts = {c: 0 for c in CLASSES}
        for pred in predictions:
            cls = pred.get("class", "")
            if cls in new_counts:
                new_counts[cls] += 1
        new_counts["total"] = sum(new_counts.values())

        with lock:
            counts = dict(new_counts)

        time.sleep(CLASSIFY_INTERVAL)

def get_counts():
    with lock:
        return dict(counts)

def start_classifier():
    threading.Thread(target=_classification_loop, daemon=True).start()
    print("[classifier] Started (reads from live video pipe)")
