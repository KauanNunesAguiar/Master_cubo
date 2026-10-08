// Metrics.h — contadores de desempenho para medir antes de otimizar.
// Com METRICS_ENABLED=0 tudo vira código vazio (custo zero).
//
// Para desligar:  build_flags = -O2 -DMETRICS_ENABLED=0
// Para testar outro tamanho de cache simulado: -DMETRICS_SIM_LINES=256 -DMETRICS_SIM_SHIFT=6
#pragma once
#include <stdint.h>

#ifndef METRICS_ENABLED
#define METRICS_ENABLED 1
#endif
#ifndef METRICS_SIM_SHIFT
#define METRICS_SIM_SHIFT 5  // linha do cache simulado = 2^5 = 32 bytes (64 entradas de poda)
#endif
#ifndef METRICS_SIM_LINES
#define METRICS_SIM_LINES 128  // número de linhas (potência de 2). 128 x 32 B = 4 KB de cache "virtual"
#endif

#if METRICS_ENABLED
#include <Arduino.h>

/* --- Flash (PruneTable::get). Tabelas: 0 twist*slice, 1 flip*slice, 2 cperm*sp, 3 udperm*sp --- */
struct MFlash {
	uint32_t reads[4];
	uint64_t cycles[4];
	uint32_t hits, misses;  // cache simulado (direto-mapeado), só estatística: não altera a busca
	uint32_t tag[METRICS_SIM_LINES];
};

/* --- Solver (zerado a cada solve) --- */
struct MSolver {
	uint32_t nodes1, nodes2;  // nós visitados em cada fase
	uint32_t p2Calls;         // quantas vezes a fase 1 chegou em G1 e chamou a fase 2
	uint32_t p2Iters;         // quantos limites de profundidade a fase 2 tentou (soma de todas as chamadas)
	uint32_t totalMs;
	uint64_t p2Cycles;  // tempo dentro de phase2() (inclui a flash)
	uint8_t sol1, solLen, stage;
	int8_t result;
};

/* --- Render / mutex (zerado por metricsResetRender) --- */
struct MRender {
	uint32_t frames, fps, fpsFrames, fpsT0;
	uint32_t renderMax, pushMax;
	uint64_t renderCycles, pushCycles;
};

struct Metrics {
	MFlash flash;
	MSolver solver;
	MRender render;
};
extern Metrics g_metrics;

void metricsInit();  // liga o contador de ciclos (DWT) e zera tudo. Chamar no início do setup().
void metricsResetSolve();
void metricsResetRender();
void metricsPrintSolve(Print& out);
void metricsPrintRender(Print& out);

static inline uint32_t metricsCycles() { return DWT->CYCCNT; }

static inline void metricsFlashRead(uint8_t id, uint32_t addr, uint32_t cyc) {
	MFlash& f = g_metrics.flash;
	f.reads[id & 3]++;
	f.cycles[id & 3] += cyc;
	uint32_t line = addr >> METRICS_SIM_SHIFT;
	uint32_t& tag = f.tag[line % METRICS_SIM_LINES];
	if (tag == line) {
		f.hits++;
	} else {
		f.misses++;
		tag = line;
	}
}

static inline void metricsRender(uint32_t renderCyc, uint32_t pushCyc) {
	MRender& r = g_metrics.render;
	r.frames++;
	r.renderCycles += renderCyc;
	r.pushCycles += pushCyc;
	if (renderCyc > r.renderMax) r.renderMax = renderCyc;
	if (pushCyc > r.pushMax) r.pushMax = pushCyc;
	uint32_t now = millis();
	r.fpsFrames++;
	if (now - r.fpsT0 >= 1000) {
		r.fps = r.fpsFrames;
		r.fpsFrames = 0;
		r.fpsT0 = now;
	}
}

#define metricsLock(w, h) ((void)0)

/* Atalhos usados no código */
#define M_TIC(v) uint32_t v = metricsCycles()
#define M_TOC(v) (metricsCycles() - (v))
#define M_INC(f) (g_metrics.f++)
#define M_ADD(f, n) (g_metrics.f += (n))
#define M_SET(f, n) (g_metrics.f = (n))

#else  // METRICS_ENABLED == 0: tudo vazio
class Print;

#define M_TIC(v)
#define M_TOC(v) 0u
#define M_INC(f) ((void)0)
#define M_ADD(f, n) ((void)0)
#define M_SET(f, n) ((void)0)
#define metricsFlashRead(id, addr, cyc) ((void)0)
#define metricsRender(rc, pc) ((void)0)
#define metricsLock(w, h) ((void)0)
static inline void metricsInit() {}
static inline void metricsResetSolve() {}
static inline void metricsResetRender() {}
static inline void metricsPrintSolve(Print&) {}
static inline void metricsPrintRender(Print&) {}

#endif