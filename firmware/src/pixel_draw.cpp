#include "pixel_draw.h"

#include <string.h>

#include "pixel_assets.h"

namespace {
const PxGlyph* glyph(char c) {
  for (int i = 0; i < PX_FONT_N; i++)
    if (PX_FONT[i].ch == c) return &PX_FONT[i];
  return &PX_FONT[36];  // espaco (entrada logo depois de 0-9 e A-Z)
}
const PxIcon* icon(const char* name) {
  for (int i = 0; i < PX_ICONS_N; i++)
    if (!strcmp(PX_ICONS[i].name, name)) return &PX_ICONS[i];
  return nullptr;
}
}  // namespace

int pxWidth(const char* t) {
  int w = 0;
  for (; *t; t++) w += glyph(*t)->w + 1;
  return w > 0 ? w - 1 : 0;
}

int pxCharX(const char* t, int i) {
  int x = 0;
  for (int k = 0; k < i && t[k]; k++) x += glyph(t[k])->w + 1;
  return x;
}

void pxBig(TFT_eSprite& s, const char* t, int x0, int y0, int k, uint16_t col, int clip0, int clip1) {
  int gap = k >= 6 ? 2 : 0;
  int sz = k - gap;
  for (int pass = 0; pass < 2; pass++) {
    int x = x0;
    for (const char* p = t; *p; p++) {
      const PxGlyph* g = glyph(*p);
      for (int gy = 0; gy < 5; gy++)
        for (int gx = 0; gx < g->w; gx++) {
          if (!((g->bits >> (gy * 3 + gx)) & 1)) continue;
          int X = x + gx * k, Y = y0 + gy * k;
          if (X + k < clip0 || X >= clip1) continue;
          if (pass == 0) s.fillRect(X + 2, Y + 2, sz, sz, TFT_BLACK);  // sombra
          else if (k >= 8) s.fillRoundRect(X, Y, sz, sz, 2, col);
          else s.fillRect(X, Y, sz, sz, col);
        }
      x += (g->w + 1) * k;
    }
  }
}

bool pxHasIcon(const char* name) { return icon(name) != nullptr; }

void pxIconBig(TFT_eSprite& s, const char* name, int x0, int y0, int k) {
  const PxIcon* ic = icon(name);
  if (!ic) return;
  for (int i = 0; i < 64; i++)
    if (ic->px[i]) s.fillRect(x0 + (i % 8) * k, y0 + (i / 8) * k, k, k, ic->px[i]);
}

void pxBufClear(PxBuf b) { memset(b, 0, sizeof(PxBuf)); }

void pxBufText(PxBuf b, const char* t, int x0, int y0, uint16_t col, int clip0, int clip1, int dimChar, uint16_t dimCol) {
  int x = x0, idx = 0;
  for (const char* p = t; *p; p++, idx++) {
    const PxGlyph* g = glyph(*p);
    uint16_t c = idx == dimChar ? dimCol : col;
    for (int gy = 0; gy < 5; gy++)
      for (int gx = 0; gx < g->w; gx++) {
        if (!((g->bits >> (gy * 3 + gx)) & 1)) continue;
        int X = x + gx, Y = y0 + gy;
        if (X < clip0 || X >= clip1 || X > 31 || Y < 0 || Y > 7) continue;
        b[Y][X] = c;
      }
    x += g->w + 1;
    if (x >= clip1) break;
  }
}

void pxBufIcon(PxBuf b, const char* name, int x0) {
  const PxIcon* ic = icon(name);
  if (!ic) return;
  for (int i = 0; i < 64; i++)
    if (ic->px[i]) b[i / 8][x0 + i % 8] = ic->px[i];
}
