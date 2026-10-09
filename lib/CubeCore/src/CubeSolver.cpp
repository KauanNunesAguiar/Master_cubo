// CubeSolver.cpp
#include "CubeSolver.h"

#include <Arduino.h>
#include <string.h>

#include <new>

#include "Metrics.h"
#include "SolverEvents.h"

CubeSolver::CubeSolver(W25Q16& flash)
    : _ts{&flash, PRUNE_ADDR_TWIST_SLICE, 0},
      _fs{&flash, PRUNE_ADDR_FLIP_SLICE, 1},
      _cs{&flash, PRUNE_ADDR_CPERM_SP, 2},
      _us{&flash, PRUNE_ADDR_UDPERM_SP, 3},
      _len(0),
      _maxDepth(30),
      _p2Max(30),
      _verr(0),
      _nodes(0),
      _t0(0),
      _timeout(0),
      _p2Used(0),
      _p2Budget(30000),
      _p2Cut(false),
      _abort(false) {
	static_assert((MAXD + 1) * sizeof(CubieCube) <= 0x800, "_cc invade g_m18");
	_cc = (CubieCube*)CCM_RAM_BASE;
	for (uint8_t i = 0; i <= MAXD; i++) new (&_cc[i]) CubieCube();
}

void CubeSolver::begin() { coordsInit(); }

// Faces 0..5 = U R F D L B; faces opostas têm o mesmo (face % 3)
static inline bool allowed(int8_t face, int8_t last) {
	if (face == last) return false;
	if (last >= 0 && face % 3 == last % 3 && face < last) return false;  // só permite U->D, R->L, F->B
	return true;
}

bool CubeSolver::tick() {
	if ((++_nodes & 0xFF) == 0 && millis() - _t0 > _timeout) _abort = true;
	return _abort;
}

bool CubeSolver::search1(uint8_t depth, uint8_t remaining, int8_t lastFace) {
	if (tick()) return false;
	M_INC(solver.nodes1);
	S_EVT(EV_P1_NODE, depth, depth ? _sol[depth - 1] : 255);
	const CubieCube& c = _cc[depth];

	if (remaining == 0) {
		if (c.ep[8] < 8 || c.ep[9] < 8 || c.ep[10] < 8 || c.ep[11] < 8) return false;
		for (uint8_t i = 0; i < 7; i++)
			if (c.co[i]) return false;
		for (uint8_t i = 0; i < 11; i++)
			if (c.eo[i]) return false;
		if (depth) {
			uint8_t last = _sol[depth - 1];
			if (last / 3 == FACE_U || last / 3 == FACE_D || last % 3 == 1) return false;
		}
		for (uint8_t i = 0; i < depth; i++) {  // a fase 1 não mantém cp: refaz a cadeia completa
			_cc[i + 1] = _cc[i];
			_cc[i + 1].multiply(moveCubie(_sol[i]));
		}
		return phase2(depth, lastFace);
	}

	uint16_t tw = getTwist(c), sl = getSlice(c);
	if (_ts.get((uint32_t)tw * N_SLICE + sl) > remaining) {
		S_EVT(EV_P1_PRUNE, depth, 255);
		return false;
	}
	if (_fs.get((uint32_t)getFlip(c) * N_SLICE + sl) > remaining) {
		S_EVT(EV_P1_PRUNE, depth, 255);
		return false;
	}

	for (uint8_t m = 0; m < N_MOVES; m++) {
		int8_t face = m / 3;
		if (!allowed(face, lastFace)) continue;
		_cc[depth + 1].multiplyP1(c, moveCubie(m));
		_sol[depth] = m;
		if (search1(depth + 1, remaining - 1, face)) return true;
		if (_abort) return false;
	}
	return false;
}

bool CubeSolver::phase2(uint8_t depth, int8_t lastFace) {
	_p2Used = 0;
	_p2Cut = false;

	M_INC(solver.p2Calls);
	S_EVT(EV_P1_G1, depth, 255);
	M_SET(solver.sol1, depth);  // se esta chamada resolver, depth = giros da fase 1

	const CubieCube& c = _cc[depth];
	uint32_t sp = getSlicePerm(c);
	uint8_t h1 = _cs.get((uint32_t)getCPerm(c) * N_SLICE_PERM + sp);
	uint8_t h2 = _us.get((uint32_t)getUDPerm(c) * N_SLICE_PERM + sp);
	uint8_t h = h1 > h2 ? h1 : h2;

	bool ok = false;
	for (uint8_t lim = h; lim <= _p2Max && depth + lim <= _maxDepth; lim++) {
		M_INC(solver.p2Iters);
		M_TIC(ti);
		bool r = search2(depth, lim, lastFace);
		M_ADD(solver.p2Cycles, M_TOC(ti));
		if (r) {
			ok = true;
			break;
		}
		if (_abort) break;
		if (_p2Cut) break;
	}

	if (_p2Cut) M_INC(solver.p2Cuts);
	return ok;
}

bool CubeSolver::search2(uint8_t depth, uint8_t remaining, int8_t lastFace) {
	if (tick()) return false;
	if (_p2Budget && ++_p2Used > _p2Budget) {
		_p2Cut = true;
		return false;
	}

	M_INC(solver.nodes2);
	S_EVT(EV_P2_NODE, depth, depth ? _sol[depth - 1] : 255);

	const CubieCube& c = _cc[depth];
	uint32_t sp = getSlicePerm(c);
	uint8_t h1 = _cs.get((uint32_t)getCPerm(c) * N_SLICE_PERM + sp);

	if (h1 > remaining) {
		S_EVT(EV_P2_PRUNE, depth, 255);
		return false;
	}

	uint8_t h2 = _us.get((uint32_t)getUDPerm(c) * N_SLICE_PERM + sp);
	uint8_t h = h1 > h2 ? h1 : h2;

	if (h == 0) {  // as duas tabelas em 0 => cubo resolvido
		_len = depth;
		S_EVT(EV_SOLUTION, depth, 255);
		return true;
	}

	if (h > remaining) {
		S_EVT(EV_P2_PRUNE, depth, 255);
		return false;
	}

	for (uint8_t k = 0; k < N_MOVES_P2; k++) {
		uint8_t m = P2_MOVES[k];
		int8_t face = m / 3;
		if (!allowed(face, lastFace)) continue;
		_cc[depth + 1].multiplyP2(c, moveCubie(m));
		_sol[depth] = m;
		if (search2(depth + 1, remaining - 1, face)) return true;
		if (_abort || _p2Cut) return false;
	}
	return false;
}
// Procura a primeira solução com no máximo maxDepth giros (escalonada, como antes).
bool CubeSolver::runStages(uint8_t maxDepth, uint8_t stFrom) {
	static const uint8_t CAP[4] = {10, 12, 14, 18};
	_maxDepth = maxDepth;
	for (uint8_t st = stFrom; st < 4; st++) {
		_p2Max = CAP[st];
		M_SET(solver.stage, st);
		uint8_t lim1 = (st < 3 && _maxDepth > 12) ? 12 : _maxDepth;
		for (uint8_t d1 = 0; d1 <= lim1; d1++) {
			if (search1(0, d1, -1)) return true;
			if (_abort) return false;
			S_EVT(EV_P1_ITER, d1, 255);
		}
	}
	return false;
}

int CubeSolver::solve(const CubeState& s, char* out, size_t outLen, uint8_t maxDepth, uint32_t timeoutMs,
                      uint32_t refineMs) {
	if (outLen == 0) return -4;

	_verr = 0;
	if (!_cc[0].fromFacelets(s)) {
		_verr = -1;
		return -1;
	}
	int v = _cc[0].verify();
	if (v != 0) {
		_verr = (int8_t)v;
		return -1;
	}

	if (maxDepth > 30) maxDepth = 30;
	_nodes = 0;
	_abort = false;
	const uint32_t tStart = millis();
	_t0 = tStart;
	_timeout = timeoutMs;
	metricsResetSolve();
	S_RESET();

	auto done = [&](int rc) -> int {
		S_END();
		M_SET(solver.totalMs, millis() - tStart);
		M_SET(solver.result, rc);
		M_SET(solver.solLen, rc >= 0 ? rc : 0);
		return rc;
	};

	// 1) primeira solução
	if (!runStages(maxDepth, refineMs ? 2 : 0)) return done(_abort ? -2 : -3);
	uint8_t bestLen = _len;
	memcpy(_best, _sol, bestLen);
#if METRICS_ENABLED
	Serial.printf("[M] 1a solucao: %u giros em %lu ms\n", (unsigned)bestLen, (unsigned long)(millis() - tStart));
#endif

	// 2) refinamento (opcional)
	if (refineMs) {
		_t0 = millis();
		_timeout = refineMs;
		_abort = false;
		while (bestLen > 1 && runStages(bestLen - 1, 0)) {
			bestLen = _len;
			memcpy(_best, _sol, bestLen);
		}
	}

	size_t p = 0;
	for (uint8_t i = 0; i < bestLen; i++) {
		if (p + 4 > outLen) return done(-4);
		out[p++] = "URFDLB"[_best[i] / 3];
		uint8_t t = _best[i] % 3;
		if (t == 1)
			out[p++] = '2';
		else if (t == 2)
			out[p++] = '\'';
		out[p++] = ' ';
	}
	if (p) p--;
	out[p] = '\0';
	return done(bestLen);
}