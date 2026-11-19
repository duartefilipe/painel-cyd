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
#include <XPT2046_Touchscreen.h>

#include "pixel_draw.h"
#include "scenes.h"

TFT_eSPI tft;
TFT_eSprite spr(&tft);
TFT_eSprite sceneSpr(&tft);  // animacao do relogio: 8 bits, alocado so no modo relogio
Preferences prefs;

// Toque da CYD: XPT2046 num SPI separado do da tela (a tela usa o HSPI).
constexpr int T_CLK = 25, T_MISO = 39, T_MOSI = 32, T_CS = 33, T_IRQ = 36;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(T_CS, T_IRQ);
// Sensor de luz (LDR) da CYD.
constexpr int PIN_LDR = 34;

// ---------- cores (tokens) ----------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t C_BG = rgb(10, 12, 17);
constexpr uint16_t C_CARD = rgb(24, 28, 37);    // cartoes sobre o fundo (visual integrado, sem divisorias)
constexpr uint16_t C_TEXT = rgb(232, 234, 238);
constexpr uint16_t C_MUTED = rgb(130, 138, 152);
constexpr uint16_t C_LINE = rgb(44, 50, 62);
constexpr uint16_t C_TRACK = rgb(40, 46, 60);
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
struct Hour { char lbl[6]; int temp; char ic; int rain; };
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
  int hu = 0, rp = 0, rx = 0;          // umidade, chuva agora, chuva maxima do dia
  char sr[6] = "", ss[6] = "";         // nascer e por do sol
  int tmMn = 0, tmMx = 0, tmRain = 0;  // amanha
  char tmIc = 'C';
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
// STATION: estacao do tempo (ilustracao animada, hora 7 segmentos, umidade/temperatura grandes).
enum Mode { M_GRID, M_CLOCK, M_PIXEL, M_STATION };
uint16_t stCol = 0xFFFF, stCol2 = 0x5E9F;  // hora; umidade e temperatura
int stFont = 2, stWday = 0;
float stTd = 0;                            // temperatura com uma casa decimal
Mode mode = M_GRID;
char sceneName[10] = "";
bool showSec = true;
uint16_t clockCol = 0xFE87;
int clockFont = 0, clockSize = 12, clockX = -1, clockY = 14;
int dateSize = 3, dateX = 12, dateY = 88;
int weaSize = 3, weaX = 238, weaY = 88;
int evSize = 3, evX = 12, evY = 110;
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

// ---------- aviso (POST /api/notify) por cima de qualquer tela ----------
bool ntOn = false;
long ntId = 0;
char ntTx[84] = "", ntIc[10] = "bell";
uint16_t ntCol = 0xFFFF;
uint32_t ntT0 = 0;  // quadro em que o aviso apareceu (rolagem no relogio/pixel)

// ---------- brilho: manual, automatico pelo LDR, ou apagado pelo horario ----------
bool autoBr = false, screenOff = false;
int ldrDark = 3000, ldrBright = 300, brMin = 15, brMax = 255;
float ldrAvg = -1;     // leitura suavizada do sensor
int brNow = -1;        // brilho aplicado agora (vai ate o alvo aos poucos)
unsigned long lastLight = 0;

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
      else moon(x + 18 * k, y + 14 * k, 9 * k, C_CARD);
      break;
    case 'P':
      if (day) sun(x + 12 * k, y + 9 * k, k, 6 * k);
      else moon(x + 12 * k, y + 9 * k, 6 * k, C_CARD);
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

const char* weatherIconName(char ic, bool day);  // definida no bloco do relogio

// Visual integrado: um fundo unico (C_BG) e cada informacao num cartao arredondado
// (C_CARD). Nao ha linhas dividindo regioes; o espaco entre cartoes faz a separacao.
void card(int x, int y, int w, int h) { spr.fillRoundRect(x, y, w, h, 9, C_CARD); }

void drawHeader() {
  spr.createSprite(W, H_HEADER);
  spr.fillSprite(C_BG);
  bool stale = isStale();
  spr.setTextDatum(ML_DATUM);
  if (stale) {
    spr.fillCircle(12, 14, 4, C_BAD);
    spr.setTextColor(C_BAD, C_BG);
    spr.drawString(linkErr[0] ? linkErr : "conectando...", 22, 14, 2);
  } else {
    spr.fillCircle(12, 14, 4, S.pc ? C_GOOD : C_BLUE);
    spr.setTextColor(C_TEXT, C_BG);
    spr.drawString(S.data, 22, 14, 2);
    if (S.layN[0]) {
      spr.setTextColor(C_MUTED, C_BG);
      spr.drawString(S.layN, 32 + spr.textWidth(S.data, 2), 15, 1);
    }
  }
  spr.setTextDatum(MR_DATUM);
  spr.setTextColor(stale ? C_MUTED : C_TEXT, C_BG);
  spr.drawString(S.hora, W - 10, 15, 4);
  spr.pushSprite(0, 0);
  spr.deleteSprite();
}

void barRow(int y, const char* label, float pct, const char* val) {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_CARD);
  spr.drawString(label, 12, y, 2);
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_TEXT, C_CARD);
  spr.drawString(val, 148, y, 2);
  int bx = 54, bw = 54, by = y + 5;
  spr.fillRoundRect(bx, by, bw, 7, 3, C_TRACK);
  int fw = constrain((int)(bw * pct / 100.0f), 0, bw);
  if (fw > 0) spr.fillRoundRect(bx, by, max(fw, 4), 7, 3, semaforo(pct));
}

// Cada widget desenha dentro de um cartao de ~150x120 num sprite de 160x124.
void wSystem() {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_CARD);
  spr.drawString("SISTEMA", 12, 6, 2);
  if (!S.hasSys) {
    spr.drawString("PC desconectado", 12, 32, 2);
    return;
  }
  char buf[24];
  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(C_TEXT, C_CARD);
  snprintf(buf, sizeof(buf), "CPU %.0f%%", S.cpu);
  spr.drawString(buf, 148, 6, 2);

  const int gx = 12, gy = 25, gw = 136, gh = 32;
  spr.fillRoundRect(gx, gy, gw, gh, 4, C_TRACK);
  int n = min(histCount, gw / 2), start = histCount - n;
  for (int i = 0; i < n; i++) {
    int v = cpuHist[start + i];
    int hpx = max(1, v * gh / 100);
    int x = gx + gw - (n - i) * 2;
    spr.fillRect(x, gy + gh - hpx, 2, hpx, C_ACCENT_DIM);
    spr.drawFastHLine(x, gy + gh - hpx, 2, C_ACCENT);
  }

  snprintf(buf, sizeof(buf), "%.0f%%", S.ram);
  barRow(62, "RAM", S.ram, buf);
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_CARD);
  snprintf(buf, sizeof(buf), "%.1f / %.0f GB", S.ru, S.rt);
  spr.drawString(buf, 54, 78, 1);
  snprintf(buf, sizeof(buf), "%.0f%%", S.dsk);
  barRow(86, "DISCO", S.dsk, buf);

  char r[16];
  arrow(12, 103, true, C_GOOD);
  fmtRate(S.dn, r, sizeof(r));
  spr.setTextColor(C_TEXT, C_CARD);
  spr.drawString(r, 23, 104, 1);
  arrow(84, 103, false, C_BLUE);
  fmtRate(S.up, r, sizeof(r));
  spr.drawString(r, 95, 104, 1);
}

void wWeather() {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_CARD);
  char city[28];
  fitText(S.city[0] ? S.city : "TEMPO", city, sizeof(city), 96, 2);
  spr.drawString(city, 12, 6, 2);
  if (!S.wOk) {
    spr.drawString("sem previsao", 12, 32, 2);
    return;
  }
  // umidade no canto
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", S.hu);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(buf, 148, 6, 2);
  pxIconBig(spr, "drop", 148 - spr.textWidth(buf, 2) - 11, 9, 1);

  weatherIcon(10, 26, 1.05f, S.ic, S.day);
  spr.setTextDatum(TL_DATUM);
  snprintf(buf, sizeof(buf), "%d", S.tc);
  spr.setTextColor(C_TEXT, C_CARD);
  spr.drawString(buf, 60, 25, 4);
  int tw = spr.textWidth(buf, 4);
  degree(60 + tw + 4, 29, 3, C_TEXT);
  spr.drawString("C", 60 + tw + 10, 25, 4);
  // minima/maxima e chance de chuva do dia
  spr.setTextColor(C_MUTED, C_CARD);
  snprintf(buf, sizeof(buf), "%d/%d", S.mn, S.mx);
  spr.drawString(buf, 60, 51, 2);
  int bx = 60 + spr.textWidth(buf, 2) + 2;
  degree(bx + 2, 55, 2, C_MUTED);
  snprintf(buf, sizeof(buf), "%d%%", S.rx);
  spr.setTextColor(C_BLUE, C_CARD);
  spr.drawString(buf, bx + 18, 51, 2);

  // proximas horas: rotulo, icone, temperatura e chance de chuva
  for (int i = 0; i < S.nh; i++) {
    int cx = 12 + i * 35 + 15;
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(C_MUTED, C_CARD);
    spr.drawString(S.h[i].lbl, cx, 70, 1);
    weatherIcon(cx - 10, 79, 0.5f, S.h[i].ic, true);
    spr.setTextColor(C_TEXT, C_CARD);
    snprintf(buf, sizeof(buf), "%d", S.h[i].temp);
    spr.drawString(buf, cx - 2, 95, 2);
    degree(cx - 2 + spr.textWidth(buf, 2) / 2 + 3, 98, 1, C_TEXT);
    if (S.h[i].rain >= 10) {
      snprintf(buf, sizeof(buf), "%d%%", S.h[i].rain);
      spr.setTextColor(C_BLUE, C_CARD);
      spr.drawString(buf, cx, 111, 1);
    }
  }
}

// Tempo detalhado: umidade, chuva, nascer/por do sol e amanha.
void wWeatherDetail() {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_CARD);
  spr.drawString("DETALHES", 12, 6, 2);
  if (!S.wOk) {
    spr.drawString("sem previsao", 12, 32, 2);
    return;
  }
  char v[24];
  struct Row { const char* icon; const char* label; };
  const Row rows[4] = {{"drop", "umidade"}, {"rain", "chuva"}, {"sunrise", "sol"},
                       {weatherIconName(S.tmIc, true), "amanha"}};
  for (int i = 0; i < 4; i++) {
    int y = 27 + i * 23;
    pxIconBig(spr, rows[i].icon, 12, y, 2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_MUTED, C_CARD);
    spr.drawString(rows[i].label, 34, y + 1, 2);
    if (i == 0) snprintf(v, sizeof(v), "%d%%", S.hu);
    else if (i == 1) snprintf(v, sizeof(v), "%d%% / %d%%", S.rp, S.rx);
    else if (i == 2) snprintf(v, sizeof(v), "%s %s", S.sr, S.ss);
    else snprintf(v, sizeof(v), "%d/%d %d%%", S.tmMn, S.tmMx, S.tmRain);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(C_TEXT, C_CARD);
    spr.drawString(v, 148, y + 1, 2);
  }
}

void wQuotes() {
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(C_MUTED, C_CARD);
  spr.drawString("COTACOES", 12, 6, 2);
  if (S.fxAt[0]) {
    spr.setTextDatum(TR_DATUM);
    spr.drawString(S.fxAt, 148, 8, 1);
  }
  if (!S.fxOk) {
    spr.setTextDatum(TL_DATUM);
    spr.drawString(S.fxMsg[0] ? S.fxMsg : "sem cotacoes", 12, 32, 2);
    return;
  }
  int rowH = S.nfx <= 3 ? 30 : 23;
  for (int i = 0; i < S.nfx; i++) {
    const Fx& q = S.fx[i];
    int y = 26 + i * rowH;
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_MUTED, C_CARD);
    spr.drawString(q.lbl, 12, y, 2);
    char ch[12];
    snprintf(ch, sizeof(ch), "%+.1f%%", q.ch);
    spr.setTextColor(q.ch >= 0 ? C_GOOD : C_BAD, C_CARD);
    spr.drawString(ch, 12, y + 15, 1);
    spr.setTextDatum(TR_DATUM);
    spr.setTextColor(C_TEXT, C_CARD);
    char vv[16];
    fitText(q.val, vv, sizeof(vv), 96, 4);
    spr.drawString(vv, 148, y, 4);
  }
}

void drawHalf(int x, const char* kind) {
  spr.createSprite(HALF, H_MID);
  spr.fillSprite(C_BG);
  if (strcmp(kind, "none") != 0) {
    // cartao esquerdo encosta na margem de 6 px; o direito deixa 6 px no meio
    if (x == 0) spr.fillRoundRect(6, 0, 151, 120, 9, C_CARD);
    else spr.fillRoundRect(3, 0, 151, 120, 9, C_CARD);
    if (!strcmp(kind, "sys")) wSystem();
    else if (!strcmp(kind, "w")) wWeather();
    else if (!strcmp(kind, "wd")) wWeatherDetail();
    else if (!strcmp(kind, "fx")) wQuotes();
  }
  spr.pushSprite(x, Y_MID);
  spr.deleteSprite();
}

// Quebra o texto em linhas que cabem em maxW pixels (fonte f). Devolve quantas linhas.
int wrapText(const char* src, char lines[][44], int maxLines, int maxW, uint8_t f) {
  int n = 0;
  char cur[44] = "";
  char word[44];
  const char* p = src;
  while (*p && n < maxLines) {
    while (*p == ' ') p++;
    int wl = 0;
    while (p[wl] && p[wl] != ' ' && wl < 43) wl++;
    if (!wl) break;
    memcpy(word, p, wl);
    word[wl] = '\0';
    p += wl;
    char tryL[88];
    snprintf(tryL, sizeof(tryL), "%s%s%s", cur, cur[0] ? " " : "", word);
    if (spr.textWidth(tryL, f) <= maxW || !cur[0]) {
      strlcpy(cur, tryL, sizeof(cur));
    } else {
      strlcpy(lines[n++], cur, 44);
      strlcpy(cur, word, sizeof(cur));
    }
  }
  if (cur[0] && n < maxLines) strlcpy(lines[n++], cur, 44);
  return n;
}

// Aviso no lugar dos dois cartoes do meio (tela em regioes).
void drawNoteMid() {
  spr.createSprite(W, H_MID);
  spr.fillSprite(C_BG);
  spr.fillRoundRect(6, 0, W - 12, 120, 10, C_CARD);
  spr.drawRoundRect(6, 0, W - 12, 120, 10, ntCol);
  pxIconBig(spr, ntIc, 20, 44, 4);
  char lines[4][44];
  uint8_t f = 4;
  int n = wrapText(ntTx, lines, 3, W - 84, 4);
  if (n == 3 && strlen(ntTx) > 48) {  // texto longo: fonte menor, ate 4 linhas
    f = 2;
    n = wrapText(ntTx, lines, 4, W - 84, 2);
  }
  int lh = f == 4 ? 28 : 18;
  int y0 = (120 - n * lh) / 2;
  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(ntCol, C_CARD);
  for (int i = 0; i < n; i++) spr.drawString(lines[i], 66, y0 + i * lh, f);
  spr.setTextDatum(BR_DATUM);
  spr.setTextColor(C_MUTED, C_CARD);
  spr.drawString("toque para fechar", W - 16, 114, 1);
  spr.pushSprite(0, Y_MID);
  spr.deleteSprite();
}

void drawAgenda() {
  spr.createSprite(W, H_AGENDA);
  spr.fillSprite(C_BG);
  if (!strcmp(S.layB, "ev")) {
    spr.fillRoundRect(6, 0, W - 12, H_AGENDA - 4, 9, C_CARD);
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(C_MUTED, C_CARD);
    spr.drawString("AGENDA", 14, 5, 2);
    if (strcmp(S.evSt, "ok") != 0 || S.nev == 0) {
      const char* msg = S.evMsg[0] ? S.evMsg
                      : (strcmp(S.evSt, "ok") == 0 ? "nada nos proximos dias" : "Google nao conectado");
      spr.drawString(msg, 14, 26, 2);
    } else {
      int n = min(S.nev, 3);
      for (int i = 0; i < n; i++) {
        int y = 23 + i * 19;
        const Ev& e = S.ev[i];
        if (e.now) spr.fillRoundRect(9, y + 1, 3, 14, 1, C_ACCENT);
        spr.setTextColor(e.now ? C_ACCENT : C_BLUE, C_CARD);
        spr.drawString(e.when, 16, y, 2);
        char t[52];
        fitText(e.title, t, sizeof(t), W - 124, 2);
        spr.setTextColor(C_TEXT, C_CARD);
        spr.drawString(t, 104, y, 2);
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
// Aviso: faixa escura embaixo, com icone e texto rolando (relogio de mesa e estacao).
void drawNoteBanner(TFT_eSprite& s) {
  if (!ntOn) return;
  const int by = 172, bh = 56;
  s.fillRoundRect(8, by, W - 16, bh, 10, TFT_BLACK);
  s.drawRoundRect(8, by, W - 16, bh, 10, ntCol);
  pxIconBig(s, ntIc, 18, by + 12, 4);
  const int x0 = 60, x1 = W - 16, k = 3;
  int tw = pxTextW(ntTx, k, 0), avail = x1 - x0;
  int off = tw <= avail ? 0 : avail - (int)(((frameNo - ntT0) * 2) % (tw + avail));
  pxBig(s, ntTx, x0 + off, by + 21, k, ntCol, x0, x1, 0);
}

void clockFrame() {
  TFT_eSprite& s = sceneSpr;
  sceneFrame(s, frameNo);
  bool stale = isStale();

  long e = nowLocal();
  if (clockSize > 0) {
    char hm[6], sc[3];
    snprintf(hm, sizeof(hm), "%02ld:%02ld", (e / 3600) % 24, (e / 60) % 60);
    snprintf(sc, sizeof(sc), "%02ld", e % 60);
    int K = clockSize, KS = max(2, (K * 5 + 6) / 12), gap = max(4, K * 2 / 3);
    int wHM = pxTextW(hm, K, clockFont), wS = showSec ? pxTextW(sc, KS, clockFont) + gap : 0;
    int x = clockX < 0 ? (W - wHM - wS) / 2 : clockX;
    pxBig(s, hm, x, clockY, K, stale ? C_MUTED : clockCol, 0, W, clockFont);
    if (showSec) pxBig(s, sc, x + wHM + gap, clockY, KS, C_ACCENT, 0, W, clockFont);
  }

  char buf[64];
  if (dateSize > 0) {
    upcase(S.data, buf, sizeof(buf));
    pxBig(s, buf, dateX, dateY, dateSize, rgb(200, 204, 212), dateX, 316, clockFont);
  }
  if (weaSize > 0 && S.wOk) {
    char t[10];
    snprintf(t, sizeof(t), "%d~C", S.tc);
    int th = pxTextH(weaSize, clockFont);
    int ik = max(1, (th + 3) / 8);
    int iy = weaY + (th - 8 * ik) / 2;
    pxIconBig(s, weatherIconName(S.ic, S.day), weaX, iy, ik);
    int tx = weaX + 8 * ik + max(3, ik * 2);
    pxBig(s, t, tx, weaY, weaSize, C_TEXT, tx, 316, clockFont);
  }

  // proximo compromisso (fixo, sem rolar) ou o motivo de estar sem dados
  if (evSize > 0) {
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
    if (buf[0]) pxBig(s, buf, evX, evY, evSize, col, evX, 316, clockFont);
  }

  drawNoteBanner(s);
  s.pushSprite(0, 0);
}

// ---------- estacao do tempo ----------
void stCloud(TFT_eSprite& s, int x, int y, uint16_t c) {
  s.fillCircle(x + 10, y + 9, 9, c);
  s.fillCircle(x + 23, y + 5, 12, c);
  s.fillCircle(x + 37, y + 10, 8, c);
  s.fillRoundRect(x + 2, y + 9, 44, 11, 5, c);
}

// Ilustracao do tempo no canto (x 6..134, y 6..118), animada: sol girando, nuvens indo e
// vindo, chuva/neve caindo, raio piscando, grama e flores balancando.
void stationArt(TFT_eSprite& s, uint32_t fr) {
  const int X = 6, G = 100;  // G = linha do chao
  char ic = S.wOk ? S.ic : 'C';
  bool day = S.day;
  float drift = sinf(fr * 0.02f) * 5;
  if (ic == 'S' || ic == 'P') {
    int cx = X + 42, cy = 36;
    if (day) {
      float a0 = fr * 0.025f;
      for (int i = 0; i < 10; i++) {
        float a = a0 + i * 0.6283f;
        int r2 = 27 + ((fr / 8 + i) % 2) * 3;
        s.drawLine(cx + cosf(a) * 20, cy + sinf(a) * 20, cx + cosf(a) * r2, cy + sinf(a) * r2, rgb(255, 170, 0));
      }
      s.fillCircle(cx, cy, 16, rgb(255, 110, 0));
      s.fillCircle(cx - 4, cy - 4, 9, rgb(255, 190, 40));
    } else {
      s.fillCircle(cx, cy, 15, rgb(235, 230, 200));
      s.fillCircle(cx + 8, cy - 6, 14, TFT_BLACK);
      for (int i = 0; i < 6; i++)
        if ((fr / 10 + i) % 4) s.drawPixel(X + 10 + i * 21, 10 + (i * 17) % 50, TFT_WHITE);
    }
    if (ic == 'P') stCloud(s, X + 50 + drift, 44, TFT_WHITE);
  } else if (ic == 'F') {
    for (int i = 0; i < 6; i++) {
      int off = (int)(sinf(fr * 0.03f + i) * 8);
      s.fillRoundRect(X + 8 + off + (i % 2) * 14, 20 + i * 12, 96, 5, 2, rgb(150, 156, 168));
    }
  } else {
    // nublado / chuva / trovoada / neve
    bool dark = ic == 'R' || ic == 'T';
    stCloud(s, X + 4 + drift, 14, dark ? rgb(110, 116, 130) : rgb(200, 206, 216));
    stCloud(s, X + 56 - drift, 28, dark ? rgb(80, 86, 100) : TFT_WHITE);
    if (ic == 'R' || ic == 'T') {
      for (int i = 0; i < 12; i++) {
        int x = X + 14 + (i * 23) % 100, y = 56 + (int)((fr * 3 + i * 17) % 40);
        s.drawLine(x, y, x - 2, y + 6, rgb(90, 160, 255));
      }
    }
    if (ic == 'T' && (fr % 90) < 6) {  // raio
      int x = X + 70;
      s.drawLine(x, 52, x - 8, 66, rgb(255, 230, 60));
      s.drawLine(x - 8, 66, x, 66, rgb(255, 230, 60));
      s.drawLine(x, 66, x - 10, 84, rgb(255, 230, 60));
    }
    if (ic == 'N') {
      for (int i = 0; i < 14; i++) {
        int x = X + 10 + (i * 19) % 110 + (int)(sinf(fr * 0.05f + i) * 3), y = 56 + (int)((fr + i * 13) % 40);
        s.fillCircle(x, y, 1, TFT_WHITE);
      }
    }
  }
  // chao: grama balancando e duas flores
  s.fillRect(X, G + 6, 128, 10, rgb(24, 120, 36));
  for (int x = X; x < X + 128; x += 3) {
    int h = 6 + (x * 7) % 7;
    int sway = (int)(sinf(fr * 0.06f + x * 0.2f) * 2);
    s.drawLine(x, G + 7, x + sway, G + 7 - h, ((x / 3) % 2) ? rgb(70, 200, 80) : rgb(40, 160, 60));
  }
  const int fx[2] = {X + 20, X + 104};
  const uint16_t fc[2] = {rgb(255, 90, 170), rgb(255, 210, 40)};
  for (int i = 0; i < 2; i++) {
    int sway = (int)(sinf(fr * 0.05f + i * 2) * 2);
    s.drawLine(fx[i], G + 7, fx[i] + sway, G - 6, rgb(40, 160, 60));
    int px = fx[i] + sway, py = G - 8;
    for (int p = 0; p < 5; p++) s.fillCircle(px + cosf(p * 1.2566f) * 3, py + sinf(p * 1.2566f) * 3, 2, fc[i]);
    s.fillCircle(px, py, 1, rgb(255, 240, 120));
  }
}

void stationFrame() {
  TFT_eSprite& s = sceneSpr;
  s.fillSprite(TFT_BLACK);
  stationArt(s, frameNo);
  bool stale = isStale();
  long e = nowLocal();
  char hh[3], mm[3], sc[3];
  snprintf(hh, sizeof(hh), "%02ld", (e / 3600) % 24);
  snprintf(mm, sizeof(mm), "%02ld", (e / 60) % 60);
  snprintf(sc, sizeof(sc), "%02ld", e % 60);

  // hora grande a direita: calcula o maior tamanho que cabe em 176 px
  const int AX = 140, AW = 176;
  int K = 12;
  for (; K > 4; K--) {
    int w = pxTextW(hh, K, stFont) * 2 + K * 2 + (showSec ? pxTextW(sc, 3, stFont) + 4 : 0);
    if (w <= AW) break;
  }
  int wHH = pxTextW(hh, K, stFont), colW = K * 2;
  int wAll = wHH * 2 + colW + (showSec ? pxTextW(sc, 3, stFont) + 4 : 0);
  int x = AX + (AW - wAll) / 2, y = 10;
  uint16_t tc = stale ? C_MUTED : stCol;
  pxBig(s, hh, x, y, K, tc, 0, W, stFont);
  if (e % 2 == 0) {  // dois pontos piscando
    int cx = x + wHH + K / 2;
    s.fillRect(cx, y + K * 3 / 2, K, K, tc);
    s.fillRect(cx, y + K * 7 / 2 - K / 2, K, K, tc);
  }
  pxBig(s, mm, x + wHH + colW, y, K, tc, 0, W, stFont);
  if (showSec) pxBig(s, sc, x + wHH * 2 + colW + 4, y, 3, C_ACCENT, 0, W, stFont);

  // fileira de dias da semana, com o de hoje destacado
  static const char* DOW[7] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
  int ry = y + pxTextH(K, stFont) + 10;
  // fonte LCD pequena (GLCD 5x7): 3 letras = 17 px, sobra espaco entre os dias
  s.setTextDatum(TL_DATUM);
  for (int i = 0; i < 7; i++) {
    int dx = AX + 2 + i * 25;
    if (i == stWday) {
      s.fillRoundRect(dx - 3, ry - 2, 23, 12, 2, stCol2);
      s.setTextColor(TFT_BLACK);
    } else {
      s.setTextColor(rgb(210, 70, 60));
    }
    s.drawString(DOW[i], dx, ry, 1);
  }
  char buf[48];
  upcase(S.data, buf, sizeof(buf));
  pxBig(s, buf, AX + 2, ry + 18, 2, C_MUTED, 0, W, 0);

  // umidade e temperatura grandes embaixo
  const int by = 128, KB = 9;
  pxBig(s, "UMIDADE", 12, by, 2, C_MUTED, 0, W, 0);
  pxBig(s, "TEMPERATURA", 166, by, 2, C_MUTED, 0, W, 0);
  if (S.wOk) {
    snprintf(buf, sizeof(buf), "%d", S.hu);
    pxBig(s, buf, 12, by + 18, KB, stCol2, 0, 160, stFont);
    pxBig(s, "%", 12 + pxTextW(buf, KB, stFont) + 6, by + 18 + KB * 2, 4, stCol2, 0, 160, 0);
    snprintf(buf, sizeof(buf), "%.1f", stTd);
    int tw = pxTextW(buf, KB, stFont);
    pxBig(s, buf, 166, by + 18, KB, stCol2, 0, W, stFont);
    pxBig(s, "~C", 166 + tw + 5, by + 18, 3, stCol2, 0, W, 0);
    // linha de baixo: minima, maxima e chuva do dia
    snprintf(buf, sizeof(buf), "MIN %d  MAX %d  CHUVA %d%%", S.mn, S.mx, S.rx);
    pxBig(s, buf, 12, 222, 2, C_MUTED, 0, W, 0);
  }
  if (stale && linkErr[0]) {
    upcase(linkErr, buf, sizeof(buf));
    s.fillRect(0, 218, W, 22, TFT_BLACK);
    pxBig(s, buf, 12, 222, 2, C_BAD, 0, W, 0);
  }
  drawNoteBanner(s);
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
  if (ntOn) {
    // aviso no lugar dos apps enquanto estiver ativo
    PxApp a;
    strlcpy(a.icon, ntIc, sizeof(a.icon));
    a.col = ntCol;
    a.dur = 60;
    a.clk = false;
    strlcpy(a.tx, ntTx, sizeof(a.tx));
    pixelBuild(pxShow, a, frameNo - ntT0);
  } else if (nPxApps == 0) {
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
  if (ntOn) {
    drawNoteMid();
  } else {
    drawHalf(0, S.layL);
    drawHalf(HALF, S.layR);
  }
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
  cfgBr = br;  // o brilho e aplicado aos poucos em lightLoop()
  if (redraw) tft.fillScreen(C_BG);
}

// Brilho: alvo = 0 (horario apagado), automatico pelo sensor de luz, ou o manual.
// Muda aos poucos para nao dar "pulo" de luz.
void lightLoop() {
  if (millis() - lastLight < 100) return;
  lastLight = millis();
  int raw = analogRead(PIN_LDR);
  ldrAvg = ldrAvg < 0 ? raw : ldrAvg * 0.9f + raw * 0.1f;
  int target = cfgBr;
  if (screenOff) {
    target = 0;
  } else if (autoBr && ldrDark != ldrBright) {
    // dark/bright sao leituras calibradas pelo painel; funciona em qualquer sentido do sensor
    float t = (ldrAvg - ldrDark) / (float)(ldrBright - ldrDark);
    t = constrain(t, 0.0f, 1.0f);
    target = brMin + (int)(t * (brMax - brMin));
  }
  if (brNow < 0) brNow = target;
  int d = target - brNow;
  if (d) brNow += d > 0 ? max(1, d / 6) : min(-1, d / 6);
  analogWrite(TFT_BL, brNow);
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
  if (!cfg.isNull()) {
    applyConfig(cfg["br"] | cfgBr, cfg["inv"] | cfgInv, cfg["rot"] | cfgRot);
    autoBr = (cfg["ab"] | 0) == 1;
    screenOff = (cfg["off"] | 0) == 1;
    ldrDark = cfg["ld"] | ldrDark;
    ldrBright = cfg["lb"] | ldrBright;
    brMin = cfg["bm"] | brMin;
    brMax = cfg["bx"] | brMax;
  }

  // aviso por cima da tela
  JsonObject nt = doc["nt"];
  bool wasNt = ntOn;
  if (!nt.isNull()) {
    long id = nt["id"] | 0L;
    if (!ntOn || id != ntId) ntT0 = frameNo;
    ntOn = true;
    ntId = id;
    strlcpy(ntTx, nt["tx"] | "", sizeof(ntTx));
    strlcpy(ntIc, nt["ic"] | "bell", sizeof(ntIc));
    ntCol = nt["c"] | 0xFFFF;
  } else {
    ntOn = false;
  }
  if (wasNt && !ntOn && mode == M_PIXEL) pxT0 = frameNo, pxTr = 99;  // volta aos apps do inicio do app atual

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
  Mode want = !strcmp(f, "clock") ? M_CLOCK : !strcmp(f, "pixel") ? M_PIXEL : !strcmp(f, "station") ? M_STATION : M_GRID;
  if (want == M_STATION) {
    stCol = lay["col"] | 0xFFFF;
    stCol2 = lay["col2"] | 0x5E9F;
    stFont = constrain((int)(lay["fn"] | 2), 0, 4);
    stWday = constrain((int)(lay["wd"] | 0), 0, 6);
  }
  const char* scn = lay["sc"] | "none";
  showSec = (lay["sec"] | 1) == 1;
  clockCol = lay["col"] | 0xFE87;
  clockFont = constrain((int)(lay["fn"] | 0), 0, 4);
  clockSize = constrain((int)(lay["cs"] | 12), 4, 18);
  clockX = constrain((int)(lay["cx"] | -1), -1, 300);
  clockY = constrain((int)(lay["cy"] | 14), 0, 225);
  dateSize = constrain((int)(lay["ds"] | 3), 0, 8);
  dateX = constrain((int)(lay["dx"] | 12), 0, 300);
  dateY = constrain((int)(lay["dy"] | 88), 0, 228);
  weaSize = constrain((int)(lay["ws"] | 3), 0, 8);
  weaX = constrain((int)(lay["wx"] | 238), 0, 300);
  weaY = constrain((int)(lay["wy"] | 88), 0, 228);
  evSize = constrain((int)(lay["es"] | 3), 0, 8);
  evX = constrain((int)(lay["ex"] | 12), 0, 300);
  evY = constrain((int)(lay["ey"] | 110), 0, 228);
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
    stTd = w["td"] | (float)S.tc;
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
      h.rain = it[3] | 0;
    }
    S.hu = w["hu"] | 0;
    S.rp = w["rp"] | 0;
    S.rx = w["rx"] | 0;
    strlcpy(S.sr, w["sr"] | "", sizeof(S.sr));
    strlcpy(S.ss, w["ss"] | "", sizeof(S.ss));
    JsonArray tm = w["tm"];
    if (!tm.isNull()) {
      S.tmMn = tm[0] | 0;
      S.tmMx = tm[1] | 0;
      S.tmIc = (tm[2] | "C")[0];
      S.tmRain = tm[3] | 0;
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
  // leva a leitura do sensor de luz: o painel mostra ao vivo para calibrar o brilho automatico
  String url = String(srvUrl) + "/api/screen?ldr=" + String((int)max(0.0f, ldrAvg));
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

// ---------- toque na tela ----------
// Toque curto: fecha o aviso; no pixel, proximo app; nas outras telas, proxima tela.
// Segurar 1 s: no pixel, proxima tela; nas outras, volta ao automatico.
// Com a tela apagada pelo horario, qualquer toque acorda.
void sendTouch(const char* action) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  if (!http.begin(String(srvUrl) + "/api/touch")) return;
  http.setTimeout(2000);
  http.addHeader("Authorization", String("Bearer ") + srvToken);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(String("{\"a\":\"") + action + "\"}");
  http.end();
  Serial.printf("toque: %s -> %d\n", action, code);
  lastPoll = 0;  // busca a tela nova ja no proximo loop
}

void onTap() {
  if (screenOff) {
    sendTouch("wake");
  } else if (ntOn) {
    ntOn = false;
    drawAll();
    sendTouch("dismiss");
  } else if (mode == M_PIXEL && nPxApps > 1) {
    memcpy(pxPrev, pxCur, sizeof(PxBuf));
    pxIdx = (pxIdx + 1) % nPxApps;
    pxT0 = frameNo;
    pxTr = 0;
  } else {
    sendTouch("next");
  }
}

void onHold() { sendTouch(mode == M_PIXEL ? "next" : "auto"); }

void touchLoop() {
  static bool down = false, held = false;
  static unsigned long t0 = 0, lastUp = 0;
  bool pressed = ts.tirqTouched() && ts.touched();
  if (pressed) {
    if (!down && millis() - lastUp > 150) {  // anti-repique
      down = true;
      held = false;
      t0 = millis();
    } else if (down && !held && millis() - t0 > 1000) {
      held = true;
      onHold();
    }
  } else if (down) {
    down = false;
    lastUp = millis();
    if (!held && millis() - t0 > 40) onTap();
  }
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
  // sensor de luz: atenuacao 0 dB deixa o LDR da CYD mais sensivel
  analogSetPinAttenuation(PIN_LDR, ADC_0db);
  // toque no SPI proprio (VSPI); a tela continua no HSPI
  touchSpi.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);
  setupWifi();
  drawAll();
}

void loop() {
  ArduinoOTA.handle();
  touchLoop();
  lightLoop();
  if (millis() - lastPoll >= (mode != M_GRID ? POLL_ANIM_MS : POLL_MS)) {
    lastPoll = millis();
    poll();
  }
  if (mode != M_GRID) {
    // relogio e pixel andam sozinhos entre um pedido e outro
    if (millis() - lastFrame >= FRAME_MS && sceneSpr.created()) {
      lastFrame = millis();
      if (mode == M_CLOCK) clockFrame();
      else if (mode == M_STATION) stationFrame();
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
