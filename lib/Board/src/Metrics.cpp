// Metrics.cpp
#include "Metrics.h"

#if METRICS_ENABLED

#include <string.h>

Metrics g_metrics;

static uint32_t mhz() {
	uint32_t m = SystemCoreClock / 1000000UL;
	return m ? m : 1;
}
static unsigned long toUs(uint64_t cyc) { return (unsigned long)(cyc / mhz()); }

void metricsResetSolve() {
	memset(&g_metrics.solver, 0, sizeof(g_metrics.solver));
	MFlash& f = g_metrics.flash;
	memset(f.reads, 0, sizeof(f.reads));
	memset(f.cycles, 0, sizeof(f.cycles));
	f.hits = f.misses = 0;
	for (uint32_t i = 0; i < METRICS_SIM_LINES; i++) f.tag[i] = 0xFFFFFFFFu;
}

void metricsResetRender() {
	memset(&g_metrics.render, 0, sizeof(g_metrics.render));
	g_metrics.render.fpsT0 = millis();
}

void metricsInit() {
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	metricsResetSolve();
	metricsResetRender();
}

void metricsPrintSolve(Print& out) {
	const MSolver& s = g_metrics.solver;
	const MFlash& f = g_metrics.flash;

	unsigned long totUs = (unsigned long)s.totalMs * 1000UL;
	unsigned long p2Us = toUs(s.p2Cycles);
	unsigned long p1Us = totUs > p2Us ? totUs - p2Us : 0;

	uint32_t reads = 0;
	uint64_t cyc = 0;
	for (uint8_t i = 0; i < 4; i++) {
		reads += f.reads[i];
		cyc += f.cycles[i];
	}
	unsigned long flUs = toUs(cyc);
	unsigned long fl1Us = toUs(f.cycles[0] + f.cycles[1]);                                        // tabelas da fase 1
	unsigned long fl2Us = toUs(f.cycles[2] + f.cycles[3]);                                        // tabelas da fase 2
	unsigned long avgNs100 = reads ? (unsigned long)(cyc * 100 / ((uint64_t)mhz() * reads)) : 0;  // centésimos de us
	unsigned long flPct = totUs ? flUs * 100UL / totUs : 0;

	uint64_t tot = (uint64_t)f.hits + f.misses;
	unsigned long hitPm = tot ? (unsigned long)((uint64_t)f.hits * 1000 / tot) : 0;

	out.printf("[M] resultado=%d  len=%u (fase1=%u fase2=%u)  estagio=%u  total=%lu ms\n", (int)s.result,
	           (unsigned)s.solLen, (unsigned)s.sol1, (unsigned)(s.solLen > s.sol1 ? s.solLen - s.sol1 : 0),
	           (unsigned)s.stage, (unsigned long)s.totalMs);
	out.printf("[M] fase1: %lu ms (flash %lu ms)   fase2: %lu ms (flash %lu ms)\n", p1Us / 1000, fl1Us / 1000,
	           p2Us / 1000, fl2Us / 1000);
	out.printf("[M] nos: f1=%lu f2=%lu   G1 alcancado=%lu x   limites de f2 testados=%lu\n", (unsigned long)s.nodes1,
	           (unsigned long)s.nodes2, (unsigned long)s.p2Calls, (unsigned long)s.p2Iters);
	out.printf(
	    "[M] flash: %lu leituras (ts=%lu fs=%lu cs=%lu us=%lu)  media %lu.%02lu us  total %lu ms = %lu%% do tempo\n",
	    (unsigned long)reads, (unsigned long)f.reads[0], (unsigned long)f.reads[1], (unsigned long)f.reads[2],
	    (unsigned long)f.reads[3], avgNs100 / 100, avgNs100 % 100, flUs / 1000, flPct);
	out.printf("[M] cache simulado %ux%uB: acerto %lu.%lu%% (%lu acertos / %lu faltas)\n", (unsigned)METRICS_SIM_LINES,
	           (unsigned)(1u << METRICS_SIM_SHIFT), hitPm / 10, hitPm % 10, (unsigned long)f.hits,
	           (unsigned long)f.misses);
}

void metricsPrintRender(Print& out) {
	const MRender& r = g_metrics.render;
	uint64_t fr = r.frames ? r.frames : 1;
	uint64_t lk = r.locks ? r.locks : 1;

	unsigned long rAvg = toUs(r.renderCycles / fr), rMax = toUs(r.renderMax);
	unsigned long pAvg = toUs(r.pushCycles / fr), pMax = toUs(r.pushMax);
	unsigned long wAvg = toUs(r.waitCycles / lk), wMax = toUs(r.waitMax);
	unsigned long hAvg = toUs(r.heldCycles / lk), hMax = toUs(r.heldMax);

	out.printf("[M] render: %lu quadros, %lu fps (ult. janela de 1 s)\n", (unsigned long)r.frames,
	           (unsigned long)r.fps);
	out.printf(
	    "[M] quadro: total medio %lu.%02lu ms max %lu.%02lu ms | pushColors medio %lu.%02lu ms max %lu.%02lu ms\n",
	    rAvg / 1000, (rAvg % 1000) / 10, rMax / 1000, (rMax % 1000) / 10, pAvg / 1000, (pAvg % 1000) / 10, pMax / 1000,
	    (pMax % 1000) / 10);
	out.printf(
	    "[M] mutex (taskRender): espera media %lu.%02lu ms max %lu.%02lu ms | segurado medio %lu.%02lu ms max "
	    "%lu.%02lu ms\n",
	    wAvg / 1000, (wAvg % 1000) / 10, wMax / 1000, (wMax % 1000) / 10, hAvg / 1000, (hAvg % 1000) / 10, hMax / 1000,
	    (hMax % 1000) / 10);
}

#endif