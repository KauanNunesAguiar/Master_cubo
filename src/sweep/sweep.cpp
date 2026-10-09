// sweep.cpp — varre _p2Budget automaticamente e imprime só estatísticas por budget
#include <Arduino.h>
#include <stdlib.h>

#include "Coords.h"
#include "CubeSolver.h"
#include "CubeState.h"
#include "W25Q16.h"

/* ---------------- parâmetros (entrada) ---------------- */
static const uint16_t N_CUBES = 200;  // cubos por budget (máx. 300 pela RAM do array de tempos)
static const uint32_t SEED = 2024;    // mesma sequência de cubos para todos os budgets
static const uint8_t SCRAMBLE = 30;
static const uint8_t MAX_DEPTH = 26;
static const uint32_t TIMEOUT_MS = 60000;
static const uint32_t REFINE_MS = 1500;
// static const uint32_t START_LIST[] = {0, 300000, 150000, 80000, 50000, 30000, 20000, 10000};  // 0 = sem limite
static const uint32_t START_LIST[] = {30000};
static const uint8_t MAX_EXTRA = 20;  // máx. de budgets extras no refinamento automático
static const uint32_t MIN_BUDGET = 5000;

struct Res {
	uint32_t budget;
	uint16_t ok, fail;
	uint32_t mean, med, p90, mx;  // ms
	uint16_t over10, over7;
	uint32_t len100;  // média de giros x100
	uint32_t wallS;
};

W25Q16 flash;
CubeSolver solver(flash);

static Res g_res[64];
static uint8_t g_nres = 0;
static uint32_t g_t[N_CUBES];
static uint32_t g_rng;

static uint32_t rnd() {  // xorshift32
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return g_rng;
}

static bool tested(uint32_t b) {
	for (uint8_t i = 0; i < g_nres; i++)
		if (g_res[i].budget == b) return true;
	return false;
}

static void printHeader() {
	Serial.printf("# SWEEP N=%u seed=%lu scramble=%u maxDepth=%u timeout=%lums refine=%lums\n", (unsigned)N_CUBES,
	              (unsigned long)SEED, (unsigned)SCRAMBLE, (unsigned)MAX_DEPTH, (unsigned long)TIMEOUT_MS,
	              (unsigned long)REFINE_MS);
	Serial.println("# budget;ok;fail;media_ms;mediana_ms;p90_ms;max_ms;>10s;>7s;giros_medio;tempo_total_s");
}

static void printRes(const Res& r) {
	Serial.printf(
	    "budget=%lu;ok=%u;fail=%u;media=%lu;mediana=%lu;p90=%lu;max=%lu;>10s=%u;>7s=%u;giros=%lu.%02lu;t=%lus\n",
	    (unsigned long)r.budget, (unsigned)r.ok, (unsigned)r.fail, (unsigned long)r.mean, (unsigned long)r.med,
	    (unsigned long)r.p90, (unsigned long)r.mx, (unsigned)r.over10, (unsigned)r.over7,
	    (unsigned long)(r.len100 / 100), (unsigned long)(r.len100 % 100), (unsigned long)r.wallS);
}

static Res runBudget(uint32_t budget) {
	solver.setPhase2Budget(budget);
	g_rng = SEED;
	Res r = {};
	r.budget = budget;
	uint64_t sumT = 0, sumLen = 0;
	uint32_t wall0 = millis();

	for (uint16_t n = 0; n < N_CUBES; n++) {
		CubeState s;
		int last = -1;
		for (uint8_t i = 0; i < SCRAMBLE; i++) {
			int f;
			do { f = rnd() % 6; } while (f == last);
			last = f;
			s.applyMove(f, 1 + rnd() % 3);
		}
		char sol[128];
		uint32_t t0 = millis();
		int len = solver.solve(s, sol, sizeof(sol), MAX_DEPTH, TIMEOUT_MS, REFINE_MS);
		uint32_t dt = millis() - t0;

		bool ok = false;
		if (len >= 0) {
			s.applyMoves(sol);
			ok = s.isSolved();
		}
		if (ok) {
			r.ok++;
			sumLen += len;
		} else {
			r.fail++;
		}
		g_t[n] = dt;
		sumT += dt;
		if (dt > 10000) r.over10++;
		if (dt > 7000) r.over7++;
	}

	for (uint16_t i = 1; i < N_CUBES; i++) {  // insertion sort
		uint32_t k = g_t[i];
		int j = (int)i - 1;
		while (j >= 0 && g_t[j] > k) {
			g_t[j + 1] = g_t[j];
			j--;
		}
		g_t[j + 1] = k;
	}
	r.mean = (uint32_t)(sumT / N_CUBES);
	r.med = g_t[N_CUBES / 2];
	r.p90 = g_t[N_CUBES * 9 / 10];
	r.mx = g_t[N_CUBES - 1];
	r.len100 = r.ok ? (uint32_t)(sumLen * 100 / r.ok) : 0;
	r.wallS = (millis() - wall0) / 1000;
	return r;
}

static void run(uint32_t b) {
	if (g_nres >= 64 || tested(b)) return;
	g_res[g_nres] = runBudget(b);
	printRes(g_res[g_nres]);
	g_nres++;
}

static uint8_t bestIdx() {  // menor média, só entre os que resolveram tudo
	uint8_t best = 0;
	bool found = false;
	for (uint8_t i = 0; i < g_nres; i++) {
		if (g_res[i].fail) continue;
		if (!found || g_res[i].mean < g_res[best].mean) {
			best = i;
			found = true;
		}
	}
	return best;
}

static void printRanking() {
	printHeader();
	bool used[64] = {false};
	Serial.println("# RANKING por media");
	for (uint8_t k = 0; k < g_nres; k++) {
		int bi = -1;
		for (uint8_t i = 0; i < g_nres; i++)
			if (!used[i] && (bi < 0 || g_res[i].mean < g_res[bi].mean)) bi = i;
		used[bi] = true;
		printRes(g_res[bi]);
	}
	Serial.printf("# MELHOR: budget=%lu\n", (unsigned long)g_res[bestIdx()].budget);
}

void setup() {
	Serial.begin(115200);
	delay(2000);
	CubeState::init();
	solver.begin();
	if (!flash.begin()) {
		Serial.println("ERRO: W25Q16 nao respondeu");
		for (;;) delay(1000);
	}
	printHeader();

	// fase A: lista fixa
	for (uint32_t b : START_LIST) run(b);

	// fase B: refinamento automático em volta do melhor (±25%)
	for (uint8_t e = 0; e < MAX_EXTRA; e++) {
		uint32_t b = g_res[bestIdx()].budget;
		if (b == 0) break;
		uint32_t lo = (b * 3 / 4) / 1000 * 1000, hi = (b * 5 / 4) / 1000 * 1000;
		bool didSomething = false;
		if (lo >= MIN_BUDGET && !tested(lo)) {
			run(lo);
			didSomething = true;
		}
		if (!tested(hi)) {
			run(hi);
			didSomething = true;
		}
		if (!didSomething) break;
	}
	Serial.println("# FIM");
}

void loop() {  // termina: reimprime o ranking a cada 60 s (útil se abrir o terminal depois)
	printRanking();
	delay(60000);
}