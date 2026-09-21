# TOLED Display

Arduino Uno demos for the **Waveshare 1.51inch Transparent OLED** (128×64, SSD1309-based, light-blue transparent panel). Both sketches render a small software 3D engine — perspective projection, backface culling, and diagonal line-hatch shading — entirely on an 8-bit Uno, using a bitmap frame buffer pushed to the display each frame.

## Hardware

- **Display**: Waveshare 1.51inch Transparent OLED (128×64, SSD1309 driver, 4-wire SPI by default)
  - Product wiki: https://www.waveshare.com/wiki/1.51inch_Transparent_OLED
- **MCU**: Arduino Uno (ATmega328P, 2KB SRAM / 32KB flash)

### Wiring (4-wire SPI)

| OLED Pin | Arduino Pin |
|----------|-------------|
| VCC      | 3.3V / 5V   |
| GND      | GND         |
| DIN      | D11         |
| CLK      | D13         |
| CS       | D10         |
| DC       | D7          |
| RST      | D8          |

## Driver / library requirements

No external Arduino library needs to be installed. Each sketch folder is self-contained and includes Waveshare's vendor driver files alongside the `.ino`:

- `OLED_Driver.cpp/.h` — low-level SSD1309 SPI driver
- `GUI_Paint.cpp/.h` — framebuffer drawing primitives (points, lines, shapes, text, `Paint_SetPixel`)
- `DEV_Config.cpp/.h` — pin/peripheral setup (SPI, GPIO)
- `Debug.h`, `ImageData.c/.h`, `font*.cpp`, `fonts.h` — debug macros, sample image, and font tables

Just open the `.ino` in Arduino IDE (board: **Arduino Uno**) and upload — everything it needs is in the same folder.

## Sketches

### `Geometry/`
Cycles through 7 wireframe 3D primitives, 10 seconds each, each one tumbling continuously on all 3 axes while "breathing" (zooming in and out):

**Box → Sphere → Cylinder → Cone → Pyramid → Wedge → Torus**

- Shape data (vertices/faces/edges) is stored in **flash via `PROGMEM`**, not RAM — the Uno only has 2KB of SRAM and the display's own frame buffer already consumes half of it, so keeping geometry tables out of RAM is what makes this fit.
- Faces are shaded with ordered diagonal line-hatching (not pixel dithering — at this physical pixel density, per-pixel dithering just reads as noise) at varying densities per face, giving each shape a sense of volume.
- Backface culling via the 2D signed area of each projected face keeps hidden faces from being drawn.

### `I3D_Moving/`
A shaded 3D cube that falls from the top of the screen to the bottom while spinning on its Z-axis, with "HELLO FROM I3D LAB" rising from the bottom to the top with a side-to-side wiggle, looping continuously.

## Notes on the rendering approach

Both sketches implement the same lightweight pipeline from scratch (no 3D library):

1. Rotate each vertex (Z-axis only in `I3D_Moving`, all 3 axes in `Geometry`)
2. Perspective-project to screen space (`scale = focal / (camDist + z)`) — nearer geometry renders larger, giving real parallax
3. Cull back-facing polygons
4. Fill visible faces with hatch-pattern shading, then draw wireframe edges on top for crisp silhouettes
5. Push the resulting 1bpp framebuffer to the OLED over SPI

This was built and tuned iteratively directly against physical hardware (Arduino Uno + this OLED), including a few RAM-exhaustion crashes along the way from storing geometry tables in RAM instead of flash — worth knowing if you extend it with more/bigger shapes.
