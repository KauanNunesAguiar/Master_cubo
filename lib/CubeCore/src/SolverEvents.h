// SolverEvents.h — gancho de eventos do solver (Etapa 6). Desligado = código vazio.
#pragma once
#include <stdint.h>

#ifndef SOLVER_EVENTS
#define SOLVER_EVENTS 0
#endif

enum SolverEvt : uint8_t { EV_P1_NODE, EV_P1_G1, EV_P2_NODE, EV_SOLUTION };

#if SOLVER_EVENTS
void solverEvent(uint8_t type, uint8_t depth, uint8_t move);  // implementar na Etapa 6
#define S_EVT(t, d, m) solverEvent((t), (d), (m))
#else
#define S_EVT(t, d, m) ((void)0)
#endif