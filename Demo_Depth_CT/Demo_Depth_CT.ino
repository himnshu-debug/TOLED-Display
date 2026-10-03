#include <math.h>
#include <string.h>
#include "OLED_Driver.h"
#include "GUI_paint.h"
#include "DEV_Config.h"
#include "Debug.h"
#include "ImageData.h"

// Demo_Depth_CT: display half of the bottle-direction-and-distance demo.
// detect_depth_and_point.py (PC side) finds a bottle with YOLOv8n, works out
// which side of frame it's on, then runs Depth Anything V2 on the bottle's
// own crop to estimate how near/far it is. It sends TWO bytes per update:
//   byte 1 (direction): 'L' left, 'R' right, 'C' centered, 'N' none seen
//   byte 2 (size tier): 'S' far/small, 'M' medium, 'B' near/big
// This sketch draws the matching arrow/icon at the matching size - the
// closer the bottle, the bigger the icon on screen.

UBYTE *BlackImage;

const int CX = 64, CY = 32;

float scaleForTier(char tier) {
  if (tier == 'S') return 0.55;
  if (tier == 'B') return 1.5;
  return 1.0; // 'M'
}

DOT_PIXEL dotPixelForTier(char tier) {
  if (tier == 'S') return DOT_PIXEL_2X2;
  if (tier == 'B') return DOT_PIXEL_4X4;
  return DOT_PIXEL_3X3;
}

void drawCornerLabel(const char *label) {
  Paint_DrawString_EN(2, 2, label, &Font8, WHITE, WHITE);
}

// Arrow grows/shrinks around the same center point (CX, CY) depending on
// tier, so a far bottle draws a small arrow and a near one draws a big one.
void drawArrow(bool pointLeft, char tier) {
  float k = scaleForTier(tier);
  int halfShaft = (int)round(32 * k);
  int headHalf  = (int)round(13 * k);
  int wing      = (int)round(14 * k);
  DOT_PIXEL dotPix = dotPixelForTier(tier);

  int tipX   = pointLeft ? (CX - halfShaft) : (CX + halfShaft);
  int tailX  = pointLeft ? (CX + halfShaft) : (CX - halfShaft);
  int wingDX = pointLeft ? wing : -wing;
  int headTopY = CY - headHalf;
  int headBotY = CY + headHalf;

  Paint_DrawLine(tailX, CY, tipX, CY, WHITE, dotPix, LINE_STYLE_SOLID);
  Paint_DrawLine(tipX, CY, tipX + wingDX, headTopY, WHITE, dotPix, LINE_STYLE_SOLID);
  Paint_DrawLine(tipX, CY, tipX + wingDX, headBotY, WHITE, dotPix, LINE_STYLE_SOLID);

  drawCornerLabel(pointLeft ? "L" : "R");
}

void drawCentered(char tier) {
  float k = scaleForTier(tier);
  int radius = (int)round(14 * k);
  DOT_PIXEL dotPix = dotPixelForTier(tier);

  Paint_DrawCircle(CX, CY, radius, WHITE, dotPix, DRAW_FILL_EMPTY);
  Paint_DrawPoint(CX, CY, WHITE, dotPix, DOT_STYLE_DFT);
  drawCornerLabel("C");
}

void drawNone() {
  const char *label = "NO BOTTLE";
  int textWidth = strlen(label) * Font16.Width;
  Paint_DrawString_EN((128 - textWidth) / 2, 24, label, &Font16, WHITE, WHITE);
}

void renderSignal(char dir, char tier) {
  Paint_Clear(BLACK);
  switch (dir) {
    case 'L': drawArrow(true, tier);  break;
    case 'R': drawArrow(false, tier); break;
    case 'C': drawCentered(tier);     break;
    default:  drawNone();             break;
  }
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

  renderSignal('N', 'M');
}

void loop() {
  static char lastDir = 'N';
  static char lastTier = 'M';

  if (Serial.available() >= 2) {
    char dir = Serial.read();
    char tier = Serial.read();
    bool validDir = (dir == 'L' || dir == 'R' || dir == 'C' || dir == 'N');
    bool validTier = (tier == 'S' || tier == 'M' || tier == 'B');

    if (validDir && validTier && (dir != lastDir || tier != lastTier)) {
      renderSignal(dir, tier);
      lastDir = dir;
      lastTier = tier;
    }
  }
}
