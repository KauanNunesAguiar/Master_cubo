// gen.cpp — gera as 4 tabelas de poda (4 bits por entrada) para a W25Q16
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Coords.h"

typedef std::vector<uint16_t> Tab16;

static Tab16 moveTable(uint16_t n, CoordGet g, CoordSet s, const uint8_t* mv, uint8_t nm) {
	Tab16 t((size_t)n * nm);
	buildMoveTable(t.data(), n, g, s, mv, nm);
	return t;
}

// d[a*nB + b] = menor número de giros para levar (a,b) até (0,0)
static std::vector<uint8_t> buildPrune(const Tab16& mA, uint32_t nA, const Tab16& mB, uint32_t nB, uint8_t nm) {
	std::vector<uint8_t> d((size_t)nA * nB, 0xFF);
	d[0] = 0;
	size_t filled = 1;
	for (uint8_t depth = 0; filled < d.size(); depth++) {
		size_t before = filled;
		for (uint32_t a = 0; a < nA; a++)
			for (uint32_t b = 0; b < nB; b++) {
				if (d[(size_t)a * nB + b] != depth) continue;
				for (uint8_t k = 0; k < nm; k++) {
					size_t i = (size_t)mA[a * nm + k] * nB + mB[b * nm + k];
					if (d[i] == 0xFF) {
						d[i] = depth + 1;
						filled++;
					}
				}
			}
		printf("  profundidade %d: %zu novas\n", depth + 1, filled - before);
		if (filled == before) break;  // nada novo: terminou
		if (depth + 1 > 15) {
			printf("ERRO: não cabe em 4 bits\n");
			exit(1);
		}
	}
	printf("  total %zu de %zu\n", filled, d.size());
	return d;
}

// 2 entradas por byte: índice par = nibble baixo, ímpar = nibble alto
static void save(const char* name, const std::vector<uint8_t>& d) {
	FILE* f = fopen(name, "wb");
	for (size_t i = 0; i < d.size(); i += 2) {
		uint8_t lo = d[i], hi = (i + 1 < d.size()) ? d[i + 1] : 0;
		fputc(lo | (hi << 4), f);
	}
	fclose(f);
	printf("  gravado %s\n", name);
}

int main() {
	coordsInit();
	Tab16 mTwist = moveTable(N_TWIST, getTwist, setTwist, nullptr, N_MOVES);
	Tab16 mFlip = moveTable(N_FLIP, getFlip, setFlip, nullptr, N_MOVES);
	Tab16 mSlice = moveTable(N_SLICE, getSlice, setSlice, nullptr, N_MOVES);
	Tab16 mCP = moveTable(N_PERM8, getCPerm, setCPerm, P2_MOVES, N_MOVES_P2);
	Tab16 mUD = moveTable(N_PERM8, getUDPerm, setUDPerm, P2_MOVES, N_MOVES_P2);
	Tab16 mSP = moveTable(N_SLICE_PERM, getSlicePerm, setSlicePerm, P2_MOVES, N_MOVES_P2);

	printf("twist x slice\n");
	save("twist_slice.bin", buildPrune(mTwist, N_TWIST, mSlice, N_SLICE, N_MOVES));
	printf("flip x slice\n");
	save("flip_slice.bin", buildPrune(mFlip, N_FLIP, mSlice, N_SLICE, N_MOVES));
	printf("cperm x sliceperm\n");
	save("cperm_sp.bin", buildPrune(mCP, N_PERM8, mSP, N_SLICE_PERM, N_MOVES_P2));
	printf("udperm x sliceperm\n");
	save("udperm_sp.bin", buildPrune(mUD, N_PERM8, mSP, N_SLICE_PERM, N_MOVES_P2));
	return 0;
}