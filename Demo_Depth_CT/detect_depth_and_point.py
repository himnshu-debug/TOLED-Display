"""
Demo_Depth_CT - water bottle direction + distance demo (PC side)

Builds on DEMO_CT by adding monocular depth estimation (Depth Anything V2,
via Hugging Face transformers) so the Arduino's arrow isn't just L/R/C -
it also scales in size with how close the bottle is.

Per frame:
  1. YOLOv8n finds the bottle (COCO class 39) and which side of frame it's on
  2. Depth Anything V2 Small runs on just the bottle's own crop, and we read
     its RAW "predicted_depth" output (higher = nearer) - not the "depth" key,
     which is a visualization image re-normalized to 0-255 on every single
     call and therefore nearly constant regardless of true distance
  3. That raw value is smoothed over a few frames, then classified against a
     self-calibrating near/far range built from the last ~90 frames (no
     hardcoded absolute thresholds, since Depth Anything's raw output scale
     isn't fixed/known in advance)

Sends TWO bytes per update to the Arduino:
    direction: 'L' / 'R' / 'C' / 'N'
    size tier: 'S' (far)  / 'M' (medium) / 'B' (near)

This is a RELATIVE depth estimate (not metric distance) - good enough for
"closer vs farther", not for an actual centimeter reading.

Press 'q' in the preview window to quit.
"""

import time
from collections import deque

import cv2
import serial
import torch
from PIL import Image
from transformers import pipeline
from ultralytics import YOLO

SERIAL_PORT = "COM3"
BAUD = 115200
BOTTLE_CLASS_ID = 39      # COCO class id for "bottle"
CENTER_DEADZONE = 0.12    # fraction of frame width treated as "centered"
DEFAULT_CAMERA_INDEX = 1  # used if the user just presses Enter, or picking fails
MISS_TOLERANCE = 8        # frames allowed to miss detection before declaring "no bottle"

DEPTH_MODEL = "depth-anything/Depth-Anything-V2-Small-hf"
DEPTH_SMOOTH_LEN = 5      # frames averaged to smooth the nearness score itself
DEPTH_RANGE_LEN = 90      # frames (~a few seconds) used to self-calibrate near/far
MIN_OBSERVED_SPREAD = 1e-3  # guard against classifying on near-zero variation
CROP_PADDING = 0.15       # extra margin around the bbox fed to the depth model


def select_camera(default_index=DEFAULT_CAMERA_INDEX):
    """List every camera Windows knows about by name and let the user pick one."""
    try:
        from pygrabber.dshow_graph import FilterGraph
        devices = FilterGraph().get_input_devices()
    except Exception as e:
        print(f"Could not list cameras by name ({e}); using index {default_index}.")
        return default_index

    if not devices:
        print(f"No cameras detected; using index {default_index}.")
        return default_index

    print("\nAvailable cameras:")
    for i, name in enumerate(devices):
        marker = "  <- default" if i == default_index else ""
        print(f"  [{i}] {name}{marker}")

    choice = input(f"Select a camera by number [default {default_index}]: ").strip()
    if choice == "":
        return default_index
    if choice.isdigit() and int(choice) < len(devices):
        return int(choice)

    print(f"Didn't recognize '{choice}', using index {default_index}.")
    return default_index


def main():
    print("Loading YOLOv8n...")
    yolo = YOLO("yolov8n.pt")

    device = 0 if torch.cuda.is_available() else -1
    print(f"Loading {DEPTH_MODEL} (device={'GPU' if device == 0 else 'CPU'})...")
    depth_pipe = pipeline(task="depth-estimation", model=DEPTH_MODEL, device=device)

    camera_index = select_camera()

    print(f"Opening {SERIAL_PORT} @ {BAUD} baud...")
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
    time.sleep(2)  # let the Uno finish its reset-on-connect before we talk to it

    cap = cv2.VideoCapture(camera_index, cv2.CAP_DSHOW)
    if not cap.isOpened():
        raise RuntimeError(f"Could not open camera index {camera_index}")

    current_dir = "N"
    current_tier = "M"
    miss_count = 0
    last_sent = None
    smooth_history = deque(maxlen=DEPTH_SMOOTH_LEN)   # smooths frame-to-frame noise
    range_history = deque(maxlen=DEPTH_RANGE_LEN)     # self-calibrates near/far bounds

    print("Running - show a water bottle to the camera. Press 'q' to quit.")

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                break

            h, w = frame.shape[:2]
            results = yolo(frame, verbose=False, classes=[BOTTLE_CLASS_ID])

            best_box = None
            best_area = 0
            for r in results:
                for box in r.boxes:
                    x1, y1, x2, y2 = box.xyxy[0].tolist()
                    area = (x2 - x1) * (y2 - y1)
                    if area > best_area:
                        best_area = area
                        best_box = (x1, y1, x2, y2)

            if best_box:
                miss_count = 0
                x1, y1, x2, y2 = best_box
                cx = (x1 + x2) / 2
                mid = w / 2
                deadzone = w * CENTER_DEADZONE

                if cx < mid - deadzone:
                    current_dir = "L"
                elif cx > mid + deadzone:
                    current_dir = "R"
                else:
                    current_dir = "C"

                # pad the crop a bit so the depth model sees some context
                bw, bh = x2 - x1, y2 - y1
                px, py = bw * CROP_PADDING, bh * CROP_PADDING
                cx1 = max(0, int(x1 - px))
                cy1 = max(0, int(y1 - py))
                cx2 = min(w, int(x2 + px))
                cy2 = min(h, int(y2 + py))

                nearness = None
                if cx2 - cx1 >= 4 and cy2 - cy1 >= 4:
                    crop_bgr = frame[cy1:cy2, cx1:cx2]
                    crop_rgb = cv2.cvtColor(crop_bgr, cv2.COLOR_BGR2RGB)
                    result = depth_pipe(Image.fromarray(crop_rgb))
                    # "predicted_depth" is the raw model output (higher = nearer).
                    # "depth" (not used here) is a PIL image re-normalized to 0-255
                    # PER CALL for visualization, so its mean is nearly constant
                    # across frames regardless of true distance - not useful for
                    # comparing "near" vs "far" over time.
                    predicted = result["predicted_depth"]
                    nearness = float(predicted.mean())
                    smooth_history.append(nearness)
                    range_history.append(nearness)

                if smooth_history:
                    avg_nearness = sum(smooth_history) / len(smooth_history)
                    observed_min = min(range_history)
                    observed_max = max(range_history)
                    spread = observed_max - observed_min

                    if spread < MIN_OBSERVED_SPREAD:
                        current_tier = "M"  # not enough variation seen yet to judge
                    else:
                        position = (avg_nearness - observed_min) / spread  # 0..1
                        if position >= 0.66:
                            current_tier = "B"
                        elif position <= 0.33:
                            current_tier = "S"
                        else:
                            current_tier = "M"

                cv2.rectangle(frame, (int(x1), int(y1)), (int(x2), int(y2)), (0, 255, 0), 2)
                depth_label = f"{nearness:.3f}" if nearness is not None else "-"
                cv2.putText(frame, f"{current_dir}/{current_tier}  depth~{depth_label}",
                            (int(x1), max(20, int(y1) - 8)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
            else:
                # a missed frame doesn't necessarily mean the bottle is gone - only
                # declare "no bottle" after several consecutive misses in a row
                miss_count += 1
                if miss_count > MISS_TOLERANCE:
                    current_dir = "N"
                    smooth_history.clear()
                    # range_history is left intact - it's long-term self-calibration
                    # and shouldn't be thrown away just because the bottle briefly
                    # left the frame

            signal = (current_dir, current_tier)
            if signal != last_sent:
                ser.write((current_dir + current_tier).encode("ascii"))
                print(f"signal -> dir={current_dir} tier={current_tier}")
                last_sent = signal

            cv2.line(frame, (w // 2, 0), (w // 2, h), (255, 255, 0), 1)
            cv2.imshow("Demo_Depth_CT - direction + distance", frame)
            if cv2.waitKey(1) & 0xFF == ord("q"):
                break
    finally:
        cap.release()
        cv2.destroyAllWindows()
        ser.close()


if __name__ == "__main__":
    main()
