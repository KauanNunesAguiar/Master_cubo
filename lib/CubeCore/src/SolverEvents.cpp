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
	}
	t.ring[t.head & (TRACE_RING - 1)] = (uint16_t)((type & 7) | ((depth & 31) << 3) | ((uint16_t)move << 8));
	t.head++;
}
#endif