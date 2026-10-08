#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Coords.h"
#include "CubeSolver.h"
#include "CubeState.h"
#include "CubeView.h"
#include "PruneTable.h"
#include "TFT_FSMC.h"
#include "TouchXPT2046.h"
#include "W25Q16.h"
#include "stm32f4ve_peripherals.h"

/* ================================================================= *
 * 1. MÓDULO ENCODER ROTATIVO KY-040                                *
 * ================================================================= */
#define ENCODER_CLK PA4
#define ENCODER_DT PA5
#define ENCODER_SW PA7

/* ================================================================= *
 * 2. BOTÕES DE SELEÇÃO DE FACES (U, R, F, D, L, B)                 *
 * ================================================================= */
#define BTN_FACE_U PC3  // Branco
#define BTN_FACE_R PA3  // Vermelho
#define BTN_FACE_F PC1  // Verde
#define BTN_FACE_D PA2  // Amarelo
#define BTN_FACE_L PC0  // Laranja
#define BTN_FACE_B PC2  // Azul

/* Autotestes no boot (poda + solver). Ponha 0 para desligar. */
#define RUN_SELFTESTS 0

/* Botões na tela (MISTURA / RESOLVE). Ponha 0 se o toque não estiver calibrado. */
#define USE_TOUCH_BUTTONS 1

/* Cubo gira sozinho por alguns segundos quando é resolvido. */
#define CELEBRATE 1

TouchXPT2046 touch;
TFT_FSMC tft;
SemaphoreHandle_t tftMutex;

CubeState cubeState;
CubeView cube(tft, cubeState);

W25Q16 flash;
CubeSolver solver(flash);

// Estado atual do controle
uint8_t selectedFace = FACE_F;  // Face padrão inicial (Front)
volatile int encoderDelta = 0;
bool lastClkState = HIGH;

volatile bool solveRequest = false;  // K1/toque pede, taskSolve atende
volatile bool solving = false;       // true enquanto o solver roda

/* ================================================================= *
 * 3. ESTADO DA INTERFACE                                            *
 * ================================================================= */
enum UiState : uint8_t { UI_IDLE, UI_SEARCHING, UI_SOLVING, UI_MANUAL, UI_SOLVED, UI_ERROR };

volatile UiState uiState = UI_IDLE;
volatile uint32_t tStart = 0, tEnd = 0, solveClickMs = 0;
volatile bool timerArmed = false;      // depois de embaralhar: o 1o giro manual liga o cronômetro
volatile bool scrambleActive = false;  // animação do embaralhamento em andamento
volatile bool solvedAuto = false;      // true = resolvido pelo solver, false = à mão

char movesText[160];             // sequência exibida (embaralhamento ou solução)
volatile uint8_t movesKind = 0;  // 0 nada, 1 embaralhamento, 2 solução
volatile uint32_t movesVer = 0;  // sobe a cada mudança da lista

uint32_t lastSearchMs = 0, lastNodes = 0, bestMs = 0;
uint16_t autoSolves = 0, manualSolves = 0, manualMoves = 0;
char uiMsg[24] = "", uiErr[24] = "";
volatile uint32_t msgUntil = 0;

static bool inputLocked() {
	UiState s = uiState;
	return solving || scrambleActive || s == UI_SEARCHING || s == UI_SOLVING;
}

static void showMsg(const char* m) {  // mensagem temporária (2,5 s) na linha de status
	strncpy(uiMsg, m, sizeof(uiMsg) - 1);
	uiMsg[sizeof(uiMsg) - 1] = 0;
	msgUntil = millis() + 2500;
}

static void setError(const char* m) {
	strncpy(uiErr, m, sizeof(uiErr) - 1);
	uiErr[sizeof(uiErr) - 1] = 0;
	tEnd = millis();
	uiState = UI_ERROR;
}

static void setMoves(const char* s, uint8_t kind) {
	taskENTER_CRITICAL();
	strncpy(movesText, s, sizeof(movesText) - 1);
	movesText[sizeof(movesText) - 1] = 0;
	movesKind = kind;
	movesVer = movesVer + 1;
	taskEXIT_CRITICAL();
}

static const char* solverErr(int e) {
	switch (e) {
		case -1:
			return "Cubo invalido";
		case -2:
			return "Tempo esgotado";
		case -3:
			return "Sem solucao";
		case -4:
			return "Buffer pequeno";
		default:
			return "Erro";
	}
}

/* ---------- ações (usadas por botões físicos, encoder e toque) ---------- */

static void requestSolve() {
	if (inputLocked()) return;
	solveClickMs = millis();  // o cronômetro conta a partir do clique
	solveRequest = true;
}

static void doScramble() {
	if (inputLocked()) return;
	srand(millis());
	char seq[96];
	uint8_t p = 0;
	int last = -1;
	for (uint8_t i = 0; i < 30; i++) {
		int f;
		do { f = rand() % 6; } while (f == last);
		last = f;
		int t = 1 + rand() % 3;
		seq[p++] = "URFDLB"[f];
		if (t == 2)
			seq[p++] = '2';
		else if (t == 3)
			seq[p++] = '\'';
		seq[p++] = ' ';
	}
	seq[p - 1] = '\0';

	if (!cube.moves(seq)) {
		showMsg("Fila cheia");
		return;
	}
	setMoves(seq, 1);
	manualMoves = 0;
	tStart = 0;
	tEnd = 0;
	timerArmed = true;
	scrambleActive = true;
	uiState = UI_IDLE;
	Serial.printf("Embaralhado: %s\n", seq);
}

static void onManualMove() {
	manualMoves++;
	if (timerArmed && uiState == UI_IDLE) {  // primeiro giro depois de embaralhar liga o cronômetro
		timerArmed = false;
		tEnd = 0;
		tStart = millis();
		uiState = UI_MANUAL;
	}
}

/* ================================================================= *
 * 4. DESENHO DO HUD                                                 *
 * Layout (320x240):                                                 *
 *   x 0..199   cubo (200x200) + botões de toque embaixo             *
 *   x 200..319 painel: badge, timer, barra, giro atual, lista, info *
 * Todo texto é de largura fixa e opaco (sem apagar antes), então    *
 * não pisca. A fonte padrão não tem acentos: textos sem acento.     *
 * ================================================================= */
#define UI_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t C_BG = UI_RGB(14, 16, 26);
static const uint16_t C_LINE = UI_RGB(60, 66, 96);
static const uint16_t C_TRACK = UI_RGB(38, 42, 62);
static const uint16_t C_TEXT = UI_RGB(235, 238, 245);
static const uint16_t C_DIM = UI_RGB(120, 126, 150);
static const uint16_t C_YEL = UI_RGB(255, 205, 0);
static const uint16_t C_GRN = UI_RGB(60, 220, 110);
static const uint16_t C_RED = UI_RGB(255, 80, 80);
static const uint16_t C_CYN = UI_RGB(0, 200, 240);
static const uint16_t C_ORG = UI_RGB(255, 150, 0);
static const uint16_t C_PUR = UI_RGB(170, 120, 255);
static const uint16_t C_BLU = UI_RGB(90, 140, 255);

static const int16_t PX = 200, PW = 120;  // painel
static const int16_t IX = 208, IW = 108;  // área útil do painel
static const int16_t BADGE_Y = 4, TIMER_Y = 32, BAR_Y = 60, LINE_Y = 72, BIG_Y = 86, LABEL_Y = 122, LIST_Y = 134,
                     CELL_W = 18, CELL_H = 11, STATS_Y = 195;
static const int16_t BTN_Y = 204, BTN_H = 32, BTN_W = 92, BTN1_X = 6, BTN2_X = 102;

/* Texto de largura fixa com cache: só redesenha se mudou. */
struct TxtCache {
	char s[24];
	uint16_t fg;
};
static TxtCache g_tc[10];

static void uiText(uint8_t slot, int16_t x, int16_t y, uint8_t size, uint16_t fg, const char* s, uint8_t width) {
	char buf[24];
	snprintf(buf, sizeof(buf), "%-*s", (int)width, s);
	buf[width] = 0;
	if (g_tc[slot].fg == fg && strcmp(g_tc[slot].s, buf) == 0) return;
	strcpy(g_tc[slot].s, buf);
	g_tc[slot].fg = fg;

	xSemaphoreTake(tftMutex, portMAX_DELAY);
	tft.setTextSize(size);
	tft.setTextColor(fg, C_BG);
	tft.setCursor(x, y);
	tft.print(buf);
	xSemaphoreGive(tftMutex);
}

static void fmtTime(char* out, size_t n, uint32_t ms) {  // m:ss.d
	if (ms > 599900) ms = 599900;
	snprintf(out, n, "%u:%02u.%u", (unsigned)(ms / 60000), (unsigned)((ms / 1000) % 60), (unsigned)((ms / 100) % 10));
}

static void drawStatic() {
	xSemaphoreTake(tftMutex, portMAX_DELAY);
	tft.fillRect(PX, 0, PW, 240, C_BG);
	tft.drawFastVLine(PX, 0, 240, C_LINE);
	tft.fillRect(0, 200, 200, 40, TFT_BLACK);
	xSemaphoreGive(tftMutex);
}

#if USE_TOUCH_BUTTONS
static void drawButton(int16_t x, const char* label, uint16_t color, bool en) {
	tft.fillRoundRect(x, BTN_Y, BTN_W, BTN_H, 7, en ? color : C_TRACK);
	tft.setTextSize(2);
	tft.setTextColor(en ? TFT_BLACK : C_DIM);
	int16_t w = (int16_t)strlen(label) * 12 - 2;
	tft.setCursor(x + (BTN_W - w) / 2, BTN_Y + (BTN_H - 14) / 2);
	tft.print(label);
}
static void drawButtons(bool en) {
	xSemaphoreTake(tftMutex, portMAX_DELAY);
	drawButton(BTN1_X, "MISTURA", C_BLU, en);
	drawButton(BTN2_X, "RESOLVE", C_GRN, en);
	xSemaphoreGive(tftMutex);
}
#else
static void drawButtons(bool) {}
#endif

static void badgeFor(UiState s, const char*& label, uint16_t& bg, uint16_t& fg) {
	fg = TFT_BLACK;
	switch (s) {
		case UI_IDLE:
			label = "PRONTO";
			bg = C_BLU;
			fg = TFT_WHITE;
			break;
		case UI_SEARCHING:
			label = "BUSCANDO";
			bg = C_ORG;
			break;
		case UI_SOLVING:
			label = "GIRANDO";
			bg = C_CYN;
			break;
		case UI_MANUAL:
			label = "MANUAL";
			bg = C_PUR;
			break;
		case UI_SOLVED:
			label = "FEITO!";
			bg = C_GRN;
			break;
		default:
			label = "ERRO";
			bg = C_RED;
			fg = TFT_WHITE;
			break;
	}
}

static void drawBadge(UiState s) {
	const char* label;
	uint16_t bg, fg;
	badgeFor(s, label, bg, fg);
	xSemaphoreTake(tftMutex, portMAX_DELAY);
	tft.fillRoundRect(IX, BADGE_Y, IW, 22, 5, bg);
	tft.setTextSize(2);
	tft.setTextColor(fg);
	int16_t w = (int16_t)strlen(label) * 12 - 2;
	tft.setCursor(IX + (IW - w) / 2, BADGE_Y + 4);
	tft.print(label);
	xSemaphoreGive(tftMutex);
}

/* Barra: trilho + trecho preenchido [fx, fx+fw). Cada pixel é escrito uma vez só (sem piscar). */
static void drawBar(int16_t fx, int16_t fw, uint16_t col) {
	static int16_t lfx = -1, lfw = -1;
	static uint16_t lcol = 0;
	if (fx == lfx && fw == lfw && col == lcol) return;
	lfx = fx;
	lfw = fw;
	lcol = col;
	xSemaphoreTake(tftMutex, portMAX_DELAY);
	if (fx > 0) tft.fillRect(IX, BAR_Y, fx, 8, C_TRACK);
	if (fw > 0) tft.fillRect(IX + fx, BAR_Y, fw, 8, col);
	int16_t rx = fx + fw;
	if (rx < IW) tft.fillRect(IX + rx, BAR_Y, IW - rx, 8, C_TRACK);
	xSemaphoreGive(tftMutex);
}

/* ---- lista de movimentos ---- */
static char tok[32][3];
static uint8_t ntok = 0;

static void parseTokens(const char* s) {
	ntok = 0;
	while (*s && ntok < 30) {
		while (*s == ' ') s++;
		if (!*s) break;
		uint8_t k = 0;
		while (*s && *s != ' ') {
			if (k < 2) tok[ntok][k++] = *s;
			s++;
		}
		tok[ntok][k] = 0;
		ntok++;
	}
}

// 0 pendente, 1 atual, 2 feito
static uint8_t tokState(uint8_t i, bool prog, int16_t done) {
	if (!prog) return 0;
	if (i < done) return 2;
	if (i == done) return 1;
	return 0;
}

static void drawToken(uint8_t i, uint8_t st) {
	int16_t x = IX + (i % 6) * CELL_W, y = LIST_Y + (i / 6) * CELL_H;
	xSemaphoreTake(tftMutex, portMAX_DELAY);
	tft.fillRect(x - 1, y - 1, 17, 10, st == 1 ? C_YEL : C_BG);
	tft.setTextSize(1);
	tft.setTextColor(st == 1 ? TFT_BLACK : (st == 2 ? C_DIM : C_TEXT));
	tft.setCursor(x + 1, y);
	tft.print(tok[i]);
	xSemaphoreGive(tftMutex);
}

static void redrawList(bool prog, int16_t done) {
	xSemaphoreTake(tftMutex, portMAX_DELAY);
	tft.fillRect(PX + 1, LIST_Y - 2, PW - 1, 5 * CELL_H + 2, C_BG);
	xSemaphoreGive(tftMutex);
	for (uint8_t i = 0; i < ntok; i++) drawToken(i, tokState(i, prog, done));
}

static uint8_t stickersOk() {  // quantos adesivos já estão na cor da própria face (0..54)
	uint8_t n = 0;
	for (uint8_t f = 0; f < 6; f++)
		for (uint8_t i = 0; i < 9; i++)
			if (cubeState.get(f, i) == f) n++;
	return n;
}

static const char* statusText(UiState s, uint32_t now) {
	if ((int32_t)(msgUntil - now) > 0) return uiMsg;
	switch (s) {
		case UI_IDLE:
			return timerArmed ? "Gire ou aperte K1" : "K1 = resolver";
		case UI_SEARCHING:
			return "Procurando...";
		case UI_SOLVING:
			return "Executando solucao";
		case UI_MANUAL:
			return "Resolva o cubo!";
		case UI_SOLVED:
			return solvedAuto ? "Resolvido (auto)" : "Resolvido (manual)";
		default:
			return uiErr;
	}
}

#if RUN_SELFTESTS
static void selfTests() {
	// 1) Poda: a profundidade de qualquer estado alcançado com k giros tem que ser <= k
	{
		PruneTable ts{&flash, PRUNE_ADDR_TWIST_SLICE}, fs{&flash, PRUNE_ADDR_FLIP_SLICE};
		PruneTable cs{&flash, PRUNE_ADDR_CPERM_SP}, us{&flash, PRUNE_ADDR_UDPERM_SP};
		bool ok = true;
		for (int n = 0; n < 200 && ok; n++) {
			int k = 1 + rand() % 6;
			CubieCube c, d;
			for (int i = 0; i < k; i++) c.multiply(moveCubie(rand() % N_MOVES));
			for (int i = 0; i < k; i++) d.multiply(moveCubie(P2_MOVES[rand() % N_MOVES_P2]));
			if (ts.get((uint32_t)getTwist(c) * N_SLICE + getSlice(c)) > k) ok = false;
			if (fs.get((uint32_t)getFlip(c) * N_SLICE + getSlice(c)) > k) ok = false;
			if (cs.get((uint32_t)getCPerm(d) * N_SLICE_PERM + getSlicePerm(d)) > k) ok = false;
			if (us.get((uint32_t)getUDPerm(d) * N_SLICE_PERM + getSlicePerm(d)) > k) ok = false;
		}
		Serial.println(ok ? "poda OK" : "poda ERRO");
	}

	// 2) Solver: embaralha, resolve, aplica a solução e confere isSolved()
	for (int n = 0; n < 5; n++) {
		CubeState s;
		for (int i = 0; i < 25; i++) s.applyMove(rand() % 6, 1 + rand() % 3);
		char sol[128];
		uint32_t t0 = millis();
		int len = solver.solve(s, sol, sizeof(sol));
		uint32_t dt = millis() - t0;
		if (len >= 0) s.applyMoves(sol);
		Serial.printf("len=%d  %lu ms  %lu nos  resolvido=%d\n  %s\n", len, (unsigned long)dt,
		              (unsigned long)solver.nodes(), s.isSolved(), len >= 0 ? sol : "");
	}
}
#endif

/* ================================================================= *
 * 5. TAREFAS                                                        *
 * ================================================================= */

/* UI: lê o toque, atualiza o estado (fim da solução / fim do giro manual) e redesenha o painel. */
void taskUI(void*) {
	drawStatic();
	drawButtons(true);

	UiState lastBadge = (UiState)255;
	bool lastLocked = false;
	uint32_t lastVer = 0xFFFFFFFF;
	int16_t lastDone = -2;
	bool lastProg = false;
	uint8_t kind = 0;
	uint32_t idleSince = 0, spinUntil = 0;
	uint8_t released = 10;
	char snap[160], line[24], tbuf[12];

	for (;;) {
		const uint32_t now = millis();

#if USE_TOUCH_BUTTONS
		/* --- toque nos botões (só na borda de subida, com 100 ms de "solto" antes) --- */
		int16_t tx, ty;
		if (touch.read(tx, ty)) {
			if (released >= 4 && ty >= BTN_Y && ty < BTN_Y + BTN_H) {
				if (tx >= BTN1_X && tx < BTN1_X + BTN_W)
					doScramble();
				else if (tx >= BTN2_X && tx < BTN2_X + BTN_W)
					requestSolve();
			}
			released = 0;
		} else if (released < 255) {
			released++;
		}
#endif

		/* --- fim de animação: o cubo precisa ficar parado por 60 ms (evita falso positivo) --- */
		if (cube.busy())
			idleSince = 0;
		else if (!idleSince)
			idleSince = now | 1;
		if (idleSince && now - idleSince >= 60 && !cube.busy()) {
			scrambleActive = false;
			UiState s = uiState;
			if (s == UI_SOLVING) {
				tEnd = idleSince;
				if (cubeState.isSolved()) {
					solvedAuto = true;
					autoSolves++;
					uiState = UI_SOLVED;
					spinUntil = now + 5000;
					Serial.printf("Resolvido em %lu ms\n", (unsigned long)(tEnd - tStart));
				} else {
					setError("Nao resolveu?!");
				}
			} else if (s == UI_MANUAL && cubeState.isSolved()) {
				tEnd = idleSince;
				uint32_t t = tEnd - tStart;
				solvedAuto = false;
				manualSolves++;
				if (!bestMs || t < bestMs) bestMs = t;
				uiState = UI_SOLVED;
				spinUntil = now + 5000;
				Serial.printf("Resolvido a mao em %lu ms\n", (unsigned long)t);
			}
		}
		const UiState cur = uiState;

#if CELEBRATE
		if ((int32_t)(spinUntil - now) > 0) cube.rotate(0, 2.0f);
#endif

		/* --- botões: acinzentados enquanto a entrada está travada --- */
		const bool locked = inputLocked();
		if (locked != lastLocked) {
			drawButtons(!locked);
			lastLocked = locked;
		}

		/* --- badge --- */
		if (cur != lastBadge) {
			drawBadge(cur);
			lastBadge = cur;
		}

		/* --- cronômetro --- */
		uint32_t ms = 0;
		uint16_t tcol = C_DIM;
		switch (cur) {
			case UI_SEARCHING:
			case UI_SOLVING:
				ms = now - tStart;
				tcol = C_TEXT;
				break;
			case UI_MANUAL:
				ms = now - tStart;
				tcol = C_YEL;
				break;
			case UI_SOLVED:
				ms = tEnd - tStart;
				tcol = C_GRN;
				break;
			case UI_ERROR:
				ms = tEnd - tStart;
				tcol = C_RED;
				break;
			default:
				break;
		}
		fmtTime(tbuf, sizeof(tbuf), ms);
		uiText(1, IX, TIMER_Y, 3, tcol, tbuf, 6);

		/* --- lista de movimentos (copia segura) --- */
		uint32_t ver = movesVer;
		if (ver != lastVer) {
			taskENTER_CRITICAL();
			memcpy(snap, movesText, sizeof(snap));
			kind = movesKind;
			taskEXIT_CRITICAL();
			parseTokens(snap);
			lastVer = ver;
			lastDone = -2;  // força redesenho completo
		}
		const bool prog = (kind == 2) || (kind == 1 && scrambleActive);
		const uint8_t pend = cube.pending();
		int16_t done = 0;
		if (prog && ntok) done = (pend >= ntok) ? 0 : (int16_t)(ntok - pend);

		if (prog != lastProg || lastDone == -2) {
			redrawList(prog, done);
			lastProg = prog;
			lastDone = done;
		} else if (done != lastDone) {
			int16_t lo = done < lastDone ? done : lastDone;
			int16_t hi = done > lastDone ? done : lastDone;
			if (hi >= ntok) hi = ntok - 1;
			for (int16_t i = lo; i <= hi; i++) drawToken((uint8_t)i, tokState((uint8_t)i, prog, done));
			lastDone = done;
		}

		if (kind == 1)
			snprintf(line, sizeof(line), "Embaralho (%u)", (unsigned)ntok);
		else if (kind == 2)
			snprintf(line, sizeof(line), "Solucao (%u)", (unsigned)ntok);
		else
			line[0] = 0;
		uiText(4, IX, LABEL_Y, 1, kind == 2 ? C_GRN : C_CYN, line, 18);

		/* --- giro atual (grande) + contador --- */
		char big[4] = "";
		if (cur == UI_SEARCHING) {
			static const char sp[4] = {'|', '/', '-', '\\'};
			big[0] = sp[(now / 100) & 3];
			big[1] = 0;
		} else if (prog && done < ntok) {
			strcpy(big, tok[done]);
		}
		uiText(2, IX, BIG_Y, 4, C_YEL, big, 2);

		char cnt[8] = "";
		if (prog && ntok) snprintf(cnt, sizeof(cnt), "%d/%u", (int)done, (unsigned)ntok);
		uiText(3, 256, BIG_Y + 6, 2, C_TEXT, cnt, 5);

		/* --- barra de progresso --- */
		{
			int16_t fx = 0, fw = 0;
			uint16_t bcol = C_BLU;
			switch (cur) {
				case UI_SEARCHING: {  // barra "indeterminada" que vai e volta
					int16_t t = (int16_t)((now / 12) % 156);
					fx = t < 78 ? t : 156 - t;
					fw = 30;
					bcol = C_ORG;
					break;
				}
				case UI_SOLVING:
					fw = ntok ? (int16_t)(IW * done / ntok) : 0;
					bcol = C_CYN;
					break;
				case UI_SOLVED:
					fw = IW;
					bcol = C_GRN;
					break;
				case UI_ERROR:
					fw = IW;
					bcol = C_RED;
					break;
				case UI_MANUAL:
					fw = (int16_t)(IW * stickersOk() / 54);
					bcol = C_PUR;
					break;
				default:
					fw = (int16_t)(IW * stickersOk() / 54);
					break;  // % de adesivos no lugar
			}
			drawBar(fx, fw, bcol);
		}

		/* --- linha de status --- */
		{
			uint16_t c = C_DIM;
			if ((int32_t)(msgUntil - now) > 0)
				c = C_YEL;
			else if (cur == UI_SOLVED)
				c = C_GRN;
			else if (cur == UI_ERROR)
				c = C_RED;
			uiText(0, IX, LINE_Y, 1, c, statusText(cur, now), 18);
		}

		/* --- estatísticas --- */
		char b[24], t2[12];
		if (lastSearchMs)
			snprintf(b, sizeof(b), "Busca: %lu.%lu s", (unsigned long)(lastSearchMs / 1000),
			         (unsigned long)((lastSearchMs / 100) % 10));
		else
			strcpy(b, "Busca: --");
		uiText(5, IX, STATS_Y, 1, C_DIM, b, 18);

		if (lastNodes)
			snprintf(b, sizeof(b), "Nos: %lu", (unsigned long)lastNodes);
		else
			strcpy(b, "Nos: --");
		uiText(6, IX, STATS_Y + 9, 1, C_DIM, b, 18);

		snprintf(b, sizeof(b), "Face %c  Giros: %u", "URFDLB"[selectedFace % 6], (unsigned)manualMoves);
		uiText(7, IX, STATS_Y + 18, 1, C_TEXT, b, 18);

		if (bestMs) {
			fmtTime(t2, sizeof(t2), bestMs);
			snprintf(b, sizeof(b), "Recorde: %s", t2);
		} else {
			strcpy(b, "Recorde: --");
		}
		uiText(8, IX, STATS_Y + 27, 1, C_YEL, b, 18);

		snprintf(b, sizeof(b), "Auto:%u  Manual:%u", (unsigned)autoSolves, (unsigned)manualSolves);
		uiText(9, IX, STATS_Y + 36, 1, C_DIM, b, 18);

		vTaskDelay(pdMS_TO_TICKS(25));
	}
}

void taskRender(void*) {
	cube.alignCameraToFace(FACE_U);

	for (;;) {
		xSemaphoreTake(tftMutex, portMAX_DELAY);
		cube.update();
		xSemaphoreGive(tftMutex);

		vTaskDelay(pdMS_TO_TICKS(20));  // ~50 FPS
	}
}

/* Procura a solução e a enfileira. Roda em tarefa própria porque demora (2 a 15 s). */
void taskSolve(void*) {
	static char sol[128];

	for (;;) {
		if (solveRequest) {
			solveRequest = false;

			if (cube.busy()) {
				showMsg("Cubo ocupado");
			} else if (cubeState.isSolved()) {
				showMsg("Ja esta resolvido");
			} else {
				solving = true;  // trava encoder/embaralhar
				timerArmed = false;
				tEnd = 0;
				tStart = solveClickMs;  // o tempo conta desde o clique
				uiState = UI_SEARCHING;
				setMoves("", 0);
				Serial.println("Resolvendo...");

				CubeState snap = cubeState;
				uint32_t t0 = millis();
				int len = solver.solve(snap, sol, sizeof(sol));
				lastSearchMs = millis() - t0;
				lastNodes = solver.nodes();

				if (len < 0) {
					Serial.printf("Solver erro %d (%lu ms)\n", len, (unsigned long)lastSearchMs);
					setError(solverErr(len));
				} else if (len == 0) {
					tEnd = millis();
					solvedAuto = true;
					uiState = UI_SOLVED;
				} else if (!cube.moves(sol)) {
					setError("Fila cheia");
				} else {
					setMoves(sol, 2);
					solvedAuto = true;
					uiState = UI_SOLVING;
					Serial.printf("Solucao (%d giros, %lu ms): %s\n", len, (unsigned long)lastSearchMs, sol);
				}
				solving = false;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

void taskButtons(void*) {
	int8_t dir = 1;
	bool lastWk = false;
	bool lastK1 = false;
	for (;;) {
		bool wk = digitalRead(BTN_WKUP) == HIGH;
		if (wk && !lastWk) dir = -dir;  // WKUP inverte o sentido
		lastWk = wk;

		if (digitalRead(BTN_K0) == LOW) cube.rotate(0, 4.0f * dir);  // K0: eixo Y

		bool k1 = digitalRead(BTN_K1) == LOW;  // K1: resolve (só na borda de descida)
		if (k1 && !lastK1) requestSolve();
		lastK1 = k1;

		vTaskDelay(pdMS_TO_TICKS(30));
	}
}

// Tarefa para ler o Encoder KY-040 por polling
void taskEncoder(void*) {
	pinMode(ENCODER_CLK, INPUT_PULLUP);
	pinMode(ENCODER_DT, INPUT_PULLUP);
	pinMode(ENCODER_SW, INPUT_PULLUP);

	lastClkState = digitalRead(ENCODER_CLK);

	for (;;) {
		bool currentClk = digitalRead(ENCODER_CLK);
		if (currentClk != lastClkState && currentClk == LOW && !inputLocked()) {
			if (digitalRead(ENCODER_DT) == HIGH) {
				cube.move(selectedFace, 1);  // Horário
			} else {
				cube.move(selectedFace, 3);  // Anti-horário
			}
			onManualMove();
		}
		lastClkState = currentClk;

		// --- CLIQUE DO BOTÃO DO ENCODER EMBARALHA O CUBO ---
		if (digitalRead(ENCODER_SW) == LOW) {
			doScramble();
			vTaskDelay(pdMS_TO_TICKS(400));  // Debounce
		}

		vTaskDelay(pdMS_TO_TICKS(2));
	}
}

// Tarefa para monitorar os 6 botões de seleção de face
void taskFaceButtons(void*) {
	const uint8_t facePins[6] = {BTN_FACE_U, BTN_FACE_R, BTN_FACE_F, BTN_FACE_D, BTN_FACE_L, BTN_FACE_B};

	for (uint8_t i = 0; i < 6; i++) { pinMode(facePins[i], INPUT_PULLUP); }

	for (;;) {
		for (uint8_t i = 0; i < 6; i++) {
			if (digitalRead(facePins[i]) == LOW) {
				selectedFace = i;
				cube.alignCameraToFace(selectedFace);
				Serial.printf("Face selecionada: %d\n", selectedFace);
				vTaskDelay(pdMS_TO_TICKS(200));  // Debounce
			}
		}
		vTaskDelay(pdMS_TO_TICKS(50));
	}
}

void taskLed(void*) {
	for (;;) {
		digitalToggle(LED_D2);
		vTaskDelay(pdMS_TO_TICKS(250));
	}
}

void setup() {
	Serial.begin(115200);
	pinMode(LED_D2, OUTPUT);

	CubeState::init();  // tabelas de permutação (uma vez, antes das tasks)
	solver.begin();     // gera os giros em nível de peça
	Serial.printf("flash %s\n", flash.begin() ? "OK" : "ERRO");

#if RUN_SELFTESTS
	selfTests();
#endif

	tft.begin();
	tft.setRotation(3);
	Serial.printf("ID: %06lX\n", tft.readID());

	tft.fillScreen(TFT_BLACK);
	if (!cube.begin()) { Serial.println("Sem RAM para o canvas do cubo"); }
	cube.setOrigin(0, 0);  // cubo no canto esquerdo; o painel ocupa x >= 200

	pinMode(BTN_K0, INPUT_PULLUP);
	pinMode(BTN_K1, INPUT_PULLUP);
	pinMode(BTN_WKUP, INPUT_PULLDOWN);

	touch.begin(tft.width(), tft.height());
	touch.setCalibration(200, 3900, 200, 3900, true, false, false);

	tftMutex = xSemaphoreCreateMutex();

	xTaskCreate(taskLed, "led", 256, NULL, 1, NULL);
	xTaskCreate(taskUI, "ui", 1024, NULL, 2, NULL);  // ocupa o lugar da antiga taskTouch
	xTaskCreate(taskRender, "render", 2048, NULL, 1, NULL);
	xTaskCreate(taskSolve, "solve", 1536, NULL, 1, NULL);
	xTaskCreate(taskButtons, "buttons", 256, NULL, 2, NULL);
	xTaskCreate(taskEncoder, "encoder", 256, NULL, 3, NULL);
	xTaskCreate(taskFaceButtons, "facebtns", 256, NULL, 2, NULL);

	vTaskStartScheduler();
}

void loop() {
	// Não usado: todas as tarefas rodam no FreeRTOS
}