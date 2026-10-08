// CubeSolver.cpp
#include "CubeSolver.h"

#include <Arduino.h>

CubeSolver::CubeSolver(W25Q16& flash)
    : _ts{&flash, PRUNE_ADDR_TWIST_SLICE},
      _fs{&flash, PRUNE_ADDR_FLIP_SLICE},
      _cs{&flash, PRUNE_ADDR_CPERM_SP},
      _us{&flash, PRUNE_ADDR_UDPERM_SP},
      _len(0),
      _maxDepth(30),
      _p2Max(30),
      _nodes(0),
      _t0(0),
      _timeout(0),
      _abort(false) {}

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
	const CubieCube& c = _cc[depth];
	uint16_t tw = getTwist(c), fl = getFlip(c), sl = getSlice(c);

	if (remaining == 0) {
		if ((tw | fl | sl) != 0) return false;
		if (depth) {
			// Se o último giro da fase 1 já é um giro da fase 2 (U, D ou meia-volta),
			// o cubo já estava em G1 um passo antes: essa solução já foi tentada com menos giros.
			uint8_t last = _sol[depth - 1];
			if (last / 3 == FACE_U || last / 3 == FACE_D || last % 3 == 1) return false;
		}
		return phase2(depth, lastFace);
	}
	if (_ts.get((uint32_t)tw * N_SLICE + sl) > remaining) return false;
	if (_fs.get((uint32_t)fl * N_SLICE + sl) > remaining) return false;

	for (uint8_t m = 0; m < N_MOVES; m++) {
		int8_t face = m / 3;
		if (!allowed(face, lastFace)) continue;
		_cc[depth + 1] = c;
		_cc[depth + 1].multiply(moveCubie(m));
		_sol[depth] = m;
		if (search1(depth + 1, remaining - 1, face)) return true;
		if (_abort) return false;
	}
	return false;
}

bool CubeSolver::phase2(uint8_t depth, int8_t lastFace) {
	const CubieCube& c = _cc[depth];
	uint32_t sp = getSlicePerm(c);
	uint8_t h1 = _cs.get((uint32_t)getCPerm(c) * N_SLICE_PERM + sp);
	uint8_t h2 = _us.get((uint32_t)getUDPerm(c) * N_SLICE_PERM + sp);
	uint8_t h = h1 > h2 ? h1 : h2;
	for (uint8_t lim = h; lim <= _p2Max && depth + lim <= _maxDepth; lim++) {
		if (search2(depth, lim, lastFace)) return true;
		if (_abort) return false;
	}
	return false;
}

bool CubeSolver::search2(uint8_t depth, uint8_t remaining, int8_t lastFace) {
	if (tick()) return false;
	const CubieCube& c = _cc[depth];
	uint32_t sp = getSlicePerm(c);
	uint8_t h1 = _cs.get((uint32_t)getCPerm(c) * N_SLICE_PERM + sp);
	uint8_t h2 = _us.get((uint32_t)getUDPerm(c) * N_SLICE_PERM + sp);
	uint8_t h = h1 > h2 ? h1 : h2;

	if (h == 0) {  // as duas tabelas em 0 => cubo resolvido
		_len = depth;
		return true;
	}
	if (h > remaining) return false;

	for (uint8_t k = 0; k < N_MOVES_P2; k++) {
		uint8_t m = P2_MOVES[k];
		int8_t face = m / 3;
		if (!allowed(face, lastFace)) continue;
		_cc[depth + 1] = c;
		_cc[depth + 1].multiply(moveCubie(m));
		_sol[depth] = m;
		if (search2(depth + 1, remaining - 1, face)) return true;
		if (_abort) return false;
	}
	return false;
}

int CubeSolver::solve(const CubeState& s, char* out, size_t outLen, uint8_t maxDepth, uint32_t timeoutMs) {
	if (outLen == 0) return -4;
	if (!_cc[0].fromFacelets(s) || _cc[0].verify() != 0) return -1;

	_maxDepth = maxDepth > 30 ? 30 : maxDepth;
	_nodes = 0;
	_abort = false;
	_t0 = millis();
	_timeout = timeoutMs;

	/* Escalonado: primeiro muitas soluções de fase 1 com fase 2 curta (barata);
	 * só relaxa o limite da fase 2 se nada for encontrado. */
	static const uint8_t CAP[4] = {10, 12, 14, 18};
	bool found = false;
	for (uint8_t st = 0; st < 4 && !found; st++) {
		_p2Max = CAP[st];
		uint8_t lim1 = (st < 3 && _maxDepth > 12) ? 12 : _maxDepth;
		for (uint8_t d1 = 0; d1 <= lim1 && !found; d1++) {
			found = search1(0, d1, -1);
			if (_abort) return -2;
		}
	}
	if (!found) return -3;

	size_t p = 0;
	for (uint8_t i = 0; i < _len; i++) {
		if (p + 4 > outLen) return -4;
		out[p++] = "URFDLB"[_sol[i] / 3];
		uint8_t t = _sol[i] % 3;  // 0 = horário, 1 = 180°, 2 = anti-horário
		if (t == 1)
			out[p++] = '2';
		else if (t == 2)
			out[p++] = '\'';
		out[p++] = ' ';
	}
	if (p) p--;  // tira o espaço final
	out[p] = '\0';
	return _len;
}