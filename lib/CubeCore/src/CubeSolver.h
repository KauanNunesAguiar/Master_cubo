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
	uint32_t nodes() const { return _nodes; }
	int verifyError() const { return _verr; }  // se solve() deu -1: -1 cor/peça inválida, -2..-6 = código do verify()
	int solve(const CubeState& s, char* out, size_t outLen, uint8_t maxDepth = 30, uint32_t timeoutMs = 60000,
	          uint32_t refineMs = 0);  // refineMs > 0: depois da 1ª solução, procura mais curtas por esse tempo
	void setPhase2Budget(uint32_t n) { _p2Budget = n; }  // 0 = sem limite

   private:
	static const uint8_t MAXD = 32;
	PruneTable _ts, _fs, _cs, _us;
	CubieCube _cc[MAXD + 1];  // _cc[d] = cubo depois de d giros
	uint8_t _sol[MAXD];       // giro (0..17) de cada passo
	uint8_t _best[MAXD];
	uint8_t _len, _maxDepth, _p2Max;  // _p2Max = limite de giros da fase 2 no estágio atual
	int8_t _verr;
	uint32_t _nodes, _t0, _timeout;
	uint32_t _p2Used, _p2Budget;
	bool _p2Cut;
	bool _abort;

	bool tick();
	bool search1(uint8_t depth, uint8_t remaining, int8_t lastFace);
	bool phase2(uint8_t depth, int8_t lastFace);
	bool search2(uint8_t depth, uint8_t remaining, int8_t lastFace);
	bool runStages(uint8_t maxDepth, uint8_t stFrom = 0);
};