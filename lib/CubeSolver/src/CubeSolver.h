// CubeSolver.h — Kociemba de duas fases (primeira solução encontrada, não a ótima)
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "Coords.h"
#include "CubeState.h"
#include "PruneTable.h"

class CubeSolver {
   public:
	explicit CubeSolver(W25Q16& flash);
	void begin();  // gera os giros básicos (chamar uma vez)

	/* Retorna o número de giros (>= 0) e escreve a solução em out ("R U2 F' ...").
	 * Erros: -1 cubo inválido, -2 tempo esgotado, -3 sem solução até maxDepth, -4 buffer pequeno. */
	int solve(const CubeState& s, char* out, size_t outLen, uint8_t maxDepth = 30, uint32_t timeoutMs = 60000);
	uint32_t nodes() const { return _nodes; }

   private:
	static const uint8_t MAXD = 32;
	PruneTable _ts, _fs, _cs, _us;
	CubieCube _cc[MAXD + 1];  // _cc[d] = cubo depois de d giros
	uint8_t _sol[MAXD];       // giro (0..17) de cada passo
	uint8_t _len, _maxDepth;
	uint32_t _nodes, _t0, _timeout;
	bool _abort;

	bool tick();
	bool search1(uint8_t depth, uint8_t remaining, int8_t lastFace);
	bool phase2(uint8_t depth, int8_t lastFace);
	bool search2(uint8_t depth, uint8_t remaining, int8_t lastFace);
};