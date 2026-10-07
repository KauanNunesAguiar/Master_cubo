// CubieCube.cpp
#include "CubieCube.h"

// Índice linear do facelet (0..53) de cada adesivo de cada peça, na ordem acima
static const uint8_t CORNER_FACELET[8][3] = {{8, 9, 20},   {6, 18, 38},  {0, 36, 47},  {2, 45, 11},
                                             {29, 26, 15}, {27, 44, 24}, {33, 53, 42}, {35, 17, 51}};
static const uint8_t CORNER_COLOR[8][3] = {{FACE_U, FACE_R, FACE_F}, {FACE_U, FACE_F, FACE_L}, {FACE_U, FACE_L, FACE_B},
                                           {FACE_U, FACE_B, FACE_R}, {FACE_D, FACE_F, FACE_R}, {FACE_D, FACE_L, FACE_F},
                                           {FACE_D, FACE_B, FACE_L}, {FACE_D, FACE_R, FACE_B}};
static const uint8_t EDGE_FACELET[12][2] = {{5, 10},  {7, 19},  {3, 37},  {1, 46},  {32, 16}, {28, 25},
                                            {30, 43}, {34, 52}, {23, 12}, {21, 41}, {50, 39}, {48, 14}};
static const uint8_t EDGE_COLOR[12][2] = {{FACE_U, FACE_R}, {FACE_U, FACE_F}, {FACE_U, FACE_L}, {FACE_U, FACE_B},
                                          {FACE_D, FACE_R}, {FACE_D, FACE_F}, {FACE_D, FACE_L}, {FACE_D, FACE_B},
                                          {FACE_F, FACE_R}, {FACE_F, FACE_L}, {FACE_B, FACE_L}, {FACE_B, FACE_R}};

static CubieCube g_moves[6];

static uint8_t colorAt(const CubeState& s, uint8_t f) { return s.get(f / 9, f % 9); }

void CubieCube::reset() {
	for (uint8_t i = 0; i < 8; i++) {
		cp[i] = i;
		co[i] = 0;
	}
	for (uint8_t i = 0; i < 12; i++) {
		ep[i] = i;
		eo[i] = 0;
	}
}

void CubieCube::multiply(const CubieCube& b) {
	uint8_t ncp[8], nco[8], nep[12], neo[12];
	for (uint8_t i = 0; i < 8; i++) {
		ncp[i] = cp[b.cp[i]];
		nco[i] = (co[b.cp[i]] + b.co[i]) % 3;
	}
	for (uint8_t i = 0; i < 12; i++) {
		nep[i] = ep[b.ep[i]];
		neo[i] = (eo[b.ep[i]] + b.eo[i]) & 1;
	}
	for (uint8_t i = 0; i < 8; i++) {
		cp[i] = ncp[i];
		co[i] = nco[i];
	}
	for (uint8_t i = 0; i < 12; i++) {
		ep[i] = nep[i];
		eo[i] = neo[i];
	}
}

bool CubieCube::fromFacelets(const CubeState& s) {
	for (uint8_t i = 0; i < 8; i++) {
		uint8_t ori = 0;
		while (ori < 3) {  // a torção é a posição do adesivo U ou D dentro do canto
			uint8_t c = colorAt(s, CORNER_FACELET[i][ori]);
			if (c == FACE_U || c == FACE_D) break;
			ori++;
		}
		if (ori == 3) return false;
		uint8_t c1 = colorAt(s, CORNER_FACELET[i][(ori + 1) % 3]);
		uint8_t c2 = colorAt(s, CORNER_FACELET[i][(ori + 2) % 3]);
		bool found = false;
		for (uint8_t j = 0; j < 8 && !found; j++)
			if (CORNER_COLOR[j][1] == c1 && CORNER_COLOR[j][2] == c2) {
				cp[i] = j;
				co[i] = ori;
				found = true;
			}
		if (!found) return false;
	}
	for (uint8_t i = 0; i < 12; i++) {
		uint8_t a = colorAt(s, EDGE_FACELET[i][0]), b = colorAt(s, EDGE_FACELET[i][1]);
		bool found = false;
		for (uint8_t j = 0; j < 12 && !found; j++) {
			if (a == EDGE_COLOR[j][0] && b == EDGE_COLOR[j][1]) {
				ep[i] = j;
				eo[i] = 0;
				found = true;
			} else if (a == EDGE_COLOR[j][1] && b == EDGE_COLOR[j][0]) {
				ep[i] = j;
				eo[i] = 1;
				found = true;
			}
		}
		if (!found) return false;
	}
	return true;
}

void CubieCube::toFacelets(CubeState& s) const {
	for (uint8_t f = 0; f < 6; f++) s.set(f, 4, f);  // centros
	for (uint8_t i = 0; i < 8; i++)
		for (uint8_t n = 0; n < 3; n++) {
			uint8_t f = CORNER_FACELET[i][(n + co[i]) % 3];
			s.set(f / 9, f % 9, CORNER_COLOR[cp[i]][n]);
		}
	for (uint8_t i = 0; i < 12; i++)
		for (uint8_t n = 0; n < 2; n++) {
			uint8_t f = EDGE_FACELET[i][(n + eo[i]) % 2];
			s.set(f / 9, f % 9, EDGE_COLOR[ep[i]][n]);
		}
}

static uint8_t parity(const uint8_t* p, uint8_t n) {  // paridade das inversões
	uint8_t inv = 0;
	for (uint8_t i = 0; i < n; i++)
		for (uint8_t j = i + 1; j < n; j++)
			if (p[i] > p[j]) inv++;
	return inv & 1;
}

int CubieCube::verify() const {
	uint16_t seen = 0;
	int sum = 0;
	for (uint8_t i = 0; i < 12; i++) {
		seen |= 1u << ep[i];
		sum += eo[i];
	}
	if (seen != 0xFFF) return -2;  // alguma aresta repetida/ausente
	if (sum & 1) return -3;        // inversão de arestas inválida
	seen = 0;
	sum = 0;
	for (uint8_t i = 0; i < 8; i++) {
		seen |= 1u << cp[i];
		sum += co[i];
	}
	if (seen != 0xFF) return -4;  // algum canto repetido/ausente
	if (sum % 3) return -5;       // torção de cantos inválida
	if (parity(ep, 12) != parity(cp, 8)) return -6;
	return 0;
}

void CubieCube::initMoves() {
	CubeState::init();
	for (uint8_t f = 0; f < 6; f++) {
		CubeState t;
		t.applyMove(f, 1);
		g_moves[f].fromFacelets(t);
	}
}

const CubieCube& CubieCube::move(uint8_t face) { return g_moves[face]; }