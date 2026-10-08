// DebugScreen.h — monitor em tempo real (Etapa 5): memória, pilhas, solver, flash, render, entrada.
// Atualiza a cada 500 ms e só redesenha as linhas que mudaram. Texto size 1: 52 colunas.
#pragma once
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "InputManager.h"
#include "Metrics.h"
#include "Screen.h"
#include "TFT_FSMC.h"

class DebugScreen : public Screen {
   public:
	DebugScreen(TFT_FSMC& tft, InputManager& input) : _tft(tft), _in(input) { memset(_prev, 0, sizeof(_prev)); }

	// h = ponteiro para a variável do handle (preenchida depois do xTaskCreate)
	void addTask(const char* name, TaskHandle_t* h, uint16_t words) {
		if (_nt < MAXT) _t[_nt++] = {name, h, words};
	}

	void onEnter() override {
		_tft.fillScreen(TFT_BLACK);
		memset(_prev, 0, sizeof(_prev));
		_last = 0;
	}

	void update(uint32_t now) override {
		if (_last && now - _last < 500) return;
		_last = now | 1;

		uint8_t r = 0;

		line(r++, TFT_CYAN, "DEBUG  uptime %lus  (K0 longo = voltar)", (unsigned long)(now / 1000));
		uint32_t heapFree = (uint32_t)xPortGetFreeHeapSize();
		if (heapFree < _minHeap) _minHeap = heapFree;
		line(r++, TFT_WHITE, "FreeRTOS heap: %lu livre / %lu  (min %lu)", (unsigned long)heapFree,
		     (unsigned long)configTOTAL_HEAP_SIZE, (unsigned long)_minHeap);
		line(r++, TFT_WHITE, "malloc livre (aprox): %lu B", (unsigned long)mallocFree());

#if METRICS_ENABLED
		const MRender& rd = g_metrics.render;
		const MSolver& s = g_metrics.solver;
		const MFlash& f = g_metrics.flash;

		uint64_t fr = rd.frames ? rd.frames : 1;
		uint32_t rAvg = cyc2us(rd.renderCycles / fr), rMax = cyc2us(rd.renderMax), pAvg = cyc2us(rd.pushCycles / fr);
		line(r++, TFT_YELLOW, "HUD %lu fps  quadro %lu.%02lu/%lu.%02lu ms  push %lu.%02lu", (unsigned long)rd.fps,
		     (unsigned long)(rAvg / 1000), (unsigned long)((rAvg % 1000) / 10), (unsigned long)(rMax / 1000),
		     (unsigned long)((rMax % 1000) / 10), (unsigned long)(pAvg / 1000), (unsigned long)((pAvg % 1000) / 10));

		uint32_t tot = s.totalMs;
		uint32_t p2 = cyc2us(s.p2Cycles) / 1000;
		uint32_t p1 = tot > p2 ? tot - p2 : 0;
		uint32_t fl1 = cyc2us(f.cycles[0] + f.cycles[1]) / 1000, fl2 = cyc2us(f.cycles[2] + f.cycles[3]) / 1000;
		line(r++, TFT_GREEN, "SOLVER res=%d len=%u (f1=%u f2=%u) estagio=%u", (int)s.result, (unsigned)s.solLen,
		     (unsigned)s.sol1, (unsigned)(s.solLen > s.sol1 ? s.solLen - s.sol1 : 0), (unsigned)s.stage);
		line(r++, TFT_WHITE, "  total %lums  f1 %lu(fl %lu)  f2 %lu(fl %lu)", (unsigned long)tot, (unsigned long)p1,
		     (unsigned long)fl1, (unsigned long)p2, (unsigned long)fl2);
		line(r++, TFT_WHITE, "  nos f1=%lu f2=%lu G1=%lu lim2=%lu", (unsigned long)s.nodes1, (unsigned long)s.nodes2,
		     (unsigned long)s.p2Calls, (unsigned long)s.p2Iters);

		uint32_t reads = 0;
		uint64_t cyc = 0;
		for (uint8_t i = 0; i < 4; i++) {
			reads += f.reads[i];
			cyc += f.cycles[i];
		}
		uint32_t mhz = SystemCoreClock / 1000000UL;
		if (!mhz) mhz = 1;
		uint32_t avg100 = reads ? (uint32_t)(cyc * 100 / ((uint64_t)mhz * reads)) : 0;
		uint32_t pct = tot ? (cyc2us(cyc) / 1000) * 100 / tot : 0;
		line(r++, TFT_MAGENTA, "FLASH %lu leit  media %lu.%02lu us  %lu%% do tempo", (unsigned long)reads,
		     (unsigned long)(avg100 / 100), (unsigned long)(avg100 % 100), (unsigned long)pct);
		line(r++, TFT_WHITE, "  ts=%lu fs=%lu cs=%lu us=%lu", (unsigned long)f.reads[0], (unsigned long)f.reads[1],
		     (unsigned long)f.reads[2], (unsigned long)f.reads[3]);
		uint64_t th = (uint64_t)f.hits + f.misses;
		uint32_t pm = th ? (uint32_t)((uint64_t)f.hits * 1000 / th) : 0;
		line(r++, TFT_WHITE, "  cache sim %ux%uB: acerto %lu.%lu%%", (unsigned)METRICS_SIM_LINES,
		     (unsigned)(1u << METRICS_SIM_SHIFT), (unsigned long)(pm / 10), (unsigned long)(pm % 10));
#endif

		line(r++, TFT_CYAN, "PILHAS (words livres/total)");
		for (uint8_t i = 0; i < _nt; i += 2) {
			char a[24], b[24];
			stackStr(a, sizeof(a), i);
			if (i + 1 < _nt)
				stackStr(b, sizeof(b), i + 1);
			else
				b[0] = 0;
			line(r++, TFT_WHITE, "  %-22s %s", a, b);
		}
		line(r++, TFT_WHITE, "Input: eventos perdidos %lu", (unsigned long)_in.dropped());
	}

   private:
	static const uint8_t LW = 52, ROWS = 22, MAXT = 8;
	struct TaskInfo {
		const char* name;
		TaskHandle_t* h;
		uint16_t words;
	};

	TFT_FSMC& _tft;
	InputManager& _in;
	TaskInfo _t[MAXT];
	uint8_t _nt = 0;
	char _prev[ROWS][LW + 1];
	uint32_t _last = 0;
	uint32_t _minHeap = 0xFFFFFFFF;  // menor valor visto (amostrado a cada 500 ms: pode perder picos curtos)

	static uint32_t cyc2us(uint64_t cyc) {
		uint32_t mhz = SystemCoreClock / 1000000UL;
		return (uint32_t)(cyc / (mhz ? mhz : 1));
	}

	static uint32_t mallocFree() {  // aproximado: livre dentro do malloc + espaço ainda não pedido à RAM
		struct mallinfo mi = mallinfo();
		int32_t room = (int32_t)(((uint32_t)&_estack - 0x800) - (uint32_t)sbrk(0));  // 2 KB p/ pilha principal
		return mi.fordblks + (room > 0 ? room : 0);
	}

	void stackStr(char* o, size_t n, uint8_t i) {
		unsigned fr = (_t[i].h && *_t[i].h) ? (unsigned)uxTaskGetStackHighWaterMark(*_t[i].h) : 0;
		snprintf(o, n, "%-8s %4u/%-4u", _t[i].name, fr, (unsigned)_t[i].words);
	}

	void line(uint8_t row, uint16_t fg, const char* fmt, ...) {
		if (row >= ROWS) return;
		char b[LW + 1];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(b, sizeof(b), fmt, ap);
		va_end(ap);
		char out[LW + 1];
		snprintf(out, sizeof(out), "%-*s", (int)LW, b);
		if (strcmp(out, _prev[row]) == 0) return;
		strcpy(_prev[row], out);
		_tft.setTextSize(1);
		_tft.setTextColor(fg, TFT_BLACK);
		_tft.setCursor(2, 4 + row * 10);
		_tft.print(out);
	}
};