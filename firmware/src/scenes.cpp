#include "scenes.h"

#include <Arduino.h>

namespace {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

enum Kind { NONE, AQUARIUM, STARS, MATRIX, FIRE, RAIN, SUNSET, SNOW, AURORA };
Kind kind = NONE;

int rnd(int a, int b) { return a + (int)(esp_random() % (uint32_t)(b - a + 1)); }

// ---------------------------------------------------------------- aquario
struct Fish { float x, y, vx, phase; uint16_t body, fin; uint8_t size; };
struct Bubble { float x, y, v; uint8_t r; };
constexpr int N_FISH = 6, N_BUB = 10;
Fish fish[N_FISH];
Bubble bub[N_BUB];
constexpr int SAND_Y = SCENE_H - 18;
const int weedX[] = {28, 46, 150, 232, 290};

const uint16_t FISH_COLORS[][2] = {
  {rgb(255, 140, 40), rgb(255, 220, 120)},   // laranja (palhaco)
  {rgb(255, 215, 0), rgb(40, 40, 40)},       // amarelo
  {rgb(90, 170, 255), rgb(20, 60, 160)},     // azul
  {rgb(235, 80, 120), rgb(255, 180, 200)},   // rosa
  {rgb(120, 230, 160), rgb(30, 120, 80)},    // verde
};

void spawnFish(Fish& f, bool anywhere) {
  bool right = esp_random() & 1;
  f.vx = (rnd(4, 11) / 10.0f) * (right ? 1 : -1);
  f.x = anywhere ? rnd(0, SCENE_W) : (right ? -20 : SCENE_W + 20);
  f.y = rnd(40, SAND_Y - 14);
  f.phase = rnd(0, 628) / 100.0f;
  const uint16_t* c = FISH_COLORS[rnd(0, 4)];
  f.body = c[0];
  f.fin = c[1];
  f.size = rnd(5, 9);
}

void spawnBubble(Bubble& b, bool anywhere) {
  b.x = weedX[rnd(0, 4)] + rnd(-6, 6);
  b.y = anywhere ? rnd(10, SAND_Y) : SAND_Y;
  b.v = rnd(4, 9) / 10.0f;
  b.r = rnd(1, 2);
}

void aquariumBegin() {
  for (auto& f : fish) spawnFish(f, true);
  for (auto& b : bub) spawnBubble(b, true);
}

void drawFish(TFT_eSprite& s, const Fish& f, uint32_t fr) {
  int x = f.x, y = f.y + sinf(fr * 0.06f + f.phase) * 2;
  int dir = f.vx > 0 ? 1 : -1;
  int L = f.size, H = f.size * 0.55f;
  // cauda abanando
  int wag = (fr / 4 + (int)(f.phase * 10)) % 2;
  s.fillTriangle(x - dir * L, y, x - dir * (L + 6), y - 4 - wag, x - dir * (L + 6), y + 4 + wag, f.fin);
  s.fillEllipse(x, y, L, H, f.body);
  s.drawFastVLine(x - dir * (L / 3), y - H + 1, H * 2 - 1, f.fin);  // listra
  s.fillCircle(x + dir * (L - 3), y - 1, 1, TFT_WHITE);
  s.drawPixel(x + dir * (L - 3), y - 1, TFT_BLACK);
}

void aquariumFrame(TFT_eSprite& s, uint32_t fr) {
  // agua em degrade (faixas, estilo pixel art)
  for (int y = 0; y < SAND_Y; y += 4) {
    uint8_t k = y * 255 / SAND_Y;
    s.fillRect(0, y, SCENE_W, 4, rgb(10, 70 - k / 6, 120 - k / 3));
  }
  // raios de luz
  for (int i = 0; i < 3; i++) {
    int x0 = 40 + i * 110 + sinf(fr * 0.01f + i) * 12;
    s.drawLine(x0, 0, x0 - 30, SAND_Y, rgb(30, 100, 150));
  }
  // areia com pedrinhas fixas
  s.fillRect(0, SAND_Y, SCENE_W, SCENE_H - SAND_Y, rgb(194, 164, 107));
  for (int x = 3; x < SCENE_W; x += 11) s.drawPixel(x, SAND_Y + 3 + (x * 7) % 11, rgb(150, 120, 80));
  // algas balancando
  for (int i = 0; i < 5; i++) {
    int hgt = 40 + (i * 17) % 50;
    for (int k = 0; k < hgt; k += 3) {
      float sw = sinf(fr * 0.05f + k * 0.15f + i) * k * 0.18f;
      s.fillRect(weedX[i] + sw, SAND_Y - k, 3, 3, (k / 3) % 2 ? rgb(40, 160, 80) : rgb(30, 120, 60));
    }
  }
  // bau do tesouro
  s.fillRect(188, SAND_Y - 10, 22, 12, rgb(120, 70, 30));
  s.drawRect(188, SAND_Y - 10, 22, 12, rgb(70, 40, 15));
  s.fillRect(197, SAND_Y - 6, 4, 4, rgb(255, 210, 60));

  for (auto& f : fish) {
    f.x += f.vx;
    if (f.x < -30 || f.x > SCENE_W + 30) spawnFish(f, false);
    drawFish(s, f, fr);
  }
  for (auto& b : bub) {
    b.y -= b.v;
    if (b.y < 2) spawnBubble(b, false);
    s.drawCircle(b.x + sinf(b.y * 0.15f) * 2, b.y, b.r, rgb(170, 220, 255));
  }
}

// ---------------------------------------------------------------- ceu estrelado
struct Star { int16_t x, y; float ph; uint8_t big; };
constexpr int N_STAR = 70;
Star stars[N_STAR];
struct Shoot { float x, y; int life; } shoot = {0, 0, 0};
uint8_t windows[40];  // janelas acesas da cidade (bits)

void starsBegin() {
  for (auto& st : stars) {
    st.x = rnd(0, SCENE_W - 1);
    st.y = rnd(0, SCENE_H - 70);
    st.ph = rnd(0, 628) / 100.0f;
    st.big = rnd(0, 9) == 0;
  }
  for (auto& w : windows) w = esp_random();
  shoot.life = 0;
}

void starsFrame(TFT_eSprite& s, uint32_t fr) {
  for (int y = 0; y < SCENE_H; y += 4) s.fillRect(0, y, SCENE_W, 4, rgb(4, 6 + y / 12, 16 + y / 5));
  for (auto& st : stars) {
    float b = (sinf(fr * 0.08f + st.ph) + 1) * 0.5f;
    uint8_t v = 60 + b * 195;
    s.drawPixel(st.x, st.y, rgb(v, v, v));
    if (st.big && b > 0.6f) {
      s.drawPixel(st.x - 1, st.y, rgb(v / 2, v / 2, v / 2));
      s.drawPixel(st.x + 1, st.y, rgb(v / 2, v / 2, v / 2));
      s.drawPixel(st.x, st.y - 1, rgb(v / 2, v / 2, v / 2));
      s.drawPixel(st.x, st.y + 1, rgb(v / 2, v / 2, v / 2));
    }
  }
  // lua com crateras (abaixo das informacoes, acima da cidade)
  const int MX = 276, MY = 140;
  s.fillCircle(MX, MY, 14, rgb(235, 230, 200));
  s.fillCircle(MX - 5, MY - 4, 3, rgb(200, 195, 170));
  s.fillCircle(MX + 5, MY + 5, 2, rgb(200, 195, 170));
  s.fillCircle(MX + 4, MY - 7, 2, rgb(210, 205, 180));
  // estrela cadente de vez em quando
  if (shoot.life <= 0 && rnd(0, 220) == 0) shoot = {(float)rnd(20, 180), (float)rnd(125, 150), 26};
  if (shoot.life > 0) {
    shoot.x += 5;
    shoot.y += 2;
    shoot.life--;
    for (int t = 0; t < 4; t++)
      s.drawLine(shoot.x - t * 5, shoot.y - t * 2, shoot.x - t * 5 - 5, shoot.y - t * 2 - 2, rgb(255 - t * 50, 255 - t * 50, 255 - t * 40));
  }
  // silhueta da cidade com janelas que acendem e apagam
  const int8_t bh[] = {28, 40, 22, 52, 34, 46, 26, 58, 30, 42, 24, 48, 36, 30, 44, 26};
  for (int i = 0; i < 16; i++) {
    int x = i * 20, h = bh[i] * 3 / 2, y = SCENE_H - h;
    s.fillRect(x, y, 20, h, rgb(14, 16, 26));
    for (int wy = 0; wy < h / 7; wy++)
      for (int wx = 0; wx < 3; wx++) {
        int bit = (i * 5 + wy * 3 + wx) % 40;
        if (windows[bit] & (1 << ((wy + wx) & 7)))
          s.fillRect(x + 3 + wx * 6, y + 4 + wy * 7, 3, 3, rgb(255, 205, 90));
      }
  }
  if (fr % 45 == 0) windows[rnd(0, 39)] ^= 1 << rnd(0, 7);
}

// ---------------------------------------------------------------- chuva Matrix
constexpr int MCOLS = SCENE_W / 8, MROWS = SCENE_H / 8 + 1, MTRAIL = 12;
float mHead[MCOLS], mSpeed[MCOLS];
char mGlyph[MCOLS][MROWS];
const char GLYPHS[] = "0123456789ABCDEFZ$#@%&*+=<>?";

void matrixBegin() {
  for (int c = 0; c < MCOLS; c++) {
    mHead[c] = -rnd(0, 40);
    mSpeed[c] = rnd(15, 45) / 100.0f;
    for (int r = 0; r < MROWS; r++) mGlyph[c][r] = GLYPHS[rnd(0, sizeof(GLYPHS) - 2)];
  }
}

void matrixFrame(TFT_eSprite& s, uint32_t fr) {
  s.fillSprite(rgb(0, 8, 2));
  s.setTextDatum(TL_DATUM);
  for (int c = 0; c < MCOLS; c++) {
    mHead[c] += mSpeed[c];
    if (mHead[c] - MTRAIL > MROWS) {
      mHead[c] = -rnd(0, 20);
      mSpeed[c] = rnd(15, 45) / 100.0f;
    }
    if (rnd(0, 6) == 0) mGlyph[c][rnd(0, MROWS - 1)] = GLYPHS[rnd(0, sizeof(GLYPHS) - 2)];
    int head = (int)mHead[c];
    for (int t = 0; t < MTRAIL; t++) {
      int r = head - t;
      if (r < 0 || r >= MROWS) continue;
      uint16_t col = t == 0 ? rgb(200, 255, 210) : rgb(0, 230 - t * 17, 70 - t * 5);
      s.setTextColor(col);
      char g[2] = {mGlyph[c][r], 0};
      s.drawString(g, c * 8 + 1, r * 8, 1);
    }
  }
}

// ---------------------------------------------------------------- fogo
constexpr int FC = 4, FW = SCENE_W / FC, FH = SCENE_H / FC;  // celulas de 4 px
uint8_t heat[FW * FH];
uint16_t firePal[37];

void fireBegin() {
  memset(heat, 0, sizeof(heat));
  for (int x = 0; x < FW; x++) heat[(FH - 1) * FW + x] = 36;
  // preto -> vinho -> vermelho -> laranja -> amarelo -> branco
  for (int i = 0; i < 37; i++) {
    float t = i / 36.0f;
    uint8_t r = t < 0.5f ? t * 2 * 255 : 255;
    uint8_t g = t < 0.35f ? 0 : (t < 0.85f ? (t - 0.35f) * 2 * 255 : 255);
    uint8_t b = t < 0.8f ? 0 : (t - 0.8f) * 5 * 255;
    firePal[i] = rgb(r, g > 255 ? 255 : g, b);
  }
  firePal[0] = rgb(12, 14, 18);
}

void fireFrame(TFT_eSprite& s, uint32_t fr) {
  // propagacao classica: cada celula sobe, esfria um pouco e desvia para o lado
  for (int x = 0; x < FW; x++) {
    for (int y = 1; y < FH; y++) {
      int src = y * FW + x;
      uint8_t p = heat[src];
      if (p == 0) {
        heat[src - FW] = 0;
        continue;
      }
      uint32_t rr = esp_random();
      int r = rr & 3;
      int dst = src - FW - r + 1;
      if (dst < 0) dst = 0;
      // esfria um pouco mais rapido que o classico: a chama ocupa ~2/3 de baixo
      // e deixa o topo escuro, onde ficam a hora e a data
      int cool = (r & 1) + (((rr >> 8) % 3) == 0);
      heat[dst] = p > cool ? p - cool : 0;
    }
  }
  // base oscila para a chama "respirar"
  for (int x = 0; x < FW; x++) heat[(FH - 1) * FW + x] = (rnd(0, 9) == 0) ? 28 : 36;
  for (int y = 0; y < FH; y++)
    for (int x = 0; x < FW; x++) s.fillRect(x * FC, y * FC, FC, FC, firePal[heat[y * FW + x]]);
}

// ---------------------------------------------------------------- noite de chuva
struct Drop { float x, y, v; };
struct Ripple { int16_t x, y; uint8_t r, maxR; };
constexpr int N_DROP = 45, N_RIP = 10;
Drop drops[N_DROP];
Ripple rips[N_RIP];
int rainFlash = 0, boltX = 160;

void rainBegin() {
  for (auto& d : drops) {
    d.x = rnd(0, SCENE_W + 40);
    d.y = rnd(0, SCENE_H);
    d.v = rnd(50, 95) / 10.0f;
  }
  for (auto& rp : rips) {
    rp.x = rnd(16, SCENE_W - 16);
    rp.y = rnd(214, SCENE_H - 4);
    rp.maxR = rnd(5, 11);
    rp.r = rnd(0, rp.maxR);
  }
  rainFlash = 0;
}

void rainFrame(TFT_eSprite& s, uint32_t fr) {
  if (rainFlash <= 0 && rnd(0, 240) == 0) {
    rainFlash = 4;
    boltX = rnd(40, SCENE_W - 40);
  }
  int fl = rainFlash > 0 ? rainFlash * 18 : 0;
  if (rainFlash > 0) rainFlash--;

  for (int y = 0; y < 210; y += 6) {
    uint8_t r = min(255, 6 + y / 24 + fl);
    uint8_t g = min(255, 10 + y / 16 + fl);
    uint8_t b = min(255, 22 + y / 10 + fl * 4 / 3);
    s.fillRect(0, y, SCENE_W, 6, rgb(r, g, b));
  }
  // chao molhado / pocas
  s.fillRect(0, 210, SCENE_W, 30, rgb(8 + fl / 3, 14 + fl / 3, 26 + fl / 2));
  s.drawFastHLine(0, 210, SCENE_W, rgb(28, 42, 68));

  // relampago
  if (fl > 20) {
    int bx = boltX, by = 16;
    for (int i = 0; i < 6; i++) {
      int nx = bx + ((i & 1) ? 10 : -12), ny = by + 26;
      s.drawLine(bx, by, nx, ny, rgb(230, 245, 255));
      s.drawLine(bx + 1, by, nx + 1, ny, rgb(160, 200, 255));
      bx = nx;
      by = ny;
    }
  }
  // nuvens escuras no topo
  for (int i = 0; i < 9; i++) {
    int cx = (i * 42 + (fr / 6) % 42) - 10;
    s.fillEllipse(cx, 8 + (i & 1) * 4, 28, 12, rgb(18 + fl / 3, 24 + fl / 3, 38 + fl / 2));
  }
  // ondas das pocas
  for (auto& rp : rips) {
    if (fr % 2 == 0) rp.r++;
    if (rp.r >= rp.maxR) {
      rp.x = rnd(16, SCENE_W - 16);
      rp.y = rnd(214, SCENE_H - 4);
      rp.maxR = rnd(5, 11);
      rp.r = 1;
    }
    uint8_t v = 130 - rp.r * 9;
    s.drawEllipse(rp.x, rp.y, rp.r * 2, max(1, rp.r / 2), rgb(v / 2, v * 3 / 4, v));
  }
  // gotas de chuva
  for (auto& d : drops) {
    d.x -= d.v * 0.28f;
    d.y += d.v;
    if (d.y > 218 || d.x < -10) {
      d.x = rnd(0, SCENE_W + 50);
      d.y = rnd(-20, 10);
      d.v = rnd(55, 95) / 10.0f;
    }
    uint16_t col = d.v > 7.5f ? rgb(140, 190, 245) : rgb(75, 115, 170);
    s.drawLine((int)d.x, (int)d.y, (int)(d.x + 3), (int)(d.y - 9), col);
  }
}

// ---------------------------------------------------------------- synthwave
void sunsetBegin() {
  for (int i = 0; i < 32; i++) {
    stars[i].x = rnd(0, SCENE_W - 1);
    stars[i].y = rnd(0, 78);
    stars[i].ph = rnd(0, 628) / 100.0f;
  }
}

void sunsetFrame(TFT_eSprite& s, uint32_t fr) {
  constexpr int HY = 142;
  // ceu: roxo escuro -> magenta -> laranja
  for (int y = 0; y < HY; y += 3) {
    float t = (float)y / HY;
    uint8_t r = 18 + t * t * 210;
    uint8_t g = 6 + (t > 0.5f ? (t - 0.5f) * 140 : t * 20);
    uint8_t b = 42 + (t < 0.6f ? t * 90 : (1.0f - t) * 130);
    s.fillRect(0, y, SCENE_W, 3, rgb(r, g, b));
  }
  for (int i = 0; i < 32; i++) {
    uint8_t v = 100 + (sinf(fr * 0.07f + stars[i].ph) + 1) * 75;
    s.drawPixel(stars[i].x, stars[i].y, rgb(v, v * 3 / 4, v));
  }
  // sol retro listrado
  constexpr int SX = 160, SY = 104, SR = 36;
  for (int dy = -SR; dy <= SR; dy++) {
    int y = SY + dy;
    if (y >= HY) continue;
    if (dy > 4 && ((dy + (int)(fr / 3)) % 7) < 2) continue;  // faixas cortadas
    int hw = sqrtf(SR * SR - dy * dy);
    float t = (float)(dy + SR) / (2 * SR);
    uint8_t g = 235 - t * 195;
    uint8_t b = t < 0.4f ? 50 : (t - 0.4f) * 220;
    s.drawFastHLine(SX - hw, y, hw * 2, rgb(255, g, b));
  }
  // montanhas no horizonte
  const int16_t mx[] = {0, 35, 68, 102, 132, 160, 192, 224, 260, 292, 320};
  const int16_t my[] = {HY, 120, 134, 114, 136, 128, 135, 112, 132, 118, HY};
  for (int i = 0; i < 10; i++) {
    s.fillTriangle(mx[i], HY, mx[i], my[i], mx[i + 1], my[i + 1], rgb(20, 8, 38));
    s.fillTriangle(mx[i], HY, mx[i + 1], my[i + 1], mx[i + 1], HY, rgb(20, 8, 38));
    s.drawLine(mx[i], my[i], mx[i + 1], my[i + 1], rgb(0, 220, 255));
  }
  // chao com grade neon em perspectiva
  s.fillRect(0, HY, SCENE_W, SCENE_H - HY, rgb(14, 4, 28));
  s.drawFastHLine(0, HY, SCENE_W, rgb(0, 240, 255));
  uint16_t gridCol = rgb(215, 35, 155);
  for (int i = -7; i <= 7; i++) s.drawLine(160 + i * 7, HY + 1, 160 + i * 48, SCENE_H - 1, gridCol);
  float phase = (fr % 16) / 16.0f;
  for (int i = 0; i < 8; i++) {
    float p = (i + phase) / 8.0f;
    int y = HY + 2 + (int)(p * p * (SCENE_H - HY - 2));
    s.drawFastHLine(0, y, SCENE_W, gridCol);
  }
}

// ---------------------------------------------------------------- nevasca
struct Flake { float x, y, vy; uint8_t sz, ph; };
constexpr int N_FLAKE = 55;
Flake flakes[N_FLAKE];

void snowBegin() {
  for (auto& f : flakes) {
    f.x = rnd(0, SCENE_W - 1);
    f.y = rnd(0, SCENE_H - 1);
    f.vy = rnd(6, 18) / 10.0f;
    f.sz = rnd(0, 5) == 0 ? 2 : (rnd(0, 2) == 0 ? 1 : 0);
    f.ph = rnd(0, 250);
  }
}

void snowFrame(TFT_eSprite& s, uint32_t fr) {
  for (int y = 0; y < 214; y += 6) s.fillRect(0, y, SCENE_W, 6, rgb(10 + y / 14, 18 + y / 10, 34 + y / 7));
  // montanhas nevadas ao fundo
  s.fillTriangle(10, 216, 85, 148, 165, 216, rgb(42, 58, 86));
  s.fillTriangle(85, 148, 63, 168, 108, 168, rgb(200, 216, 236));
  s.fillTriangle(140, 216, 230, 138, 315, 216, rgb(34, 50, 76));
  s.fillTriangle(230, 138, 204, 162, 256, 162, rgb(210, 224, 242));
  // pinheiros com neve
  const int tx[] = {36, 74, 152, 248, 286};
  const int th[] = {38, 28, 34, 42, 30};
  for (int i = 0; i < 5; i++) {
    int x = tx[i], h = th[i], y0 = 216;
    s.fillRect(x - 2, y0 - 6, 4, 6, rgb(70, 45, 25));
    for (int t = 0; t < 3; t++) {
      int ty = y0 - 6 - t * (h / 3), tw = 14 - t * 3, tht = h / 2;
      s.fillTriangle(x - tw, ty, x, ty - tht, x + tw, ty, rgb(20, 65, 50));
      s.drawLine(x - tw + 2, ty - 2, x, ty - tht, rgb(210, 228, 245));
    }
  }
  // chao coberto de neve
  s.fillRect(0, 214, SCENE_W, 26, rgb(215, 228, 242));
  s.fillEllipse(80, 214, 65, 6, rgb(230, 240, 250));
  s.fillEllipse(240, 215, 80, 7, rgb(200, 215, 235));
  // flocos caindo com vento suave
  for (auto& f : flakes) {
    f.y += f.vy;
    f.x += sinf(fr * 0.04f + f.ph * 0.1f) * 0.45f;
    if (f.y >= 224) {
      f.y = -2;
      f.x = rnd(0, SCENE_W - 1);
    }
    if (f.x < 0) f.x += SCENE_W;
    if (f.x >= SCENE_W) f.x -= SCENE_W;
    int ix = (int)f.x, iy = (int)f.y;
    uint16_t c = f.sz == 0 ? rgb(160, 180, 210) : rgb(245, 250, 255);
    if (f.sz == 2) {
      s.drawFastHLine(ix - 1, iy, 3, c);
      s.drawFastVLine(ix, iy - 1, 3, c);
    } else if (f.sz == 1) {
      s.fillRect(ix, iy, 2, 2, c);
    } else {
      s.drawPixel(ix, iy, c);
    }
  }
}

// ---------------------------------------------------------------- aurora boreal
void auroraBegin() {
  for (int i = 0; i < 45; i++) {
    stars[i].x = rnd(0, SCENE_W - 1);
    stars[i].y = rnd(0, 175);
    stars[i].ph = rnd(0, 628) / 100.0f;
  }
}

void auroraFrame(TFT_eSprite& s, uint32_t fr) {
  s.fillSprite(rgb(4, 8, 18));
  for (int i = 0; i < 45; i++) {
    uint8_t v = 80 + (sinf(fr * 0.06f + stars[i].ph) + 1) * 80;
    s.drawPixel(stars[i].x, stars[i].y, rgb(v, v, v));
  }
  // cortinas de luz da aurora (duas ondas superpostas)
  for (int x = 0; x < SCENE_W; x += 4) {
    float w1 = sinf(x * 0.022f + fr * 0.04f) * 16 + cosf(x * 0.05f - fr * 0.025f) * 8;
    int y1 = 68 + (int)w1;
    for (int k = 0; k < 10; k++) {
      uint8_t g = 210 - k * 19;
      uint8_t b = 120 + k * 8;
      uint8_t r = k * 12;
      s.fillRect(x, y1 - k * 4, 4, 4, rgb(r / 2, g, b));
    }
    float w2 = cosf(x * 0.018f - fr * 0.035f) * 18 + sinf(x * 0.04f + fr * 0.02f) * 7;
    int y2 = 112 + (int)w2;
    for (int k = 0; k < 8; k++) {
      uint8_t g = 160 - k * 18;
      uint8_t b = 190 - k * 14;
      uint8_t r = 50 + k * 14;
      s.fillRect(x, y2 - k * 4, 4, 4, rgb(r, g, b));
    }
  }
  // montanhas e lago com reflexo
  s.fillTriangle(0, 198, 70, 152, 160, 198, rgb(10, 16, 28));
  s.fillTriangle(120, 198, 210, 146, 320, 198, rgb(12, 20, 34));
  s.fillRect(0, 198, SCENE_W, 42, rgb(6, 16, 26));
  s.drawFastHLine(0, 198, SCENE_W, rgb(25, 70, 80));
  for (int y = 202; y < SCENE_H; y += 4) {
    int x0 = 40 + (int)(sinf(y * 0.3f + fr * 0.06f) * 25);
    s.drawFastHLine(x0, y, 90, rgb(15, 95, 75));
    s.drawFastHLine(x0 + 130, y + 2, 80, rgb(25, 75, 105));
  }
}

}  // namespace

void sceneBegin(const char* name) {
  if (!strcmp(name, "aquarium")) { kind = AQUARIUM; aquariumBegin(); }
  else if (!strcmp(name, "stars")) { kind = STARS; starsBegin(); }
  else if (!strcmp(name, "matrix")) { kind = MATRIX; matrixBegin(); }
  else if (!strcmp(name, "fire")) { kind = FIRE; fireBegin(); }
  else if (!strcmp(name, "rain")) { kind = RAIN; rainBegin(); }
  else if (!strcmp(name, "sunset")) { kind = SUNSET; sunsetBegin(); }
  else if (!strcmp(name, "snow")) { kind = SNOW; snowBegin(); }
  else if (!strcmp(name, "aurora")) { kind = AURORA; auroraBegin(); }
  else kind = NONE;
}

void sceneFrame(TFT_eSprite& s, uint32_t fr) {
  switch (kind) {
    case AQUARIUM: aquariumFrame(s, fr); break;
    case STARS: starsFrame(s, fr); break;
    case MATRIX: matrixFrame(s, fr); break;
    case FIRE: fireFrame(s, fr); break;
    case RAIN: rainFrame(s, fr); break;
    case SUNSET: sunsetFrame(s, fr); break;
    case SNOW: snowFrame(s, fr); break;
    case AURORA: auroraFrame(s, fr); break;
    default: s.fillSprite(rgb(12, 14, 18)); break;
  }
}
