// CubeState.cpp
#include "CubeState.h"

#include <Arduino.h>
#include <string.h>

#include <cstdlib>

const CubeFaceGeom CUBE_FACE_GEOM[6] = {
    {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},     // U
    {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}},   // R
    {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}},    // F
    {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},   // D
    {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}},   // L
    {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}},  // B
};

static const char FACE_CHARS[7] = "URFDLB";

/* ------------------------------------------------------------------ *
 * Tabelas de permutação                                              *
 * Em vez de digitar os ciclos de cada giro à mão (fácil de errar),   *
 * eles são gerados uma vez a partir da geometria 3D: cada adesivo    *
 * tem posição e normal; os da camada giram -90° (horário visto de    *
 * fora) em torno do eixo da face e procuramos onde caíram.           *
 * ------------------------------------------------------------------ */
static uint8_t g_perm[6][54];  // g_perm[face][i] = destino do adesivo i após 1 giro horário
static bool g_ready = false;

// Posição do centro do adesivo, em unidades de meio-adesivo (cubo vai de -3 a +3)
static void stickerPos(uint8_t f, uint8_t idx, int p[3]) {
	const CubeFaceGeom& g = CUBE_FACE_GEOM[f];
	int c = (int)(idx % 3) - 1, r = (int)(idx / 3) - 1;
	for (int k = 0; k < 3; k++) p[k] = 3 * g.n[k] + 2 * c * g.u[k] + 2 * r * g.v[k];
}

static int dot3(const int a[3], const int b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Rotação de -90° (horário visto de fora) de v em torno do eixo unitário a: v' = -(a x v) + a (a . v)
static void rotCW(const int a[3], const int v[3], int out[3]) {
	int cr[3] = {a[1] * v[2] - a[2] * v[1], a[2] * v[0] - a[0] * v[2], a[0] * v[1] - a[1] * v[0]};
	int d = dot3(a, v);
	for (int k = 0; k < 3; k++) out[k] = -cr[k] + a[k] * d;
}

static int findSticker(const int n[3], const int p[3]) {
	for (uint8_t f = 0; f < 6; f++) {
		const CubeFaceGeom& g = CUBE_FACE_GEOM[f];
		if (g.n[0] != n[0] || g.n[1] != n[1] || g.n[2] != n[2]) continue;
		for (uint8_t i = 0; i < 9; i++) {
			int q[3];
			stickerPos(f, i, q);
			if (q[0] == p[0] && q[1] == p[1] && q[2] == p[2]) return f * 9 + i;
		}
	}
	return -1;
}

static void initTables() {
	if (g_ready) return;
	for (uint8_t m = 0; m < 6; m++) {
		int a[3] = {CUBE_FACE_GEOM[m].n[0], CUBE_FACE_GEOM[m].n[1], CUBE_FACE_GEOM[m].n[2]};
		for (uint8_t f = 0; f < 6; f++)
			for (uint8_t i = 0; i < 9; i++) {
				int p[3], n[3] = {CUBE_FACE_GEOM[f].n[0], CUBE_FACE_GEOM[f].n[1], CUBE_FACE_GEOM[f].n[2]};
				stickerPos(f, i, p);
				int dest = f * 9 + i;
				if (dot3(p, a) > 0) {  // adesivo pertence à camada que gira
					int p2[3], n2[3];
					rotCW(a, p, p2);
					rotCW(a, n, n2);
					int d = findSticker(n2, p2);
					if (d >= 0) dest = d;
				}
				g_perm[m][f * 9 + i] = (uint8_t)dest;
			}
	}
	g_ready = true;
}

/* ------------------------------------------------------------------ */

void CubeState::init() { initTables(); }

void CubeState::reset() {
	for (uint8_t f = 0; f < 6; f++)
		for (uint8_t i = 0; i < 9; i++) _s[f * 9 + i] = f;
}

void CubeState::applyMove(uint8_t face, uint8_t turns) {
	if (face >= 6) return;
	initTables();
	turns &= 3;
	while (turns--) {
		uint8_t old[54];
		memcpy(old, _s, 54);
		for (uint8_t i = 0; i < 54; i++) _s[g_perm[face][i]] = old[i];
	}
}

static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool CubeState::nextMove(const char*& p, CubeMove& m) {
	while (isSpace(*p)) p++;
	if (!*p) return false;

	int face = -1;
	for (int i = 0; i < 6; i++)
		if (*p == FACE_CHARS[i]) face = i;
	if (face < 0) return false;

	const char* q = p + 1;
	uint8_t turns = 1;
	if (*q == '\'') {
		turns = 3;
		q++;
	} else if (*q == '2') {
		turns = 2;
		q++;
		if (*q == '\'') q++;  // aceita "R2'" como R2
	} else if (*q == '3') {
		turns = 3;
		q++;
	} else if (*q == '1') {
		q++;
	}
	if (*q && !isSpace(*q)) return false;  // ex.: "R2U" ou "RR"

	m.face = (uint8_t)face;
	m.turns = turns;
	p = q;
	return true;
}

bool CubeState::applyMoves(const char* seq) {
	// 1ª passada: valida tudo (não aplica nada se houver erro)
	const char* p = seq;
	CubeMove m;
	while (nextMove(p, m)) {}
	if (*p) return false;
	// 2ª passada: aplica
	p = seq;
	while (nextMove(p, m)) applyMove(m);
	return true;
}

bool CubeState::isSolved() const {
	for (uint8_t f = 0; f < 6; f++)
		for (uint8_t i = 0; i < 9; i++)
			if (_s[f * 9 + i] != _s[f * 9 + 4]) return false;
	return true;
}

void CubeState::toString(char out[55]) const {
	for (uint8_t i = 0; i < 54; i++) out[i] = FACE_CHARS[_s[i] < 6 ? _s[i] : 0];
	out[54] = '\0';
}

bool CubeState::fromString(const char* s) {
	uint8_t tmp[54];
	for (uint8_t i = 0; i < 54; i++) {
		int f = -1;
		for (int k = 0; k < 6; k++)
			if (s[i] == FACE_CHARS[k]) f = k;
		if (f < 0) return false;  // também cobre string curta (chega no '\0')
		tmp[i] = (uint8_t)f;
	}
	memcpy(_s, tmp, 54);
	return true;
}