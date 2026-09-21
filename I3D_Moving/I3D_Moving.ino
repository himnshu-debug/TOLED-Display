#include <math.h>
#include "OLED_Driver.h"
#include "GUI_paint.h"
#include "DEV_Config.h"
#include "Debug.h"
#include "ImageData.h"

UBYTE *BlackImage;

const int screenWidth  = 128;
const int screenHeight = 64;

// --- Falling, Z-spinning shaded 3D cube ---
const int   cubeX          = 32;
const float cubeHalfSize   = 12;
const float cubeTilt       = 0.5;   // fixed camera tilt around X axis (reveals top/side faces)
const float cubeFocal      = 60.0;
const float cubeCamDist    = 50.0;
const float cubeAngleStep  = 0.10;  // spin speed around Z axis
const float cubeFallSpeed  = 1.0;
const float cubeYTop       = -40;
const float cubeYBottom    = 104;

struct Vec3 { float x, y, z; };

const int8_t cubeEdges[12][2] = {
  {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
};
const int8_t cubeFaces[6][4] = {
  {0,1,2,3}, {4,7,6,5}, {0,3,7,4}, {1,5,6,2}, {0,4,5,1}, {3,2,6,7}
};
const uint8_t cubeFaceGap[6] = { 2, 4, 3, 3, 1, 0 }; // front,back,left,right,top,bottom

// diagonal line-hatch shading: gap=0 outline only, 1=solid, higher=sparser
bool hatchOn(int x, int y, uint8_t gap) {
  if (gap == 0) return false;
  if (gap == 1) return true;
  return ((x + y) % gap) == 0;
}

bool isFrontFacing(int *xs, int *ys, int n) {
  long area = 0;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    area += (long)xs[i] * ys[j] - (long)xs[j] * ys[i];
  }
  return area > 0;
}

void fillPolygon(int *xs, int *ys, int n, uint8_t gap) {
  if (gap == 0) return;
  int yMin = ys[0], yMax = ys[0];
  for (int i = 1; i < n; i++) {
    if (ys[i] < yMin) yMin = ys[i];
    if (ys[i] > yMax) yMax = ys[i];
  }
  if (yMin < 0) yMin = 0;
  if (yMax > 63) yMax = 63;

  for (int y = yMin; y <= yMax; y++) {
    int interX[6];
    int count = 0;
    for (int i = 0; i < n; i++) {
      int j = (i + 1) % n;
      int y0 = ys[i], y1 = ys[j];
      if (y0 == y1) continue;
      if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
        float t = (float)(y - y0) / (float)(y1 - y0);
        int x = xs[i] + (int)round((xs[j] - xs[i]) * t);
        if (count < 6) interX[count++] = x;
      }
    }
    for (int i = 1; i < count; i++) {
      int key = interX[i], k = i - 1;
      while (k >= 0 && interX[k] > key) { interX[k + 1] = interX[k]; k--; }
      interX[k + 1] = key;
    }
    for (int p = 0; p + 1 < count; p += 2) {
      int xStart = interX[p], xEnd = interX[p + 1];
      if (xStart < 0) xStart = 0;
      if (xEnd > 127) xEnd = 127;
      for (int x = xStart; x <= xEnd; x++) {
        if (hatchOn(x, y, gap)) Paint_SetPixel(x, y, WHITE);
      }
    }
  }
}

void drawCube(int cx, int cy, float theta) {
  float s = cubeHalfSize;
  Vec3 verts[8] = {
    {-s, -s, -s}, { s, -s, -s}, { s,  s, -s}, {-s,  s, -s},
    {-s, -s,  s}, { s, -s,  s}, { s,  s,  s}, {-s,  s,  s}
  };

  float cosT = cos(theta), sinT = sin(theta);
  float cosP = cos(cubeTilt), sinP = sin(cubeTilt);

  int sx[8], sy[8];
  for (int i = 0; i < 8; i++) {
    float x1 = verts[i].x * cosT - verts[i].y * sinT;
    float y1 = verts[i].x * sinT + verts[i].y * cosT;
    float z1 = verts[i].z;

    float y2 = y1 * cosP - z1 * sinP;
    float z2 = y1 * sinP + z1 * cosP;

    float scale = cubeFocal / (cubeCamDist + z2);
    sx[i] = cx + (int)round(x1 * scale);
    sy[i] = cy + (int)round(y2 * scale);
  }

  for (int f = 0; f < 6; f++) {
    int fx[4], fy[4];
    for (int k = 0; k < 4; k++) { fx[k] = sx[cubeFaces[f][k]]; fy[k] = sy[cubeFaces[f][k]]; }
    if (isFrontFacing(fx, fy, 4)) fillPolygon(fx, fy, 4, cubeFaceGap[f]);
  }

  for (int e = 0; e < 12; e++) {
    int a = cubeEdges[e][0], b = cubeEdges[e][1];
    Paint_DrawLine(sx[a], sy[a], sx[b], sy[b], WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  }
}

// --- Rising text ---
const char *msg          = "HELLO FROM I3D LAB";
const int   msgWidth     = 18 * 5;
const int   msgBaseX     = (128 - msgWidth) / 2;
const int   msgWiggleAmp = 8;
const float msgRiseSpeed = 1.2;
const float msgYBottom   = 70;
const float msgYTop      = -12;

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
  static float cubeAngle   = 0;
  static float cubeY       = cubeYTop;
  static float msgY        = msgYBottom;
  static float wigglePhase = 0;

  Paint_Clear(BLACK);

  drawCube(cubeX, (int)round(cubeY), cubeAngle);

  int msgX = msgBaseX + (int)round(sin(wigglePhase) * msgWiggleAmp);
  Paint_DrawString_EN(msgX, (int)round(msgY), msg, &Font8, WHITE, WHITE);

  OLED_1IN51_Display(BlackImage);

  cubeAngle += cubeAngleStep;
  if (cubeAngle > 2 * PI) cubeAngle -= 2 * PI;

  cubeY += cubeFallSpeed;
  if (cubeY > cubeYBottom) cubeY = cubeYTop;

  msgY -= msgRiseSpeed;
  if (msgY < msgYTop) msgY = msgYBottom;

  wigglePhase += 0.15;
  if (wigglePhase > 2 * PI) wigglePhase -= 2 * PI;

  Driver_Delay_ms(50);
}
