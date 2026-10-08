// CubeHUD.cpp
#include "CubeHUD.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Metrics.h"

/* ================================================================= *
 * Layout (320x240):                                                 *
 *   x 0..199   cubo (200x200) + botões de toque embaixo             *
 *   x 200..319 painel: badge, timer, barra, giro atual, lista, info *
 * Todo texto é de largura fixa e opaco (sem apagar antes): não pisca *
 * ================================================================= */
#define UI_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

namespace {
const uint16_t C_BG = UI_RGB(14, 16, 26);
const uint16_t C_LINE = UI_RGB(60, 66, 96);
const uint16_t C_TRACK = UI_RGB(38, 42, 62);
const uint16_t C_TEXT = UI_RGB(235, 238, 245);
const uint16_t C_DIM = UI_RGB(120, 126, 150);
const uint16_t C_YEL = UI_RGB(255, 205, 0);
const uint16_t C_GRN = UI_RGB(60, 220, 110);
const uint16_t C_RED = UI_RGB(255, 80, 80);
const uint16_t C_CYN = UI_RGB(0, 200, 240);
const uint16_t C_ORG = UI_RGB(255, 150, 0);
const uint16_t C_PUR = UI_RGB(170, 120, 255);
const uint16_t C_BLU = UI_RGB(90, 140, 255);

const int16_t PX = 200, PW = 120;  // painel
const int16_t IX = 208, IW = 108;  // área útil do painel
const int16_t BADGE_Y = 4, TIMER_Y = 32, BAR_Y = 60, LINE_Y = 72, BIG_Y = 86, LABEL_Y = 122, LIST_Y = 134, CELL_W = 18,
              CELL_H = 11, STATS_Y = 195;
const int16_t BTN_Y = 204, BTN_H = 32, BTN_W = 98, BTN1_X = 1, BTN2_X = 101;

void fmtTime(char* out, size_t n, uint32_t ms) {  // m:ss.d
	if (ms > 599900) ms = 599900;
	snprintf(out, n, "%u:%02u.%u", (unsigned)(ms / 60000), (unsigned)((ms / 1000) % 60), (unsigned)((ms / 100) % 10));
}

const char* solverErr(int e) {
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
}  // namespace

CubeHUD::CubeHUD(TFT_FSMC& tft, CubeView& cube, CubeState& state, CubeSolver& solver, TouchXPT2046* touch)
    : _tft(tft), _cube(cube), _cs(state), _solver(solver), _touch(touch) {
	memset(_tc, 0, sizeof(_tc));
	memset(_tok, 0, sizeof(_tok));
	_sol[0] = 0;
}

void CubeHUD::begin(SemaphoreHandle_t tftMutex) {
	_mtx = tftMutex;
	_cube.setOrigin(0, 0);  // cubo no canto esquerdo; o painel ocupa x >= 200
	_needStatic = true;     // a UI desenha o fundo na primeira passada (já com o mutex)
}

void CubeHUD::uiTask(void* hud) {
	CubeHUD* h = (CubeHUD*)hud;
	for (;;) {
		h->updateUI();
		vTaskDelay(pdMS_TO_TICKS(25));
	}
}

void CubeHUD::solveTask(void* hud) {
	CubeHUD* h = (CubeHUD*)hud;
	for (;;) {
		h->solveStep();
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

/* ========================= estado / ações ========================= */

bool CubeHUD::inputLocked() const {
	State s = _st;
	return _solving || _scrambleActive || s == ST_SEARCHING || s == ST_SOLVING;
}

uint32_t CubeHUD::elapsedMs() const {
	const uint32_t now = millis();
	switch (_st) {
		case ST_SEARCHING:
		case ST_SOLVING:
		case ST_MANUAL:
			return now - _tStart;
		case ST_SOLVED:
		case ST_ERROR:
			return _tEnd - _tStart;
		default:
			return 0;
	}
}

void CubeHUD::showMsg(const char* m) {  // mensagem temporária (2,5 s) na linha de status
	strncpy(_msg, m, sizeof(_msg) - 1);
	_msg[sizeof(_msg) - 1] = 0;
	_msgUntil = millis() + 2500;
}

void CubeHUD::setError(const char* m) {
	strncpy(_err, m, sizeof(_err) - 1);
	_err[sizeof(_err) - 1] = 0;
	_tEnd = millis();
	_st = ST_ERROR;
}

void CubeHUD::setMoves(const char* s, uint8_t kind) {
	taskENTER_CRITICAL();
	strncpy(_movesText, s, sizeof(_movesText) - 1);
	_movesText[sizeof(_movesText) - 1] = 0;
	_movesKind = kind;
	_movesVer = _movesVer + 1;
	taskEXIT_CRITICAL();
}

void CubeHUD::requestSolve() {
	if (inputLocked()) return;
	_clickMs = millis();  // o cronômetro conta a partir do clique
	_solveReq = true;
}

bool CubeHUD::scramble(uint8_t n) {
	if (inputLocked()) return false;
	if (n == 0) n = scrambleLen;
	if (n > 30) n = 30;
	srand(millis());

	char seq[96];
	uint8_t p = 0;
	int last = -1;
	for (uint8_t i = 0; i < n; i++) {
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
	if (p) p--;
	seq[p] = '\0';

	if (!_cube.moves(seq)) {
		showMsg("Fila cheia");
		return false;
	}
	setMoves(seq, 1);
	_manualMoves = 0;
	_tStart = 0;
	_tEnd = 0;
	_armed = true;  // o 1o giro manual depois disto liga o cronômetro
	_scrambleActive = true;
	_st = ST_IDLE;
	Serial.printf("Embaralhado: %s\n", seq);
	return true;
}

void CubeHUD::selectFace(uint8_t face) {
	if (face >= 6) return;
	_face = face;
	_cube.alignCameraToFace(face);
}

bool CubeHUD::turnSelected(bool clockwise) { return turn(_face, clockwise ? 1 : 3); }

bool CubeHUD::turn(uint8_t face, uint8_t turns) {
	if (inputLocked()) return false;
	if (!_cube.move(face, turns)) return false;
	onManualMove();
	return true;
}

void CubeHUD::onManualMove() {
	_manualMoves++;
	if (_armed && _st == ST_IDLE) {  // primeiro giro depois de embaralhar liga o cronômetro
		_armed = false;
		_tEnd = 0;
		_tStart = millis();
		_st = ST_MANUAL;
	}
}

/* Procura a solução e a enfileira (roda na solveTask: demora de 2 a 15 s). */
void CubeHUD::solveStep() {
	if (!_solveReq) return;
	_solveReq = false;

	if (_cube.busy()) {
		showMsg("Cubo ocupado");
		return;
	}
	if (_cs.isSolved()) {
		showMsg("Ja esta resolvido");
		return;
	}

	_solving = true;  // trava giros manuais e embaralhar
	_armed = false;
	_tEnd = 0;
	_tStart = _clickMs;  // o tempo conta desde o clique
	_st = ST_SEARCHING;
	setMoves("", 0);
	Serial.println("Resolvendo...");

	CubeState snap = _cs;
	uint32_t t0 = millis();
	int len = _solver.solve(snap, _sol, sizeof(_sol), 26, 60000, 1500);
	_lastSearchMs = millis() - t0;
	_lastNodes = _solver.nodes();
	metricsPrintSolve(Serial);

	if (len < 0) {
		Serial.printf("Solver erro %d (%lu ms)\n", len, (unsigned long)_lastSearchMs);
		setError(solverErr(len));
	} else if (len == 0) {
		_tEnd = millis();
		_solvedAuto = true;
		_st = ST_SOLVED;
	} else if (!_cube.moves(_sol)) {
		setError("Fila cheia");
	} else {
		setMoves(_sol, 2);
		_solvedAuto = true;
		_st = ST_SOLVING;
		Serial.printf("Solucao (%d giros, %lu ms): %s\n", len, (unsigned long)_lastSearchMs, _sol);
	}
	_solving = false;
}

/* ============================== desenho ============================== */

/* Texto de largura fixa com cache: só redesenha se mudou. */
void CubeHUD::text(uint8_t slot, int16_t x, int16_t y, uint8_t size, uint16_t fg, const char* s, uint8_t width) {
	char buf[24];
	snprintf(buf, sizeof(buf), "%-*s", (int)width, s);
	buf[width] = 0;
	if (_tc[slot].fg == fg && strcmp(_tc[slot].s, buf) == 0) return;
	strcpy(_tc[slot].s, buf);
	_tc[slot].fg = fg;

	lock();
	_tft.setTextSize(size);
	_tft.setTextColor(fg, C_BG);
	_tft.setCursor(x, y);
	_tft.print(buf);
	unlock();
}

void CubeHUD::drawStatic() {
	lock();
	_tft.fillRect(PX, 0, PW, 240, C_BG);
	_tft.drawFastVLine(PX, 0, 240, C_LINE);
	_tft.fillRect(0, 200, 200, 40, TFT_BLACK);
	unlock();
}

void CubeHUD::drawButtons(bool en) {
	if (!touchButtons) return;
	struct Btn {
		int16_t x;
		const char* label;
		uint16_t color;
	};
	const Btn btns[2] = {{BTN1_X, "SCRAMBLE", C_BLU}, {BTN2_X, "SOLVE", C_GRN}};
	lock();
	for (const Btn& b : btns) {
		_tft.fillRoundRect(b.x, BTN_Y, BTN_W, BTN_H, 7, en ? b.color : C_TRACK);
		_tft.setTextSize(2);
		_tft.setTextColor(en ? TFT_BLACK : C_DIM);
		int16_t w = (int16_t)strlen(b.label) * 12 - 2;
		_tft.setCursor(b.x + (BTN_W - w) / 2, BTN_Y + (BTN_H - 14) / 2);
		_tft.print(b.label);
	}
	unlock();
}

void CubeHUD::drawBadge(State s) {
	const char* label;
	uint16_t bg, fg = TFT_BLACK;
	switch (s) {
		case ST_IDLE:
			label = "PRONTO";
			bg = C_BLU;
			fg = TFT_WHITE;
			break;
		case ST_SEARCHING:
			label = "BUSCANDO";
			bg = C_ORG;
			break;
		case ST_SOLVING:
			label = "GIRANDO";
			bg = C_CYN;
			break;
		case ST_MANUAL:
			label = "MANUAL";
			bg = C_PUR;
			break;
		case ST_SOLVED:
			label = "FEITO!";
			bg = C_GRN;
			break;
		default:
			label = "ERRO";
			bg = C_RED;
			fg = TFT_WHITE;
			break;
	}
	lock();
	_tft.fillRoundRect(IX, BADGE_Y, IW, 22, 5, bg);
	_tft.setTextSize(2);
	_tft.setTextColor(fg);
	int16_t w = (int16_t)strlen(label) * 12 - 2;
	_tft.setCursor(IX + (IW - w) / 2, BADGE_Y + 4);
	_tft.print(label);
	unlock();
}

/* Barra: trilho + trecho preenchido [fx, fx+fw). Cada pixel é escrito uma vez só (sem piscar). */
void CubeHUD::drawBar(int16_t fx, int16_t fw, uint16_t col) {
	if (fx == _barFx && fw == _barFw && col == _barCol) return;
	_barFx = fx;
	_barFw = fw;
	_barCol = col;
	lock();
	if (fx > 0) _tft.fillRect(IX, BAR_Y, fx, 8, C_TRACK);
	if (fw > 0) _tft.fillRect(IX + fx, BAR_Y, fw, 8, col);
	int16_t rx = fx + fw;
	if (rx < IW) _tft.fillRect(IX + rx, BAR_Y, IW - rx, 8, C_TRACK);
	unlock();
}

void CubeHUD::parseTokens(const char* s) {
	_ntok = 0;
	while (*s && _ntok < 30) {
		while (*s == ' ') s++;
		if (!*s) break;
		uint8_t k = 0;
		while (*s && *s != ' ') {
			if (k < 2) _tok[_ntok][k++] = *s;
			s++;
		}
		_tok[_ntok][k] = 0;
		_ntok++;
	}
}

// 0 pendente, 1 atual, 2 feito
uint8_t CubeHUD::tokState(uint8_t i, bool prog, int16_t done) const {
	if (!prog) return 0;
	if (i < done) return 2;
	if (i == done) return 1;
	return 0;
}

void CubeHUD::drawToken(uint8_t i, uint8_t st) {
	int16_t x = IX + (i % 6) * CELL_W, y = LIST_Y + (i / 6) * CELL_H;
	lock();
	_tft.fillRect(x - 1, y - 1, 17, 10, st == 1 ? C_YEL : C_BG);
	_tft.setTextSize(1);
	_tft.setTextColor(st == 1 ? TFT_BLACK : (st == 2 ? C_DIM : C_TEXT));
	_tft.setCursor(x + 1, y);
	_tft.print(_tok[i]);
	unlock();
}

void CubeHUD::redrawList(bool prog, int16_t done) {
	lock();
	_tft.fillRect(PX + 1, LIST_Y - 2, PW - 1, 5 * CELL_H + 2, C_BG);
	unlock();
	for (uint8_t i = 0; i < _ntok; i++) drawToken(i, tokState(i, prog, done));
}

uint8_t CubeHUD::stickersOk() const {  // adesivos já na cor da própria face (0..54)
	uint8_t n = 0;
	for (uint8_t f = 0; f < 6; f++)
		for (uint8_t i = 0; i < 9; i++)
			if (_cs.get(f, i) == f) n++;
	return n;
}

const char* CubeHUD::statusText(uint32_t now) const {
	if ((int32_t)(_msgUntil - now) > 0) return _msg;
	switch (_st) {
		case ST_IDLE:
			return _armed ? "Gire ou aperte K1" : "K1 = resolver";
		case ST_SEARCHING:
			return "Procurando...";
		case ST_SOLVING:
			return "Executando solucao";
		case ST_MANUAL:
			return "Resolva o cubo!";
		case ST_SOLVED:
			return _solvedAuto ? "Resolvido (auto)" : "Resolvido (manual)";
		default:
			return _err;
	}
}

/* ============================ laço da UI ============================ */

void CubeHUD::updateUI() {
	const uint32_t now = millis();

	if (_needStatic) {
		drawStatic();
		drawButtons(true);
		_needStatic = false;
	}

	/* --- toque nos botões (só na borda de subida, com 100 ms de "solto" antes) --- */
	if (touchButtons && _touch) {
		int16_t tx, ty;
		if (_touch->read(tx, ty)) {
			if (_released >= 4 && ty >= BTN_Y && ty < BTN_Y + BTN_H) {
				if (tx >= BTN1_X && tx < BTN1_X + BTN_W)
					scramble();
				else if (tx >= BTN2_X && tx < BTN2_X + BTN_W)
					requestSolve();
			}
			_released = 0;
		} else if (_released < 255) {
			_released++;
		}
	}

	/* --- fim de animação: o cubo precisa ficar parado por 60 ms (evita falso positivo) --- */
	if (_cube.busy())
		_idleSince = 0;
	else if (!_idleSince)
		_idleSince = now | 1;
	if (_idleSince && now - _idleSince >= 60 && !_cube.busy()) {
		_scrambleActive = false;
		State s = _st;
		if (s == ST_SOLVING) {
			_tEnd = _idleSince;
			if (_cs.isSolved()) {
				_solvedAuto = true;
				_autoSolves++;
				_st = ST_SOLVED;
				_spinUntil = now + 5000;
				Serial.printf("Resolvido em %lu ms\n", (unsigned long)(_tEnd - _tStart));
			} else {
				setError("Nao resolveu?!");
			}
		} else if (s == ST_MANUAL && _cs.isSolved()) {
			_tEnd = _idleSince;
			uint32_t t = _tEnd - _tStart;
			_solvedAuto = false;
			_manualSolves++;
			if (!_bestMs || t < _bestMs) _bestMs = t;
			_st = ST_SOLVED;
			_spinUntil = now + 5000;
			Serial.printf("Resolvido a mao em %lu ms\n", (unsigned long)t);
		}
	}
	const State cur = _st;

	if (celebrate && (int32_t)(_spinUntil - now) > 0) _cube.rotate(0, 2.0f);

	/* --- botões acinzentados enquanto a entrada está travada --- */
	const bool locked = inputLocked();
	if (locked != _lastLocked) {
		drawButtons(!locked);
		_lastLocked = locked;
	}

	/* --- badge --- */
	if (cur != _lastBadge) {
		drawBadge(cur);
		_lastBadge = cur;
	}

	/* --- cronômetro --- */
	char tbuf[12];
	uint16_t tcol = C_DIM;
	switch (cur) {
		case ST_SEARCHING:
		case ST_SOLVING:
			tcol = C_TEXT;
			break;
		case ST_MANUAL:
			tcol = C_YEL;
			break;
		case ST_SOLVED:
			tcol = C_GRN;
			break;
		case ST_ERROR:
			tcol = C_RED;
			break;
		default:
			break;
	}
	fmtTime(tbuf, sizeof(tbuf), elapsedMs());
	text(1, IX, TIMER_Y, 3, tcol, tbuf, 6);

	/* --- lista de movimentos (cópia segura) --- */
	char snap[160], line[24];
	uint32_t ver = _movesVer;
	if (ver != _lastVer) {
		taskENTER_CRITICAL();
		memcpy(snap, _movesText, sizeof(snap));
		_kind = _movesKind;
		taskEXIT_CRITICAL();
		parseTokens(snap);
		_lastVer = ver;
		_lastDone = -2;  // força redesenho completo
	}
	const bool prog = (_kind == 2) || (_kind == 1 && _scrambleActive);
	const uint8_t pend = _cube.pending();
	int16_t done = 0;
	if (prog && _ntok) done = (pend >= _ntok) ? 0 : (int16_t)(_ntok - pend);

	if (prog != _lastProg || _lastDone == -2) {
		redrawList(prog, done);
		_lastProg = prog;
		_lastDone = done;
	} else if (done != _lastDone) {
		int16_t lo = done < _lastDone ? done : _lastDone;
		int16_t hi = done > _lastDone ? done : _lastDone;
		if (hi >= _ntok) hi = _ntok - 1;
		for (int16_t i = lo; i <= hi; i++) drawToken((uint8_t)i, tokState((uint8_t)i, prog, done));
		_lastDone = done;
	}

	if (_kind == 1)
		snprintf(line, sizeof(line), "Embaralho (%u)", (unsigned)_ntok);
	else if (_kind == 2)
		snprintf(line, sizeof(line), "Solucao (%u)", (unsigned)_ntok);
	else
		line[0] = 0;
	text(4, IX, LABEL_Y, 1, _kind == 2 ? C_GRN : C_CYN, line, 18);

	/* --- giro atual (grande) + contador --- */
	char big[4] = "";
	if (cur == ST_SEARCHING) {
		static const char sp[4] = {'|', '/', '-', '\\'};
		big[0] = sp[(now / 100) & 3];
		big[1] = 0;
	} else if (prog && done < _ntok) {
		strcpy(big, _tok[done]);
	}
	text(2, IX, BIG_Y, 4, C_YEL, big, 2);

	char cnt[8] = "";
	if (prog && _ntok) snprintf(cnt, sizeof(cnt), "%d/%u", (int)done, (unsigned)_ntok);
	text(3, 256, BIG_Y + 6, 2, C_TEXT, cnt, 5);

	/* --- barra de progresso --- */
	{
		int16_t fx = 0, fw = 0;
		uint16_t bcol = C_BLU;
		switch (cur) {
			case ST_SEARCHING: {  // barra "indeterminada" que vai e volta
				int16_t t = (int16_t)((now / 12) % 156);
				fx = t < 78 ? t : 156 - t;
				fw = 30;
				bcol = C_ORG;
				break;
			}
			case ST_SOLVING:
				fw = _ntok ? (int16_t)(IW * done / _ntok) : 0;
				bcol = C_CYN;
				break;
			case ST_SOLVED:
				fw = IW;
				bcol = C_GRN;
				break;
			case ST_ERROR:
				fw = IW;
				bcol = C_RED;
				break;
			case ST_MANUAL:
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
		if ((int32_t)(_msgUntil - now) > 0)
			c = C_YEL;
		else if (cur == ST_SOLVED)
			c = C_GRN;
		else if (cur == ST_ERROR)
			c = C_RED;
		text(0, IX, LINE_Y, 1, c, statusText(now), 18);
	}

	/* --- estatísticas --- */
	char b[24], t2[12];
	if (_lastSearchMs)
		snprintf(b, sizeof(b), "Busca: %lu.%lu s", (unsigned long)(_lastSearchMs / 1000),
		         (unsigned long)((_lastSearchMs / 100) % 10));
	else
		strcpy(b, "Busca: --");
	text(5, IX, STATS_Y, 1, C_DIM, b, 18);

	if (_lastNodes)
		snprintf(b, sizeof(b), "Nos: %lu", (unsigned long)_lastNodes);
	else
		strcpy(b, "Nos: --");
	text(6, IX, STATS_Y + 9, 1, C_DIM, b, 18);

	snprintf(b, sizeof(b), "Face %c  Giros: %u", "URFDLB"[_face % 6], (unsigned)_manualMoves);
	text(7, IX, STATS_Y + 18, 1, C_TEXT, b, 18);

	if (_bestMs) {
		fmtTime(t2, sizeof(t2), _bestMs);
		snprintf(b, sizeof(b), "Recorde: %s", t2);
	} else {
		strcpy(b, "Recorde: --");
	}
	text(8, IX, STATS_Y + 27, 1, C_YEL, b, 18);

	snprintf(b, sizeof(b), "Auto:%u  Manual:%u", (unsigned)_autoSolves, (unsigned)_manualSolves);
	text(9, IX, STATS_Y + 36, 1, C_DIM, b, 18);
}