// Desenho com a fonte pixelada 3x5 e os icones 8x8 de pixel_assets.h
// (gerados por tools/pixel_assets.py, os mesmos da previa do painel web).
#pragma once
#include <TFT_eSPI.h>

// Largura do texto em colunas da fonte (1 coluna de espaco entre letras).
int pxWidth(const char* t);
// Coluna onde comeca o caractere de indice i.
int pxCharX(const char* t, int i);

// Texto em blocos de k x k px num sprite, com sombra escura (relogio de mesa).
// So desenha blocos com x dentro de [clip0, clip1).
void pxBig(TFT_eSprite& s, const char* t, int x, int y, int k, uint16_t col, int clip0 = 0, int clip1 = 10000);
// Icone 8x8 ampliado k vezes.
void pxIconBig(TFT_eSprite& s, const char* name, int x, int y, int k);
bool pxHasIcon(const char* name);

// Matriz 32x8 (relogio pixel): 0 = LED apagado.
typedef uint16_t PxBuf[8][32];
void pxBufClear(PxBuf b);
void pxBufText(PxBuf b, const char* t, int x, int y, uint16_t col, int clip0, int clip1, int dimChar = -1, uint16_t dimCol = 0);
void pxBufIcon(PxBuf b, const char* name, int x);
