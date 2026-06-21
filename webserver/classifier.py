"""
Fish classifier — local YOLOv11 inference on Pi (ultralytics).
Reads latest video frame every 30s. Draws bounding boxes.
"""

import os
import threading
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from ultralytics import YOLO

# ── Configuration ─────────────────────────────────────────────────
LATEST_FRAME    = "/tmp/aquaponic_latest.jpg"
ANNOTATED_FRAME = "/tmp/aquaponic_annotated.jpg"
CLASSIFY_INTERVAL = 5    # seconds between auto-classifications (local inference, no API limit)

MODEL_DIR  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "zebrafish_model")
MODEL_PATH = os.path.join(MODEL_DIR, "best.pt")
CLASSES    = ["black_fish", "pink_fish", "yellow_fish"]

CONF_THRESH = 0.4

COLORS = {
    "black_fish":  (34, 197, 94),
    "pink_fish":   (239, 68, 68),
    "yellow_fish": (245, 158, 11),
}

# ── Lazy-load model ───────────────────────────────────────────────
_model = None

# Thread-safe latest predictions (for live video overlay)
latest_predictions = []
pred_lock = threading.Lock()

def _get_model():
    global _model
    if _model is None:
        _model = YOLO(MODEL_PATH)
        print(f"[classifier] Model loaded: {MODEL_PATH}")
    return _model


def classify(path=None):
    """Run YOLOv11 inference. Returns (predictions, raw_dict)."""
    fp = path or LATEST_FRAME
    if not os.path.exists(fp):
        return [], {}

    try:
        results = _get_model()(fp, conf=CONF_THRESH, verbose=False)
    except Exception as e:
        print(f"[classifier] Error: {e}")
        return [], {"error": str(e)}

    predictions = []
    for r in results:
        boxes = r.boxes
        if boxes is None:
            continue
        for i in range(len(boxes)):
            cls_id = int(boxes.cls[i].item())
            conf   = float(boxes.conf[i].item())
            xyxy   = boxes.xyxy[i].tolist()
            predictions.append({
                "class": CLASSES[cls_id] if cls_id < len(CLASSES) else str(cls_id),
                "confidence": round(conf, 4),
                "bbox": [round(v, 1) for v in xyxy],
            })

    # Store for live video overlay
    global latest_predictions
    with pred_lock:
        latest_predictions = predictions

    _draw_boxes(fp, predictions, ANNOTATED_FRAME)
    print(f"[classifier] {len(predictions)} det")
    return predictions, {"status": "ok", "count": len(predictions)}


def _draw_boxes(img_path, predictions, out_path):
    """Draw bounding boxes on image."""
    try:
        img = Image.open(img_path).convert("RGB")
    except Exception:
        return
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default()
    except Exception:
        font = None

    w, h = img.size
    # Predictions are in 640x640 space — scale to original
    for pred in predictions:
        cls = pred["class"]
        conf = pred["confidence"]
        x1, y1, x2, y2 = pred["bbox"]
        rx1 = int(x1 / 640 * w); ry1 = int(y1 / 640 * h)
        rx2 = int(x2 / 640 * w); ry2 = int(y2 / 640 * h)
        color = COLORS.get(cls, (148, 163, 184))
        draw.rectangle([rx1, ry1, rx2, ry2], outline=color, width=3)
        label = f"{cls} {conf:.2f}"
        bbox = draw.textbbox((rx1, ry1 - 14), label) if font else (rx1, ry1-14, rx1+80, ry1)
        draw.rectangle(bbox, fill=color)
        draw.text((rx1, ry1 - 14), label, fill=(0, 0, 0)) if font else None
    img.save(out_path, quality=85)


def apply_overlay(jpeg_bytes):
    """Draw latest bboxes onto JPEG frame bytes. Returns annotated JPEG bytes.
    Called by video generator on every frame (~10fps, ~5ms per call)."""
    with pred_lock:
        preds = list(latest_predictions)
    if not preds:
        return jpeg_bytes

    try:
        from io import BytesIO
        img = Image.open(BytesIO(jpeg_bytes)).convert("RGB")
        draw = ImageDraw.Draw(img)
        font = ImageFont.load_default()
    except Exception:
        return jpeg_bytes

    w, h = img.size
    for pred in preds:
        x1, y1, x2, y2 = pred["bbox"]
        rx1 = int(x1 / 640 * w); ry1 = int(y1 / 640 * h)
        rx2 = int(x2 / 640 * w); ry2 = int(y2 / 640 * h)
        color = COLORS.get(pred["class"], (148, 163, 184))
        draw.rectangle([rx1, ry1, rx2, ry2], outline=color, width=3)
        label = f"{pred['class']} {pred['confidence']:.2f}"
        try:
            tb = draw.textbbox((rx1, ry1 - 14), label, font=font)
            draw.rectangle(tb, fill=color)
            draw.text((rx1, ry1 - 14), label, fill=(0, 0, 0), font=font)
        except Exception:
            pass

    buf = BytesIO()
    img.save(buf, format="JPEG", quality=85)
    return buf.getvalue()


def get_latest_predictions():
    with pred_lock:
        return list(latest_predictions)
counts = {c: 0 for c in CLASSES}
counts["total"] = 0
lock = threading.Lock()

def _classification_loop():
    global counts
    time.sleep(5)
    while True:
        predictions, _ = classify()
        new = {c: 0 for c in CLASSES}
        for p in predictions:
            cls = p.get("class", "")
            if cls in new:
                new[cls] += 1
        new["total"] = sum(new.values())
        with lock:
            counts = dict(new)
        time.sleep(CLASSIFY_INTERVAL)

def get_counts():
    with lock:
        return dict(counts)

def start_classifier():
    threading.Thread(target=_classification_loop, daemon=True).start()
    print("[classifier] Started (ultralytics/YOLOv11)")
