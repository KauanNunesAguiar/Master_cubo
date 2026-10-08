// Coords.cpp
#include "Coords.h"

const uint8_t P2_MOVES[N_MOVES_P2] = {0, 1, 2, 4, 7, 9, 10, 11, 13, 16};  // U U2 U' R2 F2 D D2 D' L2 B2
static CubieCube g_m18[N_MOVES];
static uint16_t g_cnk[13][5];  // g_cnk[n][k] = C(n,k), preenchida em coordsInit()

void coordsInit() {
	for (uint8_t n = 0; n < 13; n++)
		for (uint8_t k = 0; k < 5; k++)
			g_cnk[n][k] = (k == 0) ? 1 : (n == 0) ? 0 : g_cnk[n - 1][k - 1] + g_cnk[n - 1][k];

	CubieCube::initMoves();
	for (uint8_t f = 0; f < 6; f++) {
		g_m18[f * 3] = CubieCube::move(f);
		g_m18[f * 3 + 1] = g_m18[f * 3];
		g_m18[f * 3 + 1].multiply(CubieCube::move(f));
		g_m18[f * 3 + 2] = g_m18[f * 3 + 1];
		g_m18[f * 3 + 2].multiply(CubieCube::move(f));
	}
}
const CubieCube& moveCubie(uint8_t m) { return g_m18[m]; }

/* ---- torção e inversão: números em base 3 / base 2; a última peça é determinada pelas outras ---- */
uint16_t getTwist(const CubieCube& c) {
	uint16_t t = 0;
	for (uint8_t i = 0; i < 7; i++) t = 3 * t + c.co[i];
	return t;
}
void setTwist(CubieCube& c, uint16_t t) {
	uint8_t sum = 0;
	for (int8_t i = 6; i >= 0; i--) {
		c.co[i] = t % 3;
		sum += c.co[i];
		t /= 3;
	}
	c.co[7] = (3 - sum % 3) % 3;
}
uint16_t getFlip(const CubieCube& c) {
	uint16_t f = 0;
	for (uint8_t i = 0; i < 11; i++) f = 2 * f + c.eo[i];
	return f;
}
void setFlip(CubieCube& c, uint16_t f) {
	uint8_t sum = 0;
	for (int8_t i = 10; i >= 0; i--) {
		c.eo[i] = f & 1;
		sum += c.eo[i];
		f >>= 1;
	}
	c.eo[11] = sum & 1;
}

/* ---- slice: sistema de numeração combinatória (qual subconjunto de 4 posições entre 12) ---- */
static inline uint16_t cnk(uint8_t n, uint8_t k) { return (k > n) ? 0 : g_cnk[n][k]; }
uint16_t getSlice(const CubieCube& c) {
	uint16_t a = 0;
	uint8_t x = 0;
	for (int8_t j = 11; j >= 0; j--)
		if (c.ep[j] >= 8) a += cnk(11 - j, ++x);  // aresta do meio nessa posição
	return a;
}
void setSlice(CubieCube& c, uint16_t a) {
	bool slot[12] = {false};
	for (int8_t r = 4; r >= 1; r--) {  // decodificação gulosa
		uint8_t k = r - 1;
		while (k < 11 && cnk(k + 1, r) <= a) k++;
		a -= cnk(k, r);
		slot[11 - k] = true;
	}
	uint8_t s = 8, o = 0;
	for (uint8_t j = 0; j < 12; j++) c.ep[j] = slot[j] ? s++ : o++;
}

/* ---- permutações: código de Lehmer (0 = identidade) ---- */
static uint16_t permRank(const uint8_t* p, uint8_t n) {
	uint16_t r = 0;
	for (uint8_t i = 0; i < n; i++) {
		uint8_t c = 0;
		for (uint8_t j = i + 1; j < n; j++)
			if (p[j] < p[i]) c++;
		r = r * (n - i) + c;
	}
	return r;
}
static void permUnrank(uint16_t r, uint8_t n, uint8_t* out) {
	uint8_t c[8];
	for (int8_t i = n - 1; i >= 0; i--) {
		uint8_t base = n - i;
		c[i] = r % base;
		r /= base;
	}
	bool used[8] = {false};
	for (uint8_t i = 0; i < n; i++) {
		uint8_t k = c[i];
		for (uint8_t v = 0; v < n; v++)
			if (!used[v]) {
				if (k == 0) {
					out[i] = v;
					used[v] = true;
					break;
				}
				k--;
			}
	}
}

uint16_t getCPerm(const CubieCube& c) { return permRank(c.cp, 8); }
void setCPerm(CubieCube& c, uint16_t v) { permUnrank(v, 8, c.cp); }
uint16_t getUDPerm(const CubieCube& c) { return permRank(c.ep, 8); }  // ep[0..7]
void setUDPerm(CubieCube& c, uint16_t v) { permUnrank(v, 8, c.ep); }
uint16_t getSlicePerm(const CubieCube& c) {
	uint8_t t[4];
	for (uint8_t i = 0; i < 4; i++) t[i] = c.ep[8 + i] - 8;
	return permRank(t, 4);
}
void setSlicePerm(CubieCube& c, uint16_t v) {
	uint8_t t[4];
	permUnrank(v, 4, t);
	for (uint8_t i = 0; i < 4; i++) c.ep[8 + i] = t[i] + 8;
}

/* ---- tabela de movimento ---- */
void buildMoveTable(uint16_t* table, uint16_t n, CoordGet get, CoordSet set, const uint8_t* moves, uint8_t nMoves) {
	for (uint16_t i = 0; i < n; i++)
		for (uint8_t k = 0; k < nMoves; k++) {
			CubieCube c;  // resolvido; o setter só mexe na parte dessa coordenada
			set(c, i);
			c.multiply(moveCubie(moves ? moves[k] : k));
			table[(uint32_t)i * nMoves + k] = get(c);
		}
}