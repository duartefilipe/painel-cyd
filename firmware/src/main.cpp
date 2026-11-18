// Painel CYD - a placa so desenha. Quem busca os dados e o servidor (Docker no
// Portainer). A placa entra no Wi-Fi e pede GET /api/screen a cada segundo.
//
// Resposta (chaves curtas):
// {"t":"14:32","d":"qui, 05 nov","pc":1,
//  "cfg":{"br":200,"inv":0,"rot":1},
//  "lay":{"n":"Trabalho","l":"sys","r":"w","b":"ev"},     <- o que vai em cada regiao
//  "sys":{"cpu":23.5,"ram":61.2,"ru":10.1,"rt":31.9,"dsk":72,"dn":123.4,"up":12.3},
//  "w":{"ok":1,"city":"Santa Maria","tc":24,"ic":"P","day":1,"mn":17,"mx":29,"h":[["15h",25,"P"],...]},
//  "fx":{"ok":1,"at":"14:30","l":[["USD","5,83",0.95],...]},
//  "ev":{"st":"ok","msg":"","l":[["14:00","Reuniao",1],...]}}
// So vem os blocos que a tela atual usa. Texto chega sem acento (fonte ASCII).
//
// Primeira vez (ou BOOT segurado ao ligar): a placa abre a rede "CYD-Painel" com
// a senha mostrada na tela. Conecte pelo celular e informe Wi-Fi, servidor e token.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "pixel_draw.h"
#include "scenes.h"

TFT_eSPI tft;
TFT_eSprite spr(&tft);
TFT_eSprite sceneSpr(&tft);  // animacao do relogio: 8 bits, alocado so no modo relogio
Preferences prefs;

// ---------- cores (tokens) ----------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t C_BG = rgb(12, 14, 18);
constexpr uint16_t C_TEXT = rgb(232, 234, 238);
constexpr uint16_t C_MUTED = rgb(130, 138, 152);
constexpr uint16_t C_LINE = rgb(44, 50, 62);
constexpr uint16_t C_TRACK = rgb(34, 38, 48);
constexpr uint16_t C_ACCENT = rgb(255, 140, 40);
constexpr uint16_t C_ACCENT_DIM = rgb(110, 62, 22);
constexpr uint16_t C_GOOD = rgb(80, 200, 120);
constexpr uint16_t C_WARN = rgb(240, 190, 60);
constexpr uint16_t C_BAD = rgb(235, 80, 70);
constexpr uint16_t C_BLUE = rgb(100, 165, 255);
constexpr uint16_t C_SUN = rgb(255, 200, 50);
constexpr uint16_t C_CLOUD = rgb(200, 206, 216);
constexpr uint16_t C_CLOUD_DARK = rgb(130, 138, 152);

// ---------- layout ----------
constexpr int W = 320;
constexpr int H_HEADER = 28;
constexpr int Y_MID = H_HEADER;
constexpr int H_MID = 124;
constexpr int HALF = 160;
constexpr int Y_AGENDA = Y_MID + H_MID;
constexpr int H_AGENDA = 240 - Y_AGENDA;
constexpr unsigned long STALE_MS = 5000;
constexpr unsigned long POLL_MS = 1000;
constexpr int PIN_BOOT = 0;

// ---------- estado ----------
struct Hour { char lbl[6]; int temp; char ic; };
struct Ev { char when[18]; char title[48]; bool now; };
struct Fx { char lbl[5]; char val[14]; float ch; };

struct State {
  char hora[6] = "--:--";
  char data[24] = "";
  char layN[16] = "";
  char layL[5] = "none", layR[5] = "none", layB[5] = "none";
  bool pc = false;
  bool hasSys = false;
  float cpu = 0, ram = 0, ru = 0, rt = 0, dsk = 0, dn = 0, up = 0;
  bool wOk = false;
  char city[24] = "";
  int tc = 0, mn = 0, mx = 0;
  char ic = 'C';
  bool day = true;
  Hour h[4];
  int nh = 0;
  bool fxOk = false;
  char fxAt[6] = "";
  char fxMsg[32] = "";
  Fx fx[4];
  int nfx = 0;
  char evSt[6] = "off";
  char evMsg[40] = "";
  Ev ev[4];
  int nev = 0;
} S;

constexpr int HIST_N = 72;  // 72 colunas x 2 px = 144 px de grafico
uint8_t cpuHist[HIST_N];
int histCount = 0;

int cfgBr = 200, cfgInv = 0, cfgRot = 1;
unsigned long lastMsg = 0, lastPoll = 0;
bool everGot = false;
bool wasStale = true;

// ---------- modos animados ----------
// GRID: 3 regioes (redesenha a cada resposta). CLOCK: relogio de mesa, animacao em tela
// cheia com hora pixelada por cima. PIXEL: faixa de LEDs 32x8 com apps se alternando.
enum Mode { M_GRID, M_CLOCK, M_PIXEL };
Mode mode = M_GRID;
char sceneName[10] = "";
bool showSec = true;
uint16_t clockCol = 0xFE87;
long epBase = 0;            // segundos locais desde 1970, vindos do servidor
unsigned long epAt = 0;     // millis() de quando epBase chegou
uint32_t frameNo = 0;
unsigned long lastFrame = 0;
constexpr unsigned long FRAME_MS = 33;       // ~30 quadros/s
constexpr unsigned long POLL_ANIM_MS = 4000; // nos modos animados o pedido HTTP trava a animacao: pede menos

struct PxApp { char icon[10]; uint16_t col; uint8_t dur; bool clk; char tx[64]; };
constexpr int MAX_APPS = 8;
PxApp pxApps[MAX_APPS];
int nPxApps = 0;
char pxSig[96] = "";         // muda quando a lista de apps muda (reinicia a rotacao)
bool pxSlide = true;
int pxIdx = 0, pxShownIdx = -1;
uint32_t pxT0 = 0, pxTr = 99;

char srvUrl[96] = "http://192.168.31.229:8765";
char srvToken[72] = "";
char linkErr[32] = "";  // motivo de estar sem dados, para o cabecalho

// ---------- utilidades de desenho ----------
uint16_t semaforo(float pct) {
  if (pct < 60) return C_GOOD;
  if (pct < 85) return C_WARN;
  return C_BAD;
}

// Corta o texto com ".." para caber em maxW pixels.
void fitText(const char* src, char* out, size_t outSz, int maxW, uint8_t font) {
  strlcpy(out, src, outSz);
  if (spr.textWidth(out, font) <= maxW) return;
  size_t n = strlen(out);
  while (n > 0) {
    out[--n] = '\0';
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s..", out);
    if (spr.textWidth(tmp, font) <= maxW) {
      strlcpy(out, tmp, outSz);
      return;
    }
  }
}

void degree(int x, int y, int r, uint16_t c) {
  spr.drawCircle(x, y, r, c);
  if (r > 2) spr.drawCircle(x, y, r - 1, c);
}

void fmtRate(float kbps, char* out, size_t sz) {
  if (kbps >= 1024) snprintf(out, sz, "%.1f MB/s", kbps / 1024.0f);
  else snprintf(out, sz, "%.0f KB/s", kbps);
}

// ---------- icones de tempo (primitivas, sem bitmap) ----------
void cloud(int x, int y, float k, uint16_t c) {
  spr.fillCircle(x + 12 * k, y + 17 * k, 7 * k, c);
  spr.fillCircle(x + 20 * k, y + 12 * k, 9 * k, c);
  spr.fillCircle(x + 28 * k, y + 17 * k, 6 * k, c);
  spr.fillRect(x + 12 * k, y + 17 * k, 16 * k, 7 * k, c);
}

void sun(int cx, int cy, float k, int r) {
  for (int a = 0; a < 360; a += 45) {
    float rad = a * DEG_TO_RAD;
    spr.drawLine(cx + cos(rad) * (r + 2 * k), cy + sin(rad) * (r + 2 * k),
                 cx + cos(rad) * (r + 5 * k), cy + sin(rad) * (r + 5 * k), C_SUN);
  }
  spr.fillCircle(cx, cy, r, C_SUN);
}

void moon(int cx, int cy, int r, uint16_t bg) {
  spr.fillCircle(cx, cy, r, C_CLOUD);
  spr.fillCircle(cx + r / 2 + 1, cy - r / 3, r, bg);
}

void weatherIcon(int x, int y, float k, char ic, bool day) {
  switch (ic) {
    case 'S':
      if (day) sun(x + 18 * k, y + 14 * k, k, 8 * k);
      else moon(x + 18 * k, y + 14 * k, 9 * k, C_BG);
      break;
    case 'P':
      if (day) sun(x + 12 * k, y + 9 * k, k, 6 * k);
      else moon(x + 12 * k, y + 9 * k, 6 * k, C_BG);
      cloud(x + 2 * k, y + 3 * k, k, C_CLOUD);
      break;
    case 'F':
      for (int i = 0; i < 3; i++)
        spr.fillRoundRect(x + (4 + i * 3) * k, y + (8 + i * 6) * k, 26 * k, 3 * k + 1, 1, C_CLOUD_DARK);
      break;
    case 'R':
      cloud(x, y - 3 * k, k, C_CLOUD_DARK);
      for (int i = 0; i < 3; i++)
        spr.drawLine(x + (14 + i * 6) * k, y + 22 * k, x + (11 + i * 6) * k, y + 28 * k, C_BLUE);
      break;
    case 'T':
      cloud(x, y - 3 * k, k, C_CLOUD_DARK);
      spr.drawLine(x + 21 * k, y + 20 * k, x + 17 * k, y + 25 * k, C_SUN);
      spr.drawLine(x + 17 * k, y + 25 * k, x + 22 * k, y + 25 * k, C_SUN);
      spr.drawLine(x + 22 * k, y + 25 * k, x + 18 * k, y + 30 * k, C_SUN);
      break;
    case 'N':
      cloud(x, y - 3 * k, k, C_CLOUD);
      for (int i = 0; i < 3; i++) spr.fillCircle(x + (14 + i * 6) * k, y + 25 * k, k + 0.5f, C_TEXT);
      break;
    default:  // 'C' nublado
      cloud(x, y, k, C_CLOUD);
      break;
  }
}

void arrow(int x, int y, bool down, uint16_t c) {
  if (down) spr.fillTriangle(x, y + 3, x + 8, y + 3, x + 4, y + 10, c);
  else spr.fillTriangle(x, y + 10, x + 8, y + 10, x + 4, y + 3, c);
}

// ---------- secoes ----------
bool isStale() { return !everGot || millis() - lastMsg > (mode != M_GRID ? POLL_ANIM_MS * 3 : STALE_MS); }

void drawHeader() {
  spr.createSprite(W, H_HEADER);
  spr.fillSprite(C_BG);
  bool stale = isStale();
  spr.setTextDatum(ML_DATUM);
  if (stale) {
    spr.fillCircle(10, 14, 4, C_BAD);
    spr.setTextColor(C_BAD, C_BG);
    spr.drawString(linkErr[0] ? linkErr : "conectando...", 20, 14, 2);
  } else {
    spr.fillCircle(10, 14, 4, S.pc ? C_GOOD : C_BLUE);
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString(S.data, 20, 14, 2);
    // nome da tela, discreto, depois da data
    if (S.layN[0]) {
      spr.setTextColor(C_MUTED, C_BG);
      spr.drawString(S.layN, 30 + spr.textWidth(S.data, 2), 15, 1);
    }
  }
  spr.setTextDatum(MR_DATUM);
  spr.setTextColor(stale ? C_MUTED : C_TEXT, C_BG);
  spr.drawString(S.hora, W - 8, 15, 4);
  spr.drawFastHLine(0, H_HEADER - 1, W, C_LINE);
  spr.pushSprite(0, 0);
  spr.deleteSprite();
}

void barRow(int y, const char* label, float pct, const char* val) {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_BG);
  spr.drawString(label, 8, y, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString(val, 152, y, 2);
  int bx = 50, bw = 58, by = y + 5;
  spr.fillRoundRect(bx, by, bw, 7, 3, C_TRACK);
  int fw = constrain((int)(bw * pct / 100.0f), 0, bw);
  if (fw > 0) spr.fillRoundRect(bx, by, max(fw, 4), 7, 3, semaforo(pct));
}

// Cada widget desenha num sprite de 160x124 (coordenadas locais).
void wSystem() {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_BG);
  spr.drawString("SISTEMA", 8, 4, 2);

  if (!S.hasSys) {
    spr.drawString("PC desconectado", 8, 30, 2);
    return;
  }
  char buf[24];
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  snprintf(buf, sizeof(buf), "CPU %.0f%%", S.cpu);
  spr.drawString(buf, 152, 4, 2);

  const int gx = 8, gy = 24, gw = 144, gh = 34;
  spr.fillRect(gx, gy, gw, gh, C_TRACK);
  int start = HIST_N - histCount;
  for (int i = 0; i < histCount; i++) {
    int v = cpuHist[i];
    int hpx = max(1, v * gh / 100);
    int x = gx + (start + i) * 2;
    spr.fillRect(x, gy + gh - hpx, 2, hpx, C_ACCENT_DIM);
    spr.drawFastHLine(x, gy + gh - hpx, 2, C_ACCENT);
  }

  snprintf(buf, sizeof(buf), "%.0f%%", S.ram);
  barRow(64, "RAM", S.ram, buf);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_BG);
  snprintf(buf, sizeof(buf), "%.1f / %.0f GB", S.ru, S.rt);
  spr.drawString(buf, 50, 80, 1);

  snprintf(buf, sizeof(buf), "%.0f%%", S.dsk);
  barRow(88, "DISCO", S.dsk, buf);

  char r[16];
  arrow(8, 104, true, C_GOOD);
  fmtRate(S.dn, r, sizeof(r));
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString(r, 19, 105, 1);
  arrow(84, 104, false, C_BLUE);
  fmtRate(S.up, r, sizeof(r));
  spr.drawString(r, 95, 105, 1);
}

void wWeather() {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_BG);
  char city[28];
  fitText(S.city[0] ? S.city : "TEMPO", city, sizeof(city), 144, 2);
  spr.drawString(city, 10, 4, 2);

  if (!S.wOk) {
    spr.drawString("sem previsao", 10, 30, 2);
    return;
  }
  weatherIcon(10, 26, 1.1f, S.ic, S.day);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", S.tc);
  spr.setTextColor(C_TEXT, C_BG);
  spr.drawString(buf, 62, 24, 4);
  int tw = spr.textWidth(buf, 4);
  degree(62 + tw + 4, 28, 3, C_TEXT);
  spr.drawString("C", 62 + tw + 10, 24, 4);

  spr.setTextColor(C_MUTED, C_BG);
  snprintf(buf, sizeof(buf), "%d / %d", S.mn, S.mx);
  spr.drawString(buf, 62, 52, 2);
  degree(62 + spr.textWidth(buf, 2) + 4, 56, 2, C_MUTED);

  for (int i = 0; i < S.nh; i++) {
    int cx = 10 + i * 37 + 17;
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(C_MUTED, C_BG);
    spr.drawString(S.h[i].lbl, cx, 74, 2);
    weatherIcon(cx - 10, 92, 0.55f, S.h[i].ic, true);
    spr.setTextColor(C_TEXT, C_BG);
    snprintf(buf, sizeof(buf), "%d", S.h[i].temp);
    spr.drawString(buf, cx - 2, 108, 2);
    degree(cx - 2 + spr.textWidth(buf, 2) / 2 + 3, 111, 1, C_TEXT);
  }
}

void wQuotes() {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_BG);
  spr.drawString("COTACOES", 8, 4, 2);
  if (S.fxAt[0]) {
    spr.setTextDatum(TR_DATUM);
    spr.drawString(S.fxAt, 152, 6, 1);
  }
  if (!S.fxOk) {
    spr.setTextDatum(TL_DATUM);
    spr.drawString(S.fxMsg[0] ? S.fxMsg : "sem cotacoes", 8, 30, 2);
    return;
  }
  // Ate 4 linhas. Esquerda: rotulo e, embaixo, a variacao do dia. Direita: valor em R$.
  int rowH = S.nfx <= 3 ? 31 : 24;
  for (int i = 0; i < S.nfx; i++) {
    const Fx& q = S.fx[i];
    int y = 24 + i * rowH;
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_MUTED, C_BG);
    spr.drawString(q.lbl, 8, y, 2);
    char ch[12];
    snprintf(ch, sizeof(ch), "%+.1f%%", q.ch);
    uint16_t c = q.ch >= 0 ? C_GOOD : C_BAD;
    spr.setTextColor(c, C_BG);
    spr.drawString(ch, 8, y + 15, 1);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(C_TEXT, C_BG);
    char v[16];
    fitText(q.val, v, sizeof(v), 100, 4);
    spr.drawString(v, 152, y, 4);
  }
}

void drawHalf(int x, const char* kind) {
  spr.createSprite(HALF, H_MID);
  spr.fillSprite(C_BG);
  if (!strcmp(kind, "sys")) wSystem();
  else if (!strcmp(kind, "w")) wWeather();
  else if (!strcmp(kind, "fx")) wQuotes();
  if (x > 0) spr.drawFastVLine(0, 6, H_MID - 12, C_LINE);
  spr.pushSprite(x, Y_MID);
  spr.deleteSprite();
}

void drawAgenda() {
  spr.createSprite(W, H_AGENDA);
  spr.fillSprite(C_BG);
  spr.drawFastHLine(0, 0, W, C_LINE);
  if (!strcmp(S.layB, "ev")) {
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_MUTED, C_BG);
    spr.drawString("AGENDA", 8, 4, 2);

    if (strcmp(S.evSt, "ok") != 0 || S.nev == 0) {
      const char* msg = S.evMsg[0] ? S.evMsg
                      : (strcmp(S.evSt, "ok") == 0 ? "nada nos proximos dias" : "Google nao conectado");
      spr.drawString(msg, 8, 24, 2);
    } else {
      for (int i = 0; i < S.nev; i++) {
        int y = 22 + i * 16;
        const Ev& e = S.ev[i];
        if (e.now) spr.fillRect(0, y + 1, 3, 13, C_ACCENT);
        spr.setTextColor(e.now ? C_ACCENT : C_BLUE, C_BG);
        spr.drawString(e.when, 8, y, 2);
        char t[52];
        fitText(e.title, t, sizeof(t), W - 106, 2);
        spr.setTextColor(C_TEXT, C_BG);
        spr.drawString(t, 98, y, 2);
      }
    }
  }
  spr.pushSprite(0, Y_AGENDA);
  spr.deleteSprite();
}

// ---------- relogio de mesa ----------
long nowLocal() { return epBase + (long)((millis() - epAt) / 1000); }

// Copia em maiusculas (a fonte pixelada so tem maiusculas).
void upcase(const char* src, char* out, size_t sz) {
  size_t i = 0;
  for (; src[i] && i < sz - 1; i++) out[i] = toupper((unsigned char)src[i]);
  out[i] = '\0';
}

const char* weatherIconName(char ic, bool day) {
  switch (ic) {
    case 'S': return day ? "sun" : "moon";
    case 'P': return "partly";
    case 'F': return "fog";
    case 'R': return "rain";
    case 'T': return "storm";
    case 'N': return "snow";
    default: return "cloud";
  }
}

// Relogio de mesa: quadro da animacao + informacoes pixeladas por cima, no mesmo sprite
// (tela cheia, 8 bits), enviado de uma vez para nao piscar.
void clockFrame() {
  TFT_eSprite& s = sceneSpr;
  sceneFrame(s, frameNo);
  bool stale = isStale();

  long e = nowLocal();
  char hm[6], sc[3];
  snprintf(hm, sizeof(hm), "%02ld:%02ld", (e / 3600) % 24, (e / 60) % 60);
  snprintf(sc, sizeof(sc), "%02ld", e % 60);
  const int K = 12, KS = 5;
  int wHM = pxWidth(hm) * K, wS = showSec ? pxWidth(sc) * KS + 8 : 0;
  int x = (W - wHM - wS) / 2;
  pxBig(s, hm, x, 14, K, stale ? C_MUTED : clockCol);
  if (showSec) pxBig(s, sc, x + wHM + 8, 14, KS, C_ACCENT);

  char buf[64];
  upcase(S.data, buf, sizeof(buf));
  pxBig(s, buf, 12, 88, 3, rgb(200, 204, 212));
  if (S.wOk) {
    char t[10];
    snprintf(t, sizeof(t), "%d~C", S.tc);
    int tw = pxWidth(t) * 3;
    pxBig(s, t, 308 - tw, 88, 3, C_TEXT);
    pxIconBig(s, weatherIconName(S.ic, S.day), 308 - tw - 22, 86, 2);
  }

  // proximo compromisso (rola se nao couber) ou o motivo de estar sem dados
  uint16_t col = C_BLUE;
  buf[0] = '\0';
  if (stale && linkErr[0]) {
    upcase(linkErr, buf, sizeof(buf));
    col = C_BAD;
  } else if (S.nev > 0) {
    char raw[72];
    snprintf(raw, sizeof(raw), "%s %s", S.ev[0].when, S.ev[0].title);
    upcase(raw, buf, sizeof(buf));
    if (S.ev[0].now) col = C_ACCENT;
  }
  if (buf[0]) {
    int lw = pxWidth(buf) * 3, avail = 296;
    int off = lw > avail ? avail - (int)((frameNo >> 1) % (lw + avail)) : 0;
    pxBig(s, buf, 12 + off, 110, 3, col, 12, 308);
  }
  s.pushSprite(0, 0);
}

// ---------- relogio pixel (faixa 32x8) ----------
PxBuf pxCur, pxPrev, pxShow;

// Monta o quadro do app; devolve quantos quadros ele fica na tela.
uint32_t pixelBuild(PxBuf b, const PxApp& a, uint32_t f) {
  char t[64];
  long e = nowLocal();
  if (a.clk) snprintf(t, sizeof(t), "%02ld:%02ld", (e / 3600) % 24, (e / 60) % 60);
  else strlcpy(t, a.tx, sizeof(t));
  bool icon = strcmp(a.icon, "none") != 0 && pxHasIcon(a.icon);
  int x0 = icon ? 9 : 0, avail = 32 - x0, tw = pxWidth(t);
  int off = tw <= avail ? x0 + (avail - tw) / 2 : x0 + avail - (int)((f >> 1) % (tw + avail));
  pxBufClear(b);
  // ":" da hora pisca (fica cinza nos segundos impares)
  pxBufText(b, t, off, 1, a.col, x0, 32, a.clk && (e % 2) ? 2 : -1, rgb(85, 85, 85));
  if (icon) pxBufIcon(b, a.icon, 0);
  uint32_t frames = a.dur * (1000 / FRAME_MS);
  if (tw > avail) frames = max(frames, (uint32_t)(tw + avail) * 2);  // rola o texto inteiro pelo menos uma vez
  return frames;
}

void pixelFrame() {
  TFT_eSprite& s = sceneSpr;
  if (nPxApps == 0) {
    pxBufClear(pxShow);
    pxBufText(pxShow, "SEM APPS", 1, 1, C_MUTED, 0, 32);
  } else {
    if (pxIdx >= nPxApps) pxIdx = 0;
    uint32_t frames = pixelBuild(pxCur, pxApps[pxIdx], frameNo - pxT0);
    if (frameNo - pxT0 >= frames && nPxApps > 1) {
      memcpy(pxPrev, pxCur, sizeof(PxBuf));
      pxIdx = (pxIdx + 1) % nPxApps;
      pxT0 = frameNo;
      pxTr = 0;
      pixelBuild(pxCur, pxApps[pxIdx], 0);
    }
    if (pxSlide && pxTr < 16) {  // desliza para cima em 8 passos
      int k = (pxTr >> 1) + 1;
      for (int y = 0; y < 8; y++) memcpy(pxShow[y], y < 8 - k ? pxPrev[y + k] : pxCur[y - (8 - k)], sizeof(pxShow[y]));
      pxTr++;
    } else {
      memcpy(pxShow, pxCur, sizeof(PxBuf));
    }
  }
  s.fillSprite(TFT_BLACK);
  const uint16_t off = rgb(22, 22, 26);
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 32; x++) s.fillRoundRect(x * 10 + 1, y * 10 + 1, 8, 8, 2, pxShow[y][x] ? pxShow[y][x] : off);
  s.pushSprite(0, 80);

  // indicador de qual app esta na tela (direto na tela, so quando muda)
  if (pxShownIdx != pxIdx) {
    pxShownIdx = pxIdx;
    tft.fillRect(0, 170, W, 8, TFT_BLACK);
    int n = nPxApps, x0 = 160 - (n * 10 - 4) / 2;
    for (int i = 0; i < n; i++) tft.fillRect(x0 + i * 10, 172, 6, 3, i == pxIdx ? TFT_WHITE : rgb(68, 68, 68));
  }
  // sem dados: aviso discreto em cima da faixa
  static bool shownStale = false;
  bool stale = isStale();
  if (stale != shownStale) {
    shownStale = stale;
    tft.fillRect(0, 50, W, 20, TFT_BLACK);
    if (stale && linkErr[0]) {
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(C_BAD, TFT_BLACK);
      tft.drawString(linkErr, W / 2, 52, 2);
    }
  }
}

void sceneStop() {
  if (sceneSpr.created()) sceneSpr.deleteSprite();
}

// Aloca o sprite do modo animado: tela cheia no relogio, faixa 320x80 no pixel.
void sceneStart() {
  sceneStop();
  sceneSpr.setColorDepth(8);
  int h = mode == M_PIXEL ? 80 : SCENE_H;
  if (!sceneSpr.createSprite(SCENE_W, h)) {
    Serial.printf("sem memoria para a animacao (heap %u)\n", ESP.getFreeHeap());
    return;
  }
  if (mode == M_CLOCK) sceneBegin(sceneName);
  frameNo = 0;
  pxShownIdx = -1;
}

void drawAll() {
  if (mode != M_GRID) return;  // modos animados desenham no loop
  drawHeader();
  drawHalf(0, S.layL);
  drawHalf(HALF, S.layR);
  drawAgenda();
}

// Tela cheia de aviso (configuracao do Wi-Fi, erros de boot).
void drawNotice(const char* title, const char* l1, const char* l2 = "", const char* l3 = "", const char* l4 = "") {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.drawString(title, 12, 14, 4);
  tft.drawFastHLine(12, 44, W - 24, C_LINE);
  const char* ls[] = {l1, l2, l3, l4};
  for (int i = 0; i < 4; i++) {
    tft.setTextColor(i == 0 ? C_TEXT : C_MUTED, C_BG);
    tft.drawString(ls[i], 12, 58 + i * 24, 2);
  }
}

// ---------- configuracao vinda do servidor ----------
void applyConfig(int br, int inv, int rot) {
  bool redraw = false;
  if (rot != cfgRot) {
    cfgRot = rot;
    tft.setRotation(cfgRot);
    redraw = true;
  }
  if (inv != cfgInv) {
    cfgInv = inv;
    tft.invertDisplay(cfgInv);
  }
  if (br != cfgBr) {
    cfgBr = br;
    analogWrite(TFT_BL, cfgBr);
  }
  if (redraw) tft.fillScreen(C_BG);
}

// ---------- parse ----------
bool handlePayload(const String& body) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("JSON: %s\n", err.c_str());
    strlcpy(linkErr, "resposta invalida", sizeof(linkErr));
    return false;
  }

  JsonObject cfg = doc["cfg"];
  if (!cfg.isNull()) applyConfig(cfg["br"] | cfgBr, cfg["inv"] | cfgInv, cfg["rot"] | cfgRot);

  strlcpy(S.hora, doc["t"] | "--:--", sizeof(S.hora));
  strlcpy(S.data, doc["d"] | "", sizeof(S.data));
  S.pc = (doc["pc"] | 0) == 1;

  long ep = doc["ep"] | 0L;
  if (ep > 0) {
    epBase = ep;
    epAt = millis();
  }

  JsonObject lay = doc["lay"];
  const char* f = lay["f"] | "";
  Mode want = !strcmp(f, "clock") ? M_CLOCK : !strcmp(f, "pixel") ? M_PIXEL : M_GRID;
  const char* scn = lay["sc"] | "none";
  showSec = (lay["sec"] | 1) == 1;
  clockCol = lay["col"] | 0xFE87;
  if (want != mode || (want == M_CLOCK && strcmp(scn, sceneName) != 0)) {
    // troca de tipo de tela (ou de animacao): limpa e (re)aloca o sprite
    mode = want;
    strlcpy(sceneName, scn, sizeof(sceneName));
    tft.fillScreen(mode == M_GRID ? C_BG : TFT_BLACK);
    if (mode == M_GRID) sceneStop();
    else sceneStart();
    pxSig[0] = '\0';
  }
  if (mode == M_PIXEL) {
    pxSlide = strcmp(lay["tr"] | "slide", "none") != 0;
    // assinatura = icones + tipo; se mudou, recomeca do primeiro app
    char sig[sizeof(pxSig)] = "";
    int n = 0;
    for (JsonArray it : lay["a"].as<JsonArray>()) {
      if (n >= MAX_APPS) break;
      PxApp& a = pxApps[n++];
      strlcpy(a.icon, it[0] | "none", sizeof(a.icon));
      a.col = it[1] | 0xFFFF;
      a.dur = constrain((int)(it[2] | 6), 2, 60);
      a.clk = (it[3] | 0) == 1;
      strlcpy(a.tx, it[4] | "", sizeof(a.tx));
      size_t l = strlen(sig);
      snprintf(sig + l, sizeof(sig) - l, "%s%d,", a.icon, a.clk);
    }
    if (n != nPxApps || strcmp(sig, pxSig) != 0) {
      nPxApps = n;
      strlcpy(pxSig, sig, sizeof(pxSig));
      pxIdx = 0;
      pxT0 = frameNo;
      pxTr = 99;
      pxShownIdx = -1;
    }
  }
  strlcpy(S.layN, lay["n"] | "", sizeof(S.layN));
  strlcpy(S.layL, lay["l"] | "sys", sizeof(S.layL));
  strlcpy(S.layR, lay["r"] | "w", sizeof(S.layR));
  strlcpy(S.layB, lay["b"] | "ev", sizeof(S.layB));

  JsonObject sys = doc["sys"];
  S.hasSys = !sys.isNull();
  if (S.hasSys) {
    S.cpu = sys["cpu"] | 0.0f;
    S.ram = sys["ram"] | 0.0f;
    S.ru = sys["ru"] | 0.0f;
    S.rt = sys["rt"] | 0.0f;
    S.dsk = sys["dsk"] | 0.0f;
    S.dn = sys["dn"] | 0.0f;
    S.up = sys["up"] | 0.0f;
    if (histCount == HIST_N) memmove(cpuHist, cpuHist + 1, HIST_N - 1);
    else histCount++;
    cpuHist[histCount - 1] = (uint8_t)constrain((int)S.cpu, 0, 100);
  } else {
    histCount = 0;  // PC saiu: o grafico recomeca quando voltar
  }

  JsonObject w = doc["w"];
  if (!w.isNull()) {
    S.wOk = (w["ok"] | 0) == 1;
    strlcpy(S.city, w["city"] | "", sizeof(S.city));
    S.tc = w["tc"] | 0;
    S.mn = w["mn"] | 0;
    S.mx = w["mx"] | 0;
    S.ic = (w["ic"] | "C")[0];
    S.day = (w["day"] | 1) == 1;
    S.nh = 0;
    for (JsonArray it : w["h"].as<JsonArray>()) {
      if (S.nh >= 4) break;
      Hour& h = S.h[S.nh++];
      strlcpy(h.lbl, it[0] | "", sizeof(h.lbl));
      h.temp = it[1] | 0;
      h.ic = (it[2] | "C")[0];
    }
  }

  JsonObject fx = doc["fx"];
  if (!fx.isNull()) {
    S.fxOk = (fx["ok"] | 0) == 1;
    strlcpy(S.fxAt, fx["at"] | "", sizeof(S.fxAt));
    strlcpy(S.fxMsg, fx["msg"] | "", sizeof(S.fxMsg));
    S.nfx = 0;
    for (JsonArray it : fx["l"].as<JsonArray>()) {
      if (S.nfx >= 4) break;
      Fx& q = S.fx[S.nfx++];
      strlcpy(q.lbl, it[0] | "", sizeof(q.lbl));
      strlcpy(q.val, it[1] | "", sizeof(q.val));
      q.ch = it[2] | 0.0f;
    }
  }

  JsonObject ev = doc["ev"];
  if (!ev.isNull()) {
    strlcpy(S.evSt, ev["st"] | "off", sizeof(S.evSt));
    strlcpy(S.evMsg, ev["msg"] | "", sizeof(S.evMsg));
    S.nev = 0;
    for (JsonArray it : ev["l"].as<JsonArray>()) {
      if (S.nev >= 4) break;
      Ev& e = S.ev[S.nev++];
      strlcpy(e.when, it[0] | "", sizeof(e.when));
      strlcpy(e.title, it[1] | "", sizeof(e.title));
      e.now = (it[2] | 0) == 1;
    }
  }

  lastMsg = millis();
  everGot = true;
  linkErr[0] = '\0';
  drawAll();
  return true;
}

void poll() {
  if (WiFi.status() != WL_CONNECTED) {
    strlcpy(linkErr, "sem Wi-Fi", sizeof(linkErr));
    return;
  }
  HTTPClient http;
  String url = String(srvUrl) + "/api/screen";
  if (!http.begin(url)) {
    strlcpy(linkErr, "endereco invalido", sizeof(linkErr));
    return;
  }
  http.setTimeout(2500);
  http.setConnectTimeout(2000);
  http.addHeader("Authorization", String("Bearer ") + srvToken);
  int code = http.GET();
  if (code == 200) {
    handlePayload(http.getString());
  } else if (code == 401) {
    strlcpy(linkErr, "token recusado", sizeof(linkErr));
  } else if (code < 0) {
    strlcpy(linkErr, "servidor fora do ar", sizeof(linkErr));
  } else {
    snprintf(linkErr, sizeof(linkErr), "servidor: HTTP %d", code);
  }
  http.end();
}

// ---------- atualizacao pelo Wi-Fi (OTA) ----------
// pio run -e cyd-ota -t upload, com CYD_IP e PAINEL_TOKEN no ambiente.
// A senha do OTA e o proprio PAINEL_TOKEN gravado na placa.
void setupOta() {
  if (!srvToken[0]) return;  // sem token, sem OTA (nao abre atualizacao sem senha)
  ArduinoOTA.setHostname("cyd-painel");
  ArduinoOTA.setPassword(srvToken);
  ArduinoOTA.onStart([]() {
    sceneStop();  // libera memoria para receber o firmware
    mode = M_GRID;
    drawNotice("Atualizando", "Recebendo firmware pelo Wi-Fi...", "nao desligue a placa");
    tft.drawRect(12, 160, W - 24, 16, C_LINE);
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    int w = (W - 28) * done / total;
    tft.fillRect(14, 162, w, 12, C_ACCENT);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    char m[32];
    snprintf(m, sizeof(m), "erro OTA %u, reiniciando", e);
    drawNotice("Falhou", m);
    delay(2000);
    ESP.restart();
  });
  ArduinoOTA.begin();
}

// ---------- Wi-Fi e configuracao ----------
char apPass[10];
bool saveParams = false;

void onApStart(WiFiManager*) {
  char l2[48];
  snprintf(l2, sizeof(l2), "rede: CYD-Painel   senha: %s", apPass);
  drawNotice("Configurar", "Conecte o celular na rede abaixo", l2,
             "abra 192.168.4.1 e informe o Wi-Fi,", "o servidor e o PAINEL_TOKEN");
}

void setupWifi() {
  prefs.begin("painel", false);
  // BOOT segurado ao ligar: apaga Wi-Fi e configuracao e abre o portal.
  pinMode(PIN_BOOT, INPUT_PULLUP);
  bool reset = digitalRead(PIN_BOOT) == LOW;

  String u = prefs.getString("url", srvUrl);
  String t = prefs.getString("token", "");
  strlcpy(srvUrl, u.c_str(), sizeof(srvUrl));
  strlcpy(srvToken, t.c_str(), sizeof(srvToken));

  WiFiManager wm;
  if (reset) {
    drawNotice("Reset", "Apagando Wi-Fi e configuracao...");
    wm.resetSettings();
    prefs.clear();
    srvToken[0] = '\0';
    delay(800);
  }

  // Senha do portal sorteada a cada boot e mostrada so na tela.
  snprintf(apPass, sizeof(apPass), "%08lu", (unsigned long)(esp_random() % 100000000UL));

  WiFiManagerParameter pUrl("url", "Servidor (http://IP:porta)", srvUrl, sizeof(srvUrl) - 1);
  WiFiManagerParameter pTok("token", "PAINEL_TOKEN", srvToken, sizeof(srvToken) - 1);
  wm.addParameter(&pUrl);
  wm.addParameter(&pTok);
  wm.setTitle("Painel CYD");
  wm.setAPCallback(onApStart);
  wm.setSaveParamsCallback([]() { saveParams = true; });
  wm.setSaveConfigCallback([]() { saveParams = true; });
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(300);  // 5 min sem configurar: reinicia e tenta de novo

  drawNotice("Conectando", "Entrando no Wi-Fi...", "segure BOOT ao ligar para reconfigurar");

  bool ok;
  if (reset || !srvToken[0]) ok = wm.startConfigPortal("CYD-Painel", apPass);
  else ok = wm.autoConnect("CYD-Painel", apPass);

  if (saveParams) {
    strlcpy(srvUrl, pUrl.getValue(), sizeof(srvUrl));
    strlcpy(srvToken, pTok.getValue(), sizeof(srvToken));
    // tira barra final e espacos
    size_t n = strlen(srvUrl);
    while (n && (srvUrl[n - 1] == '/' || srvUrl[n - 1] == ' ')) srvUrl[--n] = '\0';
    prefs.putString("url", srvUrl);
    prefs.putString("token", srvToken);
  }
  if (!ok) {
    drawNotice("Sem Wi-Fi", "Nao conectou. Reiniciando...");
    delay(2000);
    ESP.restart();
  }
  WiFi.setAutoReconnect(true);
  Serial.printf("Wi-Fi ok, IP %s, servidor %s, heap livre %u\n", WiFi.localIP().toString().c_str(), srvUrl, ESP.getFreeHeap());
  tft.fillScreen(C_BG);
  setupOta();
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(cfgRot);
  tft.fillScreen(C_BG);
  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, cfgBr);
  setupWifi();
  drawAll();
}

void loop() {
  ArduinoOTA.handle();
  if (millis() - lastPoll >= (mode != M_GRID ? POLL_ANIM_MS : POLL_MS)) {
    lastPoll = millis();
    poll();
  }
  if (mode != M_GRID) {
    // relogio e pixel andam sozinhos entre um pedido e outro
    if (millis() - lastFrame >= FRAME_MS && sceneSpr.created()) {
      lastFrame = millis();
      if (mode == M_CLOCK) clockFrame();
      else pixelFrame();
      frameNo++;
    }
    delay(1);
    return;
  }
  // Sem resposta nova, so o cabecalho muda (indicador e motivo da falha).
  static char shownErr[32] = "";
  bool stale = isStale();
  if (stale != wasStale || (stale && strcmp(shownErr, linkErr) != 0)) {
    wasStale = stale;
    strlcpy(shownErr, linkErr, sizeof(shownErr));
    drawHeader();
  }
  delay(10);
}
