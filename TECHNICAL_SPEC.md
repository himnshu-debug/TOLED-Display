# TOLED Display — Technical Specification

Version as of commit `5dc9d46`. This document describes the system as currently built: hardware, firmware architecture, PC-side software, communication protocol, memory budgets, and known issues/resolutions.

## 1. System Overview

TOLED Display is a set of Arduino-based demos and tools built around the Waveshare 1.51inch Transparent OLED, spanning:

- A from-scratch software 3D rendering pipeline for an 8-bit microcontroller (rotation, perspective projection, backface culling, line-hatch shading)
- A literal, instrumented implementation of Bresenham's line algorithm for teaching/visualization
- A webcam → object detection → embedded display pipeline (PC does vision, Arduino does rendering)

All firmware shares one vendor driver library; each sketch is otherwise self-contained.

## 2. Hardware

### 2.1 Display
| Spec | Value |
|---|---|
| Model | Waveshare 1.51inch Transparent OLED |
| Driver IC | SSD1309 |
| Resolution | 128×64 (physical panel 64×128, used in 270° rotation for a 128×64 landscape framebuffer) |
| Color | Monochrome, light-blue transparent |
| Interface | 4-wire SPI (default) |

### 2.2 MCU (current)
| Spec | Value |
|---|---|
| Board | Arduino Uno |
| MCU | ATmega328P |
| Clock | 16 MHz |
| SRAM | 2 KB |
| Flash | 32 KB |
| EEPROM | 1 KB (unused) |

### 2.3 Wiring — single display (current physical build)
| OLED Pin | Arduino Pin | Notes |
|---|---|---|
| VCC | 3.3V/5V | |
| GND | GND | |
| DIN (MOSI) | D11 | hardware SPI |
| CLK (SCK) | D13 | hardware SPI |
| CS | D10 | |
| DC | D7 | |
| RST | D8 | |

### 2.4 Wiring — dual display plan (designed, not yet built)
For the proposed stereo/binocular variant. DIN and CLK are a shared SPI bus; every other signal is per-display.

| Signal | Display A | Display B |
|---|---|---|
| DIN/CLK | D11 / D13 (shared) | D11 / D13 (shared) |
| CS | D10 | D9 |
| DC | D7 | D6 |
| RST | D8 | D5 |

**Key constraint**: each display needs a 1024-byte framebuffer (`UBYTE *BlackImage`, sized `ceil(64/8) × 128`). Two independent framebuffers (2048 bytes) would consume 100% of the Uno's SRAM, leaving zero stack — not viable. The dual-display design pushes **one shared framebuffer** to both displays sequentially (with a configurable millisecond delay between the two `OLED_x_Display()` calls), which is also what the Pulfrich-effect experiment requires (identical content, time-offset per eye) — so the RAM constraint and the experiment's own requirements happen to align.

### 2.5 Peripheral hardware in use (PC side)
| Device | Role |
|---|---|
| HP Wide Vision HD Camera (built-in) | DirectShow index 0 |
| Logitech C615 HD Webcam (external, USB) | DirectShow index 1 — used by DEMO_CT |

### 2.6 Hardware considered but not yet acquired
- Second Waveshare 1.51inch Transparent OLED (for stereo/dual-eye build)
- ESP32 / ESP32-S3 (owned, not yet integrated) — needed for any on-device camera or TinyML work; Uno cannot do either (no USB host, insufficient RAM for frame buffers)
- Raspberry Pi — identified as the practical path for USB UVC webcam capture + real-time object detection, since neither Uno nor ESP32 can read a USB UVC device (no mature UVC-over-USB-host stack on microcontrollers)

## 3. Firmware Architecture

All sketches live under `OLED_Module_Code/Arduino/<SketchName>/` and share a common vendor driver library, copied into each sketch folder (Arduino requires all compiled sources alongside the `.ino`):

| File | Role |
|---|---|
| `OLED_Driver.cpp/.h` | Low-level SSD1309 SPI driver — init, clear, display push |
| `GUI_Paint.cpp/.h` | Framebuffer drawing primitives: points, lines (Bresenham), rectangles, circles, text, `Paint_SetPixel` |
| `DEV_Config.cpp/.h` | Pin definitions (`OLED_CS`, `OLED_DC`, `OLED_RST`), SPI/GPIO setup, `Serial.begin(115200)` |
| `Debug.h`, `ImageData.c/.h`, `font*.cpp`, `fonts.h` | Debug macros, sample bitmap, PROGMEM font tables (Font8/12/16/20/24, plus CN variants) |

### 3.1 Sketches

**`Geometry/`** — cycles through 7 wireframe 3D primitives, 10s each, each tumbling on all 3 axes with a sine-wave zoom ("breathing") effect:
Box → Sphere (octahedron) → Cylinder (hexagonal prism) → Cone (hexagonal base) → Pyramid (square base) → Wedge → Torus (6×3 segment mesh)

Implements a generic from-scratch 3D pipeline:
1. Rotate each vertex on X, Y, Z
2. Perspective-project: `scale = FOCAL / (CAM_DIST + z)` — nearer geometry renders larger (real parallax)
3. Backface-cull via the 2D signed area (shoelace formula) of each projected face
4. Fill visible faces with **diagonal line-hatch shading** at a per-face density (`gap` 0=outline-only, 1=solid, higher=sparser), then draw wireframe edges on top
5. Push the resulting 1bpp buffer over SPI

All shape data (vertex coordinates, face index lists) is stored in **PROGMEM** and read via `pgm_read_byte` — see §6.1 for why this is load-bearing. A generic `ShapeDef` struct + flattened face records (`[gap, idx0..idx5]`, `-1`-padded) let one engine render all 7 shapes instead of per-shape code.

Current build: **12,136 bytes flash (37%)**, **676 bytes RAM (33%)**.

**`I3D_Moving/`** — a shaded cube (same hatch-shading + backface-culling technique, but only Z-axis rotation) falls top-to-bottom while spinning, with "HELLO FROM I3D LAB" rising bottom-to-top with a sine-wave horizontal wiggle. Both loop continuously and independently.

Current build: **13,062 bytes flash (40%)**, **552 bytes RAM (27%)**.

**`Bresenham/`** — hardware demonstration of the line algorithm via `Paint_DrawLine`: a radar-sweep line rotates 360° from a center point, then a 60-spoke radial burst reveals one spoke per frame, holds, clears, and repeats.

Current build: **9,662 bytes flash (29%)**, **472 bytes RAM (23%)**.

**`Line_Steps/`** — re-implements the *exact* all-octant Bresenham loop from `GUI_Paint.cpp` (same `dx`, `dy`, `err`, `2·err` logic) inline, but plots one pixel per iteration with a 90ms pause and an enlarged "cursor" pixel, so each decision is individually visible. Cycles through 5 labeled test cases: Shallow (dx>dy), Steep (dy>dx), Diagonal (dx=dy), Horizontal (dy=0), Vertical (dx=0).

Current build: **8,730 bytes flash (27%)**, **608 bytes RAM (29%)**.

**`DEMO_CT/`** — the display half of a webcam object-detection demo (see §4). Listens on `Serial` for a single ASCII byte (`L`/`R`/`C`/`N`) and renders a matching full-screen sign: a left arrow, right arrow, centered bullseye, or "NO BOTTLE" text. Redraws only on state change (not every loop iteration) to avoid flicker.

Current build: **11,632 bytes flash (36%)**, **492 bytes RAM (24%)**.

## 4. PC-Side Software (DEMO_CT vision pipeline)

| Component | Version | Role |
|---|---|---|
| Python | 3.12.10 | Runtime |
| OpenCV (`opencv-python`) | 4.10.0.84 | Webcam capture, preview rendering |
| Ultralytics (YOLOv8n) | 8.4.170 | Pretrained object detection (COCO class 39 = "bottle"; no custom training) |
| pyserial | 3.5 | Serial link to the Arduino |

**Pipeline** (`detect_and_point.py`):
1. Open the Logitech C615 (DirectShow index 1) via `cv2.VideoCapture`
2. Each frame: run YOLOv8n restricted to class 39 (bottle), take the largest detected box
3. Compare the box's horizontal center to the frame midpoint (± a 12%-of-width deadzone) to classify `L` / `R` / `C`
4. **Debounce**: a single missed detection doesn't immediately flip to `N` — only after `MISS_TOLERANCE = 8` consecutive missed frames, since YOLO occasionally drops a frame on an otherwise-steady object
5. On state change, write one ASCII byte to the serial port
6. Show a live preview window with the bounding box and a center-line overlay; `q` quits

**Launch**: `Run_DEMO_CT.bat` (handles working directory, reminds the user to close the Windows Camera app, keeps the console open on exit/error) — also available as a Desktop shortcut ("DEMO_CT - Bottle Pointer").

## 5. Communication Protocol (PC ↔ Arduino)

- **Transport**: USB CDC serial, 115200 baud, 8N1 (default `Serial.begin(115200)` in `DEV_Config.cpp`)
- **Direction**: PC → Arduino only (one-way)
- **Payload**: single ASCII byte per state change
  - `L` = object left of center
  - `R` = object right of center
  - `C` = object centered
  - `N` = no object detected
- **Framing**: none needed — one byte is one complete message. No length prefix, checksum, or ACK (acceptable for a single-byte, low-rate, same-room USB-tethered link; would need framing if extended to richer payloads, e.g. bounding box coordinates).

## 6. Known Issues & Resolutions

### 6.1 RAM exhaustion from non-PROGMEM constant data (resolved)
**Symptom**: `Geometry` rendered as visual noise / garbage, then as a fully corrupted/blank display, as more 3D shapes were added.
**Root cause**: on AVR, a plain `const` array is **not** automatically stored in flash — without an explicit `PROGMEM` attribute it's copied into RAM at startup. Shape vertex/face tables (icosahedron, cylinder, torus) pushed RAM usage close to 1000 bytes, which combined with the mandatory 1024-byte framebuffer left under 50 bytes for the entire call stack — any nested function call (rotate → project → fill → draw) overflowed it, corrupting the framebuffer.
**Fix**: moved all shape tables to `PROGMEM`, read via `pgm_read_byte`; also reduced shape complexity (icosahedron → octahedron, octagon bases → hexagon, 8×4 torus mesh → 6×3) to shrink both the tables and the per-frame scratch buffers (`sx[]`, `sy[]`, `sharedEdges[]`) sized to the worst case. Net effect: RAM usage dropped from ~980 bytes to ~670-680 bytes, restoring a workable stack margin.

### 6.2 Pixel dithering reads as noise at this physical resolution (resolved)
**Symptom**: Bayer-matrix ordered dithering (used to fake multiple "shade" levels on a 1-bit display) looked like static/clutter in practice, not smooth shading.
**Root cause**: the OLED's pixel pitch is large enough, and the panel small enough, that per-pixel dither patterns are individually resolvable rather than optically blending into a perceived gray — a technique that works on higher-resolution displays viewed from a distance fails here.
**Fix**: replaced per-pixel Bayer dithering with **diagonal line-hatching** (`(x+y) % gap == 0`), which reads as a deliberate texture rather than noise at this resolution, while still giving discrete shade levels per face.

### 6.3 OpenCV 5.0 DirectShow backend cannot open non-zero camera indices (resolved)
**Symptom**: `cv2.VideoCapture(1, cv2.CAP_DSHOW)` (and `CAP_MSMF`, `CAP_ANY`) failed with `"backend is generally available but can't be used to capture by index"` for any index other than 0, even though the Logitech C615 was confirmed present and healthy (verified via `pygrabber` device enumeration and the native Windows Camera app).
**Fix**: downgraded `opencv-python` from the initially-installed `5.0.0.93` to the mature, widely-used `4.10.0.84` release.

### 6.4 Windows Camera Frame Server exclusive lock (resolved, operational issue)
**Symptom**: even after the OpenCV downgrade, opening the Logitech by index still failed.
**Root cause**: the Windows Camera app had an open session on the device; Windows' Frame Server can hold a reservation that blocks other applications (including DirectShow-based access from OpenCV) from acquiring the same device.
**Fix**: no code change — closing the Windows Camera app released the lock. `Run_DEMO_CT.bat` now prints a reminder to close it before running.

## 7. Repository Structure

GitHub: `https://github.com/himnshu-debug/TOLED-Display` (public)

```
TOLED-Display/
├── README.md
├── TECHNICAL_SPEC.md          (this file)
├── .gitignore                 (*.pt, __pycache__/, *.pyc)
├── Geometry/                  (sketch + vendor driver copy)
├── I3D_Moving/                (sketch + vendor driver copy)
├── Bresenham/                 (sketch + vendor driver copy)
├── Line_Steps/                (sketch + vendor driver copy)
└── DEMO_CT/                   (sketch + vendor driver copy + detect_and_point.py + Run_DEMO_CT.bat)
```

Each sketch folder is a complete, independent Arduino IDE project (vendor driver files are duplicated per folder rather than shared via a library, matching how Arduino sketches are structured).

## 8. Open Design Directions (not yet implemented)

- **Dual-eye stereo rig**: pin plan designed (§2.4); software not yet written. Two sub-variants discussed:
  - *Pulfrich-effect test*: same framebuffer pushed to both displays with a controllable millisecond delay between them — creates a depth illusion, but only for laterally-moving content (not general scenes)
  - *True stereoscopy*: independent per-eye renders — ruled out on the Uno (RAM), would need a Mega (8KB SRAM) or similar
- **Camera-integrated glasses**: identified architecture is vision/compute split — a Raspberry Pi (or similar) handles USB webcam capture + object detection (Uno/ESP32 can't read UVC devices), then sends lightweight results over serial to the existing display-rendering firmware, reusing `GUI_Paint`/`Paint_DrawLine` as-is
- **Object detection scope**: currently single-class (bottle, COCO id 39) with a 2-state horizontal position signal; richer payloads (multiple classes, bounding box geometry, distance estimate) would need the serial protocol in §5 extended with real framing
