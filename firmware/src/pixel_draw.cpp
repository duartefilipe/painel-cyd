#include "pixel_draw.h"

#include <string.h>

#include "pixel_assets.h"

namespace {
const PxGlyph* glyph(char c) {
  for (int i = 0; i < PX_FONT_N; i++)
    if (PX_FONT[i].ch == c) return &PX_FONT[i];
  return &PX_FONT[36];  // espaco (entrada logo depois de 0-9 e A-Z)
}
const PxGlyph57* glyph57(char c) {
  for (int i = 0; i < PX_FONT57_N; i++)
    if (PX_FONT57[i].ch == c) return &PX_FONT57[i];
  return &PX_FONT57[36];
}
const PxIcon* icon(const char* name) {
  for (int i = 0; i < PX_ICONS_N; i++)
    if (!strcmp(PX_ICONS[i].name, name)) return &PX_ICONS[i];
  return nullptr;
}
int width57(const char* t) {
  int w = 0;
  for (; *t; t++) w += glyph57(*t)->w + 1;
  return w > 0 ? w - 1 : 0;
}
int scale57(int k) { return max(1, (k * 2 + 1) / 3); }

// Bitmask 7 segmentos (bits 0..6 = A,B,C,D,E,F,G) para '0'..'9'.
const uint8_t SEG7[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

void drawSegDigit(TFT_eSprite& s, int d, int x, int y, int k, uint16_t c) {
  uint8_t msk = SEG7[d];
  int gap = k >= 6 ? 2 : 0;
  int w = 3 * k - gap, h = 5 * k - gap;
  int t = max(1, (k * 2 + 1) / 3);
  int m = (h - t) / 2;
  int g = k >= 5 ? 2 : (k >= 3 ? 1 : 0);
  if (msk & 0x01) s.fillRect(x + g, y, w - 2 * g, t, c);                  // A (topo)
  if (msk & 0x40) s.fillRect(x + g, y + m, w - 2 * g, t, c);              // G (meio)
  if (msk & 0x08) s.fillRect(x + g, y + h - t, w - 2 * g, t, c);          // D (base)
  if (msk & 0x20) s.fillRect(x, y + g, t, m - g, c);                      // F (esq sup)
  if (msk & 0x02) s.fillRect(x + w - t, y + g, t, m - g, c);              // B (dir sup)
  if (msk & 0x10) s.fillRect(x, y + m + t, t, h - m - t - g, c);          // E (esq inf)
  if (msk & 0x04) s.fillRect(x + w - t, y + m + t, t, h - m - t - g, c);  // C (dir inf)
}
}  // namespace

int pxWidth(const char* t) {
  int w = 0;
  for (; *t; t++) w += glyph(*t)->w + 1;
  return w > 0 ? w - 1 : 0;
}

int pxTextW(const char* t, int k, int font) {
  if (font == PX_F_ARCADE || font == PX_F_CLEAN) return width57(t) * scale57(k);
  return pxWidth(t) * k;
}

int pxTextH(int k, int font) {
  if (font == PX_F_ARCADE || font == PX_F_CLEAN) return 7 * scale57(k);
  return 5 * k;
}

void pxBig(TFT_eSprite& s, const char* t, int x0, int y0, int k, uint16_t col, int clip0, int clip1, int font) {
  if (k <= 0) return;
  if (font == PX_F_ARCADE || font == PX_F_CLEAN) {
    int ks = scale57(k);
    int gap = (font == PX_F_ARCADE && ks >= 4) ? (ks >= 7 ? 2 : 1) : 0;
    int sz = ks - gap;
    for (int pass = 0; pass < 2; pass++) {
      int off = pass == 0 ? 2 : 0;
      uint16_t c = pass == 0 ? TFT_BLACK : col;
      int x = x0;
      for (const char* p = t; *p; p++) {
        const PxGlyph57* g = glyph57(*p);
        for (int gy = 0; gy < 7; gy++)
          for (int gx = 0; gx < g->w; gx++) {
            if (!((g->rows[gy] >> gx) & 1)) continue;
            int X = x + gx * ks, Y = y0 + gy * ks;
            if (X < clip0 || X + ks > clip1) continue;
            if (pass == 1 && font == PX_F_ARCADE && ks >= 6) s.fillRoundRect(X, Y, sz, sz, 2, c);
            else s.fillRect(X + off, Y + off, sz, sz, c);
          }
        x += (g->w + 1) * ks;
      }
    }
    return;
  }

  int gap = (font == PX_F_SEG) ? 0 : (k >= 6 ? 2 : (font == PX_F_DOTS && k >= 3 ? 1 : 0));
  int sz = k - gap;
  for (int pass = 0; pass < 2; pass++) {
    int off = pass == 0 ? 2 : 0;
    uint16_t c = pass == 0 ? TFT_BLACK : col;
    int x = x0;
    for (const char* p = t; *p; p++) {
      if (font == PX_F_SEG && *p >= '0' && *p <= '9') {
        if (x >= clip0 && x + 3 * k <= clip1) drawSegDigit(s, *p - '0', x + off, y0 + off, k, c);
        x += 4 * k;
        continue;
      }
      const PxGlyph* g = glyph(*p);
      for (int gy = 0; gy < 5; gy++)
        for (int gx = 0; gx < g->w; gx++) {
          if (!((g->bits >> (gy * 3 + gx)) & 1)) continue;
          int X = x + gx * k, Y = y0 + gy * k;
          if (X < clip0 || X + k > clip1) continue;
          if (font == PX_F_DOTS) {
            int r = max(1, sz / 2);
            s.fillCircle(X + off + k / 2, Y + off + k / 2, r, c);
          } else if (pass == 1 && font == PX_F_PIXEL && k >= 8) {
            s.fillRoundRect(X, Y, sz, sz, 2, c);
          } else {
            s.fillRect(X + off, Y + off, sz, sz, c);
          }
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
