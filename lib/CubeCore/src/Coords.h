// Coords.h — coordenadas do Kociemba (funções livres sobre CubieCube)
#pragma once
#include <stdint.h>

#include "CubieCube.h"

constexpr uint16_t N_TWIST = 2187, N_FLIP = 2048, N_SLICE = 495, N_PERM8 = 40320, N_SLICE_PERM = 24;
constexpr uint8_t N_MOVES = 18, N_MOVES_P2 = 10;

/* Giro m (0..17): face = m / 3, voltas = m % 3 + 1  (U,U2,U',R,R2,R',F,...) */
extern const uint8_t P2_MOVES[N_MOVES_P2];  // índices dos 10 giros da fase 2
void coordsInit();                          // chamar uma vez (gera os 18 giros em peças)
const CubieCube& moveCubie(uint8_t m);

// Fase 1 (valem para qualquer cubo)
uint16_t getTwist(const CubieCube& c);
void setTwist(CubieCube& c, uint16_t v);
uint16_t getFlip(const CubieCube& c);
void setFlip(CubieCube& c, uint16_t v);
uint16_t getSlice(const CubieCube& c);
void setSlice(CubieCube& c, uint16_t v);

// Fase 2 (udperm e sliceperm só valem com as arestas do meio já no meio)
uint16_t getCPerm(const CubieCube& c);
void setCPerm(CubieCube& c, uint16_t v);
uint16_t getUDPerm(const CubieCube& c);
void setUDPerm(CubieCube& c, uint16_t v);
uint16_t getSlicePerm(const CubieCube& c);
void setSlicePerm(CubieCube& c, uint16_t v);

/* Tabela de movimento: table[coord * nMoves + k] = coordenada após aplicar o giro k.
 * moves = lista de índices de giro (nullptr = todos os 18). */
typedef uint16_t (*CoordGet)(const CubieCube&);
typedef void (*CoordSet)(CubieCube&, uint16_t);
void buildMoveTable(uint16_t* table, uint16_t n, CoordGet get, CoordSet set, const uint8_t* moves, uint8_t nMoves);