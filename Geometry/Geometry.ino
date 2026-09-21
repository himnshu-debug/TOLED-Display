#include <math.h>
#include <avr/pgmspace.h>
#include "OLED_Driver.h"
#include "GUI_paint.h"
#include "DEV_Config.h"
#include "Debug.h"
#include "ImageData.h"

UBYTE *BlackImage;

const int   CX    = 64;   // center of the 128x64 landscape screen
const int   CY    = 32;
const float FOCAL    = 60.0;
const float CAM_DIST = 55.0;

const unsigned long SHAPE_DURATION_MS = 10000; // 10 seconds per shape

#define MAX_FACE_VERTS 6
#define MAX_VERTS      18
#define MAX_EDGES      40
#define FACE_STRIDE    7   // 1 gap byte + MAX_FACE_VERTS index bytes

struct Vec3 { float x, y, z; };

// ---- diagonal line-hatch shading: gap=0 outline only, 1=solid, higher=sparser ----
bool hatchOn(int x, int y, uint8_t gap) {
  if (gap == 0) return false;
  if (gap == 1) return true;
  return ((x + y) % gap) == 0;
}

void rotateVec3(Vec3 &v, float ax, float ay, float az) {
  float cx1 = cos(ax), sx1 = sin(ax);
  float y1 = v.y * cx1 - v.z * sx1;
  float z1 = v.y * sx1 + v.z * cx1;
  v.y = y1; v.z = z1;

  float cy1 = cos(ay), sy1 = sin(ay);
  float x2 = v.x * cy1 + v.z * sy1;
  float z2 = -v.x * sy1 + v.z * cy1;
  v.x = x2; v.z = z2;

  float cz1 = cos(az), sz1 = sin(az);
  float x3 = v.x * cz1 - v.y * sz1;
  float y3 = v.x * sz1 + v.y * cz1;
  v.x = x3; v.y = y3;
}

void projectVec3(Vec3 v, int *sx, int *sy) {
  float scale = FOCAL / (CAM_DIST + v.z);
  *sx = CX + (int)round(v.x * scale);
  *sy = CY + (int)round(v.y * scale);
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
    int interX[4];
    int count = 0;
    for (int i = 0; i < n; i++) {
      int j = (i + 1) % n;
      int y0 = ys[i], y1 = ys[j];
      if (y0 == y1) continue;
      if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
        float t = (float)(y - y0) / (float)(y1 - y0);
        int x = xs[i] + (int)round((xs[j] - xs[i]) * t);
        if (count < 4) interX[count++] = x;
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

// =========================================================================
// Shape data lives entirely in PROGMEM (flash), not RAM.
// Each face record is FACE_STRIDE bytes: [gap, idx0..idx(MAX_FACE_VERTS-1)], -1 padded.
// =========================================================================
struct ShapeDef {
  const int8_t *verts;
  uint8_t nVerts;
  const int8_t *faces;
  uint8_t nFaces;
};

// ---- BOX (cuboid): 8 verts, 6 quad faces ----
const int8_t boxVerts[] PROGMEM = {
  -12,-9,-9,  12,-9,-9,  12,9,-9,  -12,9,-9,
  -12,-9,9,   12,-9,9,   12,9,9,   -12,9,9
};
const int8_t boxFaces[] PROGMEM = {
  2, 0,1,2,3, -1,-1,
  4, 4,7,6,5, -1,-1,
  3, 0,3,7,4, -1,-1,
  3, 1,5,6,2, -1,-1,
  1, 0,4,5,1, -1,-1,
  0, 3,2,6,7, -1,-1
};

// ---- SPHERE (octahedron): 6 verts, 8 triangular faces ----
const int8_t sphereVerts[] PROGMEM = {
  13,0,0,  -13,0,0,  0,13,0,  0,-13,0,  0,0,13,  0,0,-13
};
const int8_t sphereFaces[] PROGMEM = {
  2, 0,2,4, -1,-1,-1,
  0, 2,1,4, -1,-1,-1,
  3, 1,3,4, -1,-1,-1,
  1, 3,0,4, -1,-1,-1,
  1, 2,0,5, -1,-1,-1,
  3, 1,2,5, -1,-1,-1,
  0, 3,1,5, -1,-1,-1,
  2, 0,3,5, -1,-1,-1
};

// ---- CYLINDER (hexagonal prism): 12 verts, 8 faces (6 side quads + 2 hex caps) ----
const int8_t cylVerts[] PROGMEM = {
  10,-13,0,  5,-13,9,   -5,-13,9,  -10,-13,0,  -5,-13,-9,  5,-13,-9,
  10,13,0,   5,13,9,    -5,13,9,   -10,13,0,   -5,13,-9,   5,13,-9
};
const int8_t cylFaces[] PROGMEM = {
  3, 0,1,7,6,   -1,-1,
  3, 1,2,8,7,   -1,-1,
  3, 2,3,9,8,   -1,-1,
  3, 3,4,10,9,  -1,-1,
  3, 4,5,11,10, -1,-1,
  3, 5,0,6,11,  -1,-1,
  1, 0,1,2,3,4,5,
  0, 11,10,9,8,7,6
};

// ---- CONE (hexagonal base): 7 verts, 7 faces (6 tri sides + 1 hex base) ----
const int8_t coneVerts[] PROGMEM = {
  11,10,0,   6,10,10,   -6,10,10,  -11,10,0,  -6,10,-10,  6,10,-10,
  0,-14,0
};
const int8_t coneFaces[] PROGMEM = {
  2, 0,1,6, -1,-1,-1,
  2, 1,2,6, -1,-1,-1,
  2, 2,3,6, -1,-1,-1,
  2, 3,4,6, -1,-1,-1,
  2, 4,5,6, -1,-1,-1,
  2, 5,0,6, -1,-1,-1,
  0, 5,4,3,2,1,0
};

// ---- PYRAMID (square base): 5 verts, 5 faces ----
const int8_t pyramidVerts[] PROGMEM = {
  -10,10,-10,  10,10,-10,  10,10,10,  -10,10,10,
  0,-14,0
};
const int8_t pyramidFaces[] PROGMEM = {
  2, 0,1,4,   -1,-1,-1,
  3, 1,2,4,   -1,-1,-1,
  1, 2,3,4,   -1,-1,-1,
  4, 3,0,4,   -1,-1,-1,
  0, 3,2,1,0, -1,-1
};

// ---- WEDGE: 6 verts, 5 faces ----
const int8_t wedgeVerts[] PROGMEM = {
  -10,-10,-10,  -10,10,-10,  10,10,-10,
  -10,-10,10,   -10,10,10,   10,10,10
};
const int8_t wedgeFaces[] PROGMEM = {
  2, 0,1,2,   -1,-1,-1,
  4, 3,5,4,   -1,-1,-1,
  0, 1,2,5,4, -1,-1,
  3, 0,3,4,1, -1,-1,
  1, 0,2,5,3, -1,-1
};

// ---- TORUS (6 major x 3 minor segments): 18 verts, 18 quad faces ----
const int8_t torusVerts[] PROGMEM = {
  15,0,0,   8,4,0,   8,-4,0,
  8,0,13,   4,4,7,   4,-4,7,
  -8,0,13,  -4,4,7,  -4,-4,7,
  -15,0,0,  -8,4,0,  -8,-4,0,
  -8,0,-13, -4,4,-7, -4,-4,-7,
  8,0,-13,  4,4,-7,  4,-4,-7
};
const int8_t torusFaces[] PROGMEM = {
  2, 0,3,4,1,    -1,-1,
  1, 1,4,5,2,    -1,-1,
  0, 2,5,3,0,    -1,-1,
  2, 3,6,7,4,    -1,-1,
  1, 4,7,8,5,    -1,-1,
  0, 5,8,6,3,    -1,-1,
  2, 6,9,10,7,   -1,-1,
  1, 7,10,11,8,  -1,-1,
  0, 8,11,9,6,   -1,-1,
  2, 9,12,13,10, -1,-1,
  1, 10,13,14,11,-1,-1,
  0, 11,14,12,9, -1,-1,
  2, 12,15,16,13,-1,-1,
  1, 13,16,17,14,-1,-1,
  0, 14,17,15,12,-1,-1,
  2, 15,0,1,16,  -1,-1,
  1, 16,1,2,17,  -1,-1,
  0, 17,2,0,15,  -1,-1
};

const ShapeDef shapes[7] = {
  { boxVerts,    8,  boxFaces,    6 },
  { sphereVerts, 6,  sphereFaces, 8 },
  { cylVerts,    12, cylFaces,    8 },
  { coneVerts,   7,  coneFaces,   7 },
  { pyramidVerts,5,  pyramidFaces,5 },
  { wedgeVerts,  6,  wedgeFaces,  5 },
  { torusVerts,  18, torusFaces,  18 }
};
const int NUM_SHAPES = 7;

// ---- shared runtime buffers (sized once for the largest shape, not per-shape) ----
int sx[MAX_VERTS], sy[MAX_VERTS];
int8_t sharedEdges[MAX_EDGES][2];
int sharedEdgeCount = 0;

bool edgeExists(int8_t a, int8_t b) {
  for (int i = 0; i < sharedEdgeCount; i++) {
    if ((sharedEdges[i][0] == a && sharedEdges[i][1] == b) ||
        (sharedEdges[i][0] == b && sharedEdges[i][1] == a)) return true;
  }
  return false;
}

void buildEdgesForShape(int shapeIndex) {
  const ShapeDef &shape = shapes[shapeIndex];
  sharedEdgeCount = 0;
  for (int f = 0; f < shape.nFaces; f++) {
    int8_t idxs[MAX_FACE_VERTS];
    int n = 0;
    for (int k = 0; k < MAX_FACE_VERTS; k++) {
      int8_t idx = (int8_t)pgm_read_byte(&shape.faces[f * FACE_STRIDE + 1 + k]);
      if (idx < 0) break;
      idxs[n++] = idx;
    }
    for (int k = 0; k < n; k++) {
      int8_t a = idxs[k], b = idxs[(k + 1) % n];
      if (!edgeExists(a, b) && sharedEdgeCount < MAX_EDGES) {
        sharedEdges[sharedEdgeCount][0] = a;
        sharedEdges[sharedEdgeCount][1] = b;
        sharedEdgeCount++;
      }
    }
  }
}

void drawShape(int shapeIndex, float ax, float ay, float az, float zoom) {
  const ShapeDef &shape = shapes[shapeIndex];
  for (int i = 0; i < shape.nVerts; i++) {
    Vec3 v;
    v.x = (int8_t)pgm_read_byte(&shape.verts[i * 3 + 0]) * zoom;
    v.y = (int8_t)pgm_read_byte(&shape.verts[i * 3 + 1]) * zoom;
    v.z = (int8_t)pgm_read_byte(&shape.verts[i * 3 + 2]) * zoom;
    rotateVec3(v, ax, ay, az);
    projectVec3(v, &sx[i], &sy[i]);
  }

  for (int f = 0; f < shape.nFaces; f++) {
    uint8_t gap = (uint8_t)pgm_read_byte(&shape.faces[f * FACE_STRIDE]);
    int fx[MAX_FACE_VERTS], fy[MAX_FACE_VERTS], n = 0;
    for (int k = 0; k < MAX_FACE_VERTS; k++) {
      int8_t idx = (int8_t)pgm_read_byte(&shape.faces[f * FACE_STRIDE + 1 + k]);
      if (idx < 0) break;
      fx[n] = sx[idx]; fy[n] = sy[idx]; n++;
    }
    if (n >= 3 && isFrontFacing(fx, fy, n)) fillPolygon(fx, fy, n, gap);
  }

  for (int e = 0; e < sharedEdgeCount; e++) {
    int a = sharedEdges[e][0], b = sharedEdges[e][1];
    Paint_DrawLine(sx[a], sy[a], sx[b], sy[b], WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  }
}

// =========================================================================
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
  static int shapeIndex = 0;
  static unsigned long shapeStart = 0;
  static bool needRebuild = true;
  static float ax = 0, ay = 0, az = 0;
  static float zoomPhase = 0;

  if (needRebuild) {
    buildEdgesForShape(shapeIndex);
    needRebuild = false;
  }

  if (millis() - shapeStart > SHAPE_DURATION_MS) {
    shapeStart = millis();
    shapeIndex = (shapeIndex + 1) % NUM_SHAPES;
    needRebuild = true;
  }

  float zoom = 1.0 + sin(zoomPhase) * 0.3; // breathes between 0.7x and 1.3x

  Paint_Clear(BLACK);
  drawShape(shapeIndex, ax, ay, az, zoom);
  OLED_1IN51_Display(BlackImage);

  ax += 0.05; ay += 0.07; az += 0.04;
  zoomPhase += 0.04;
  if (zoomPhase > 2 * PI) zoomPhase -= 2 * PI;

  Driver_Delay_ms(60);
}
