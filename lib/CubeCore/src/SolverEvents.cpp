// SolverEvents.cpp
#include "SolverEvents.h"

#if SOLVER_EVENTS
#include <Arduino.h>
#include <string.h>

SolverTrace g_trace;

void solverTraceReset() {
	SolverTrace& t = g_trace;
	memset(t.nodes, 0, sizeof(t.nodes));
	memset(t.pruned, 0, sizeof(t.pruned));
	memset(t.path, 0, sizeof(t.path));
	t.g1 = 0;
	t.iterBase = t.lastIterNodes = 0;
	t.lastIterDepth = 0;
	t.curDepth = t.curPhase = t.solLen = 0;
	t.solved = false;
	t.t0 = millis();
	t.t1 = 0;
	t.seq = t.seq + 1;
	t.running = true;
}

void solverTraceEnd() {
	g_trace.t1 = millis();
	g_trace.running = false;
}

void solverEvent(uint8_t type, uint8_t depth, uint8_t move) {
	SolverTrace& t = g_trace;
	if (depth > 31) depth = 31;
	switch (type) {
		case EV_P1_NODE:
			t.nodes[0][depth]++;
			if (move != 255) t.path[depth] = move;
			t.curDepth = depth;
			t.curPhase = 0;
			break;
		case EV_P2_NODE:
			t.nodes[1][depth]++;
			if (move != 255) t.path[depth] = move;
			t.curDepth = depth;
			t.curPhase = 1;
			break;
		case EV_P1_PRUNE:
			t.pruned[0][depth]++;
			break;
		case EV_P2_PRUNE:
			t.pruned[1][depth]++;
			break;
		case EV_P1_G1:
			t.g1++;
			break;
		case EV_SOLUTION:
			t.solved = true;
			t.solLen = depth;
			break;
		case EV_P1_ITER: {
			uint32_t tot = 0;
			for (uint8_t i = 0; i < 32; i++) tot += t.nodes[0][i];
			t.lastIterNodes = tot - t.iterBase;
			t.lastIterDepth = depth;
			t.iterBase = tot;
			break;
		}
	}
	t.ring[t.head & (TRACE_RING - 1)] = (uint16_t)((type & 7) | ((depth & 31) << 3) | ((uint16_t)move << 8));
	t.head++;
}

void solverTracePrintCsv(Print& out) {
	const SolverTrace& t = g_trace;
	uint8_t last = 0;
	for (uint8_t d = 0; d < 32; d++)
		if (t.nodes[0][d] || t.pruned[0][d] || t.nodes[1][d] || t.pruned[1][d]) last = d;
	out.printf("[CSV] busca=%u resolvido=%u giros=%u ms=%lu g1=%lu\n", (unsigned)t.seq, (unsigned)t.solved,
	           (unsigned)t.solLen, (unsigned long)((t.running ? millis() : t.t1) - t.t0), (unsigned long)t.g1);
	out.println("[CSV] prof,f1_nos,f1_poda,f2_nos,f2_poda");
	for (uint8_t d = 0; d <= last; d++)
		out.printf("[CSV] %u,%lu,%lu,%lu,%lu\n", (unsigned)d, (unsigned long)t.nodes[0][d],
		           (unsigned long)t.pruned[0][d], (unsigned long)t.nodes[1][d], (unsigned long)t.pruned[1][d]);
}
#endif