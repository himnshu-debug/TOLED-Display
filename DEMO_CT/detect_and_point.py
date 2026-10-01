"""
DEMO_CT - water bottle direction demo (PC side)

Watches the webcam for a water bottle (YOLOv8n, pretrained on COCO - class
39 is "bottle", so no custom training needed), works out whether it's to
the left, right, or roughly centered in frame, and sends one byte over
serial to the Arduino running DEMO_CT.ino:

    'L' = bottle is in the left part of the frame
    'R' = bottle is in the right part of the frame
    'C' = bottle is roughly centered
    'N' = no bottle currently in view

Press 'q' in the preview window to quit.
"""

import time

import cv2
from ultralytics import YOLO
import serial

SERIAL_PORT = "COM3"     # the Arduino's port
BAUD = 115200             # matches Serial.begin(115200) in DEV_Config.cpp
BOTTLE_CLASS_ID = 39      # COCO class id for "bottle"
CENTER_DEADZONE = 0.12    # fraction of frame width treated as "centered"
CAMERA_INDEX = 0


def main():
    print("Loading YOLOv8n (first run downloads the pretrained weights)...")
    model = YOLO("yolov8n.pt")

    print(f"Opening {SERIAL_PORT} @ {BAUD} baud...")
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
    time.sleep(2)  # let the Uno finish its reset-on-connect before we talk to it

    cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open camera index {CAMERA_INDEX}")

    last_sent = None
    print("Running - show a water bottle to the camera. Press 'q' to quit.")

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                break

            h, w = frame.shape[:2]
            results = model(frame, verbose=False, classes=[BOTTLE_CLASS_ID])

            best_box = None
            best_area = 0
            for r in results:
                for box in r.boxes:
                    x1, y1, x2, y2 = box.xyxy[0].tolist()
                    area = (x2 - x1) * (y2 - y1)
                    if area > best_area:
                        best_area = area
                        best_box = (x1, y1, x2, y2)

            signal = "N"
            if best_box:
                x1, y1, x2, y2 = best_box
                cx = (x1 + x2) / 2
                mid = w / 2
                deadzone = w * CENTER_DEADZONE

                if cx < mid - deadzone:
                    signal = "L"
                elif cx > mid + deadzone:
                    signal = "R"
                else:
                    signal = "C"

                cv2.rectangle(frame, (int(x1), int(y1)), (int(x2), int(y2)), (0, 255, 0), 2)
                cv2.putText(frame, f"bottle -> {signal}", (int(x1), max(20, int(y1) - 8)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

            if signal != last_sent:
                ser.write(signal.encode("ascii"))
                print(f"signal -> {signal}")
                last_sent = signal

            cv2.line(frame, (w // 2, 0), (w // 2, h), (255, 255, 0), 1)
            cv2.imshow("DEMO_CT - water bottle direction", frame)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    finally:
        cap.release()
        cv2.destroyAllWindows()
        ser.close()


if __name__ == "__main__":
    main()
