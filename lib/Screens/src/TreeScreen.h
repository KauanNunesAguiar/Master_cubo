// TreeScreen.h — visualização da árvore de busca do solver (Etapa 6)
#pragma once
#include "CubeHUD.h"
#include "Screen.h"
#include "SolverEvents.h"
#include "TFT_FSMC.h"

#if SOLVER_EVENTS
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

class TreeScreen : public Screen {
   public:
	bool autoReturn = true;    // se entrou durante a busca, volta ao HUD quando ela termina
	uint16_t lingerMs = 1200;  // quanto tempo mostra a árvore final antes de voltar

	TreeScreen(TFT_FSMC& tft, CubeHUD& hud, ScreenManager& mgr, uint8_t hudIdx)
	    : _tft(tft), _hud(hud), _mgr(mgr), _hudIdx(hudIdx) {}

	void onEnter() override {
		_sawSearch = (_hud.state() == CubeHUD::ST_SEARCHING);
		_doneAt = 0;
		_last = 0;
		_seq = g_trace.seq;
		drawFrame();
	}

	void update(uint32_t now) override {
		if (autoReturn && _sawSearch) {
			if (_hud.state() == CubeHUD::ST_SEARCHING)
				_doneAt = 0;
			else if (!_doneAt)
				_doneAt = now | 1;
			else if (now - _doneAt >= lingerMs) {
				_doneAt = 0;
				_sawSearch = false;
				_mgr.request(_hudIdx);
				return;
			}
		}
		if (_last && now - _last < 100) return;
		_last = now | 1;

		const SolverTrace& t = g_trace;
		if (t.seq != _seq) {  // busca nova: limpa o gráfico
			_seq = t.seq;
			drawChartFrame();
		}
		drawBars(t);
		drawText(t, now);
	}

   private:
	static const int16_t X0 = 26, CH_TOP = 40, CH_H = 128, BOT = CH_TOP + CH_H, GW = 10;
	static const uint8_t NG = 27, LW = 52, ROWS = 5;
	static const uint16_t BG = TFT_BLACK, C_ORANGE = 0xFD20, C_GRID = 0x2945;

	TFT_FSMC& _tft;
	CubeHUD& _hud;
	ScreenManager& _mgr;
	uint8_t _hudIdx;
	bool _sawSearch = false;
	uint32_t _doneAt = 0, _last = 0;
	uint8_t _seq = 0;
	uint8_t _h[NG][4];
	char _prev[ROWS][LW + 1];

	// log2 em ponto fixo, escala fixa (2^20 = topo) => as barras só crescem durante a busca
	static uint8_t hOf(uint32_t n) {
		uint32_t v = n + 1;
		int b = 31 - __builtin_clz(v);
		uint32_t frac = ((v - (1u << b)) << 8) >> b;
		uint32_t h = (((uint32_t)b << 8) | frac) * CH_H / (20u << 8);
		return h > CH_H ? CH_H : (uint8_t)h;
	}

	void drawChartFrame() {
		_tft.fillRect(X0, CH_TOP, NG * GW, CH_H, BG);
		static const uint32_t G[5] = {10, 100, 1000, 10000, 100000};
		for (uint8_t i = 0; i < 5; i++) _tft.drawFastHLine(X0, BOT - hOf(G[i]), NG * GW, C_GRID);
		_tft.drawFastHLine(X0, BOT, NG * GW, TFT_WHITE);
		memset(_h, 0, sizeof(_h));
	}

	void drawFrame() {
		_tft.fillScreen(BG);
		_tft.setTextSize(2);
		_tft.setTextColor(TFT_WHITE, BG);
		_tft.setCursor(4, 4);
		_tft.print("ARVORE DE BUSCA");
		_tft.setTextSize(1);
		_tft.setTextColor(TFT_CYAN, BG);
		_tft.setCursor(26, 26);
		_tft.print("F1 nos");
		_tft.setTextColor(TFT_RED, BG);
		_tft.setCursor(84, 26);
		_tft.print("F1 poda");
		_tft.setTextColor(C_ORANGE, BG);
		_tft.setCursor(146, 26);
		_tft.print("F2 nos");
		_tft.setTextColor(TFT_MAGENTA, BG);
		_tft.setCursor(204, 26);
		_tft.print("F2 poda");
		_tft.setTextColor(TFT_WHITE, BG);
		_tft.setCursor(262, 26);
		_tft.print("log");

		static const uint32_t G[5] = {10, 100, 1000, 10000, 100000};
		static const char* const GL[5] = {"10", "100", "1k", "10k", "100k"};
		_tft.setTextColor(C_GRID | 0x4208, BG);
		for (uint8_t i = 0; i < 5; i++) {
			_tft.setCursor(0, BOT - hOf(G[i]) - 4);
			_tft.print(GL[i]);
		}
		_tft.setTextColor(TFT_WHITE, BG);
		for (uint8_t d = 0; d < NG; d += 5) {
			_tft.setCursor(X0 + d * GW, BOT + 3);
			_tft.print((int)d);
		}
		drawChartFrame();
		memset(_prev, 0, sizeof(_prev));
	}

	void drawBars(const SolverTrace& t) {
		static const uint16_t COL[4] = {TFT_CYAN, TFT_RED, C_ORANGE, TFT_MAGENTA};
		for (uint8_t d = 0; d < NG; d++) {
			uint32_t v[4] = {t.nodes[0][d], t.pruned[0][d], t.nodes[1][d], t.pruned[1][d]};
			for (uint8_t k = 0; k < 4; k++) {
				uint8_t h = hOf(v[k]), o = _h[d][k];
				if (h == o) continue;
				int16_t x = X0 + d * GW + k * 2;
				if (h > o)
					_tft.fillRect(x, BOT - h, 2, h - o, COL[k]);
				else
					_tft.fillRect(x, BOT - o, 2, o - h, BG);
				_h[d][k] = h;
			}
		}
	}

	// razão média entre profundidades consecutivas (inclui repetições da busca iterativa)
	static uint32_t branching100(const uint32_t* a) {
		int dmin = 1, dmax = 0;
		while (dmin < 32 && !a[dmin]) dmin++;
		for (int d = 31; d >= 1; d--)
			if (a[d]) {
				dmax = d;
				break;
			}
		if (dmin >= 32 || dmax <= dmin) return 0;
		return (uint32_t)(powf((float)a[dmax] / (float)a[dmin], 1.0f / (float)(dmax - dmin)) * 100.0f);
	}

	void drawText(const SolverTrace& t, uint32_t now) {
		static const char FC[7] = "URFDLB";

		// 0: caminho atual (últimos 12 giros)
		char path[40];
		uint8_t p = 0, d0 = t.curDepth > 12 ? t.curDepth - 11 : 1;
		for (uint8_t d = d0; d <= t.curDepth && d < 32 && p < sizeof(path) - 4; d++) {
			uint8_t m = t.path[d];
			path[p++] = FC[(m / 3) % 6];
			if (m % 3 == 1)
				path[p++] = '2';
			else if (m % 3 == 2)
				path[p++] = '\'';
			path[p++] = ' ';
		}
		path[p] = 0;
		line(0, TFT_WHITE, "F%u d%-2u %s%s", (unsigned)t.curPhase + 1, (unsigned)t.curDepth, d0 > 1 ? ".. " : "", path);

		// 1: últimos 10 eventos do buffer circular (N/P = fase 1 nó/poda, n/p = fase 2, G = G1, S = solução)
		static const char EC[8] = {'N', 'G', 'n', 'S', 'P', 'p', '?', '?'};
		char ev[48];
		uint8_t q = 0;
		uint32_t have = t.head < TRACE_RING ? t.head : TRACE_RING;
		uint8_t cnt = have < 10 ? (uint8_t)have : 10;
		for (uint8_t i = cnt; i >= 1; i--) {
			uint16_t e = t.ring[(t.head - i) & (TRACE_RING - 1)];
			q += snprintf(ev + q, sizeof(ev) - q, "%c%u ", EC[e & 7], (unsigned)((e >> 3) & 31));
		}
		ev[q] = 0;
		line(1, TFT_YELLOW, "ult: %s", ev);

		// 2: totais
		uint32_t n1 = 0, p1 = 0, n2 = 0, p2 = 0;
		for (uint8_t d = 0; d < 32; d++) {
			n1 += t.nodes[0][d];
			p1 += t.pruned[0][d];
			n2 += t.nodes[1][d];
			p2 += t.pruned[1][d];
		}
		line(2, TFT_WHITE, "nos F1=%lu poda %lu | F2=%lu poda %lu", (unsigned long)n1, (unsigned long)p1,
		     (unsigned long)n2, (unsigned long)p2);

		// 3: ramificação média e % de poda nos últimos 64 eventos
		uint32_t rn = 0, rp = 0;
		for (uint8_t i = 1; i <= have; i++) {
			uint8_t ty = t.ring[(t.head - i) & (TRACE_RING - 1)] & 7;
			if (ty == EV_P1_NODE || ty == EV_P2_NODE)
				rn++;
			else if (ty == EV_P1_PRUNE || ty == EV_P2_PRUNE)
				rp++;
		}
		uint32_t b1 = branching100(t.nodes[0]), b2 = branching100(t.nodes[1]);
		line(3, TFT_WHITE, "ramif F1 %lu.%02lu  F2 %lu.%02lu  poda rec. %lu%%", (unsigned long)(b1 / 100),
		     (unsigned long)(b1 % 100), (unsigned long)(b2 / 100), (unsigned long)(b2 % 100),
		     (unsigned long)(rn ? rp * 100 / rn : 0));

		// 4: status
		uint32_t el = t.running ? now - t.t0 : t.t1 - t.t0;
		if (t.running)
			line(4, TFT_ORANGE_(), "BUSCANDO %lu.%lu s   G1=%lu", (unsigned long)(el / 1000),
			     (unsigned long)((el / 100) % 10), (unsigned long)t.g1);
		else if (t.solved)
			line(4, TFT_GREEN, "Resolvido: %u giros em %lu.%lu s  G1=%lu", (unsigned)t.solLen,
			     (unsigned long)(el / 1000), (unsigned long)((el / 100) % 10), (unsigned long)t.g1);
		else if (t.t0)
			line(4, TFT_RED, "Sem solucao  %lu.%lu s", (unsigned long)(el / 1000), (unsigned long)((el / 100) % 10));
		else
			line(4, TFT_WHITE, "Nenhuma busca ainda");
	}

	static uint16_t TFT_ORANGE_() { return C_ORANGE; }

	void line(uint8_t row, uint16_t fg, const char* fmt, ...) {
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
		_tft.setTextColor(fg, BG);
		_tft.setCursor(2, 184 + row * 10);
		_tft.print(out);
	}
};

#else  // SOLVER_EVENTS == 0: tela vazia com aviso
class TreeScreen : public Screen {
   public:
	bool autoReturn = true;
	uint16_t lingerMs = 1200;
	TreeScreen(TFT_FSMC& tft, CubeHUD&, ScreenManager&, uint8_t) : _tft(tft) {}
	void onEnter() override {
		_tft.fillScreen(TFT_BLACK);
		_tft.setTextSize(1);
		_tft.setTextColor(TFT_WHITE, TFT_BLACK);
		_tft.setCursor(8, 8);
		_tft.print("Compile com -D SOLVER_EVENTS=1");
	}
	void update(uint32_t) override {}

   private:
	TFT_FSMC& _tft;
};
#endif