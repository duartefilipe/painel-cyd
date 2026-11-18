// Desenho com as fontes pixeladas (3x5 e 5x7) e os icones 8x8 de pixel_assets.h
// (gerados por tools/pixel_assets.py, os mesmos da previa do painel web).
#pragma once
#include <TFT_eSPI.h>

// Estilos de fonte do relogio de mesa:
// 0 = pixel 3x5, 1 = LED redondo 3x5, 2 = 7 segmentos, 3 = arcade 5x7, 4 = lisa 5x7
enum PxFont { PX_F_PIXEL = 0, PX_F_DOTS = 1, PX_F_SEG = 2, PX_F_ARCADE = 3, PX_F_CLEAN = 4 };

// Largura do texto em colunas da fonte 3x5 (1 coluna de espaco entre letras).
int pxWidth(const char* t);
// Largura e altura em pixels do texto na escala k e fonte escolhida.
int pxTextW(const char* t, int k, int font = 0);
int pxTextH(int k, int font = 0);

// Texto ampliado num sprite, com sombra escura (relogio de mesa).
// So desenha dentro de [clip0, clip1).
void pxBig(TFT_eSprite& s, const char* t, int x, int y, int k, uint16_t col, int clip0 = 0, int clip1 = 10000, int font = 0);
// Icone 8x8 ampliado k vezes.
void pxIconBig(TFT_eSprite& s, const char* name, int x, int y, int k);
bool pxHasIcon(const char* name);

// Matriz 32x8 (relogio pixel): 0 = LED apagado.
typedef uint16_t PxBuf[8][32];
void pxBufClear(PxBuf b);
void pxBufText(PxBuf b, const char* t, int x, int y, uint16_t col, int clip0, int clip1, int dimChar = -1, uint16_t dimCol = 0);
void pxBufIcon(PxBuf b, const char* name, int x);
