#include <math.h>
#include "OLED_Driver.h"
#include "GUI_paint.h"
#include "DEV_Config.h"
#include "Debug.h"
#include "ImageData.h"

// Draws each preset line ONE PIXEL AT A TIME, mirroring the exact loop
// structure of Paint_DrawLine in GUI_Paint.cpp (same dx, dy, err, 2*err
// decision logic) so you can literally watch Bresenham's algorithm choose
// each pixel and progress across the screen, for every octant.

UBYTE *BlackImage;

struct LineCase { int x0, y0, x1, y1; const char *label; };

const LineCase cases[] = {
  { 6,  16, 122, 34, "SHALLOW   dx > dy" },
  { 30, 14, 50,  60, "STEEP     dy > dx" },
  { 10, 12, 56,  58, "DIAGONAL  dx = dy" },
  { 6,  38, 122, 38, "HORIZONTAL  dy = 0" },
  { 64, 14, 64,  62, "VERTICAL    dx = 0" },
};
const int NUM_CASES = sizeof(cases) / sizeof(cases[0]);

const int STEP_DELAY = 90;    // ms between pixels -- slow enough to watch it progress
const int LABEL_HOLD = 500;   // ms showing just the label before drawing starts
const int FINISH_HOLD = 1600; // ms holding the completed line before the next case

// Same all-octant Bresenham loop as Paint_DrawLine, but plots one pixel
// per iteration with a delay, highlighting the current pixel as it goes.
void drawLineStepwise(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0);
  int dy = -abs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;
  int x = x0, y = y0;
  int prevX = -1, prevY = -1;

  for (;;) {
    if (prevX >= 0) Paint_DrawPoint(prevX, prevY, WHITE, DOT_PIXEL_1X1, DOT_STYLE_DFT);
    Paint_DrawPoint(x, y, WHITE, DOT_PIXEL_3X3, DOT_STYLE_DFT); // current step, enlarged
    OLED_1IN51_Display(BlackImage);
    prevX = x; prevY = y;
    Driver_Delay_ms(STEP_DELAY);

    bool doneX = false, doneY = false;
    int e2 = 2 * err;
    if (e2 >= dy) {
      if (x == x1) doneX = true;
      else { err += dy; x += sx; }
    }
    if (e2 <= dx) {
      if (y == y1) doneY = true;
      else { err += dx; y += sy; }
    }
    if (doneX || doneY) break;
  }

  Paint_DrawPoint(prevX, prevY, WHITE, DOT_PIXEL_1X1, DOT_STYLE_DFT); // settle final pixel
  OLED_1IN51_Display(BlackImage);
}

void setup() {
  System_Init();
  Serial.print(F("OLED_Init()...\r\n"));
  OLED_1IN51_Init();
  Driver_Delay_ms(500);
  OLED_1IN51_Clear();

  UWORD Imagesize = ((OLED_1IN51_WIDTH % 8 == 0) ? (OLED_1IN51_WIDTH / 8) : (OLED_1IN51_WIDTH / 8 + 1)) * OLED_1IN51_HEIGHT;
  if ((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    Serial.print("Failed to apply for black memory...\r\n");
    return;
  }
  Paint_NewImage(BlackImage, OLED_1IN51_WIDTH, OLED_1IN51_HEIGHT, 270, BLACK);
  Paint_SelectImage(BlackImage);
  Paint_Clear(BLACK);
}

void loop() {
  static int caseIndex = 0;
  const LineCase &c = cases[caseIndex];

  Paint_Clear(BLACK);
  Paint_DrawString_EN(2, 2, c.label, &Font8, WHITE, WHITE);
  OLED_1IN51_Display(BlackImage);
  Driver_Delay_ms(LABEL_HOLD);

  drawLineStepwise(c.x0, c.y0, c.x1, c.y1);

  Driver_Delay_ms(FINISH_HOLD);

  caseIndex = (caseIndex + 1) % NUM_CASES;
}
