// SolverEvents.h — eventos do solver para a visualização da árvore (Etapa 6).
// SOLVER_EVENTS=0 => tudo vira código vazio. Ligue com -D SOLVER_EVENTS=1.
#pragma once
#include <stdint.h>

#ifndef SOLVER_EVENTS
#define SOLVER_EVENTS 0
#endif

enum SolverEvt : uint8_t { EV_P1_NODE, EV_P1_G1, EV_P2_NODE, EV_SOLUTION, EV_P1_PRUNE, EV_P2_PRUNE };

#if SOLVER_EVENTS
static const uint8_t TRACE_RING = 64;  // potência de 2
struct SolverTrace {
	uint32_t nodes[2][32];      // [fase-1][profundidade]: nós visitados
	uint32_t pruned[2][32];     // nós cortados pela tabela de poda
	uint32_t g1;                // vezes que a fase 1 chegou em G1
	uint32_t head;              // total de eventos emitidos
	uint32_t t0, t1;            // início / fim da busca (ms)
	uint16_t ring[TRACE_RING];  // últimos eventos: tipo(3) | prof(5) | giro(8)
	uint8_t path[32];           // path[d] = giro (0..17) que levou ao nó de profundidade d
	uint8_t curDepth, curPhase, solLen;
	volatile uint8_t seq;  // +1 a cada busca nova
	volatile bool running, solved;
};
extern SolverTrace g_trace;
void solverEvent(uint8_t type, uint8_t depth, uint8_t move);
void solverTraceReset();
void solverTraceEnd();
#define S_EVT(t, d, m) solverEvent((t), (d), (m))
#define S_RESET() solverTraceReset()
#define S_END() solverTraceEnd()
#else
#define S_EVT(t, d, m) ((void)0)
#define S_RESET() ((void)0)
#define S_END() ((void)0)
#endif