// Animacoes em pixel art para a metade de baixo do relogio de mesa (320x120).
// Desenham num sprite de 8 bits (38 KB) que fica alocado so enquanto o relogio esta na tela.
#pragma once
#include <TFT_eSPI.h>

constexpr int SCENE_W = 320;
constexpr int SCENE_H = 120;

// Reinicia o estado da cena ("aquarium", "stars", "matrix", "fire" ou "none").
void sceneBegin(const char* name);
// Desenha um quadro. fr = contador de quadros (~30 por segundo).
void sceneFrame(TFT_eSprite& s, uint32_t fr);
