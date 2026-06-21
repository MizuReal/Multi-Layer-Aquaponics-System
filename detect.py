from ultralytics import YOLO
import cv2

model = YOLO("/home/pi/zebrafish/best_ncnn_model/", task="detect")

cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
cap.set(cv2.CAP_PROP_FPS, 30)

CONF_THRESHOLD = 0.5

while True:
    ret, frame = cap.read()
    if not ret:
        break

    results = model.predict(
        source=frame,
        conf=CONF_THRESHOLD,
        imgsz=640,
        verbose=False,
    )

    annotated = results[0].plot()
    cv2.imshow("ZebraFish Detector", annotated)

    for box in results[0].boxes:
        cls = results[0].names[int(box.cls)]
        conf = float(box.conf)
        print(f"Detected: {cls} ({conf:.2f})")

    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()