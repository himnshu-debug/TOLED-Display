#include <math.h>
#include <string.h>
#include "OLED_Driver.h"
#include "GUI_paint.h"
#include "DEV_Config.h"
#include "Debug.h"
#include "ImageData.h"

// DEMO_CT: the display half of the water-bottle direction demo.
// A PC script (detect_and_point.py) watches the webcam for a bottle and
// sends one byte over Serial: 'L' (left), 'R' (right), 'C' (centered),
// or 'N' (none seen). This sketch just renders whichever sign matches.

UBYTE *BlackImage;

void drawArrow(bool pointLeft) {
  const int shaftY = 24, headTopY = 10, headBotY = 38;
  int tipX   = pointLeft ? 28 : 100;
  int tailX  = pointLeft ? 100 : 28;
  int wingDX = pointLeft ? 16 : -16;

  Paint_DrawLine(tailX, shaftY, tipX, shaftY, WHITE, DOT_PIXEL_4X4, LINE_STYLE_SOLID);
  Paint_DrawLine(tipX, shaftY, tipX + wingDX, headTopY, WHITE, DOT_PIXEL_3X3, LINE_STYLE_SOLID);
  Paint_DrawLine(tipX, shaftY, tipX + wingDX, headBotY, WHITE, DOT_PIXEL_3X3, LINE_STYLE_SOLID);

  const char *label = pointLeft ? "LEFT" : "RIGHT";
  int textWidth = strlen(label) * Font16.Width;
  Paint_DrawString_EN((128 - textWidth) / 2, 46, label, &Font16, WHITE, WHITE);
}

void drawCentered() {
  Paint_DrawCircle(64, 22, 14, WHITE, DOT_PIXEL_3X3, DRAW_FILL_EMPTY);
  Paint_DrawPoint(64, 22, WHITE, DOT_PIXEL_3X3, DOT_STYLE_DFT);
  const char *label = "CENTER";
  int textWidth = strlen(label) * Font16.Width;
  Paint_DrawString_EN((128 - textWidth) / 2, 46, label, &Font16, WHITE, WHITE);
}

void drawNone() {
  const char *label = "NO BOTTLE";
  int textWidth = strlen(label) * Font16.Width;
  Paint_DrawString_EN((128 - textWidth) / 2, 24, label, &Font16, WHITE, WHITE);
}

void renderSignal(char c) {
  Paint_Clear(BLACK);
  switch (c) {
    case 'L': drawArrow(true);  break;
    case 'R': drawArrow(false); break;
    case 'C': drawCentered();   break;
    default:  drawNone();       break;
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

  renderSignal('N');
}

void loop() {
  static char lastSignal = 'N';

  if (Serial.available() > 0) {
    char c = Serial.read();
    if ((c == 'L' || c == 'R' || c == 'C' || c == 'N') && c != lastSignal) {
      renderSignal(c);
      lastSignal = c;
    }
  }
}
