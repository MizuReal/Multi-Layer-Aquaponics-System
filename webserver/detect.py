#!/usr/bin/env python3
"""Zebrafish detector — YOLOv11 ultralytics on Pi. Usage: python3 detect.py [image.jpg]"""

import sys, os, time
from ultralytics import YOLO

MODEL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "zebrafish_model")
MODEL_PATH = os.path.join(MODEL_DIR, "best.pt")
CLASSES = ["black_fish", "pink_fish", "yellow_fish"]

if not os.path.exists(MODEL_PATH):
    print(f"Model not found: {MODEL_PATH}")
    print("Extract the zip: cd ~ && unzip -o zebrafish_model_full.zip -d webserver/zebrafish_model")
    sys.exit(1)

model = YOLO(MODEL_PATH)
print(f"Loaded: {MODEL_PATH}")

img = sys.argv[1] if len(sys.argv) > 1 else "/tmp/aquaponic_latest.jpg"
start = time.time()
results = model(img, conf=0.4, verbose=False)
elapsed = time.time() - start

for r in results:
    for box in (r.boxes or []):
        cls_id = int(box.cls[0].item())
        conf = float(box.conf[0].item())
        print(f"  {CLASSES[cls_id]}: {conf:.2f}")

# Save annotated
if results:
    results[0].save("/tmp/detect_output.jpg")
    print(f"Saved: /tmp/detect_output.jpg")
print(f"Time: {elapsed*1000:.0f}ms")
