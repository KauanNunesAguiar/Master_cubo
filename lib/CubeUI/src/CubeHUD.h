// CubeHUD.h
// Interface completa do cubo mágico: painel lateral (status, cronômetro, barra, lista de giros,
// estatísticas), botões de toque, embaralhar, "resolver" em segundo plano e cronômetro manual.
//
// Uso (resumo):
//   CubeHUD hud(tft, cube, cubeState, solver, &touch);
//   setup():  hud.begin(tftMutex);
//             xTaskCreate(CubeHUD::uiTask,    "ui",    1024, &hud, 2, NULL);
//             xTaskCreate(CubeHUD::solveTask, "solve", 1536, &hud, 1, NULL);
//   Entradas: hud.requestSolve(), hud.scramble(), hud.selectFace(f), hud.turnSelected(cw)
//
// Regras:
//  - Todo desenho no TFT é feito com o mutex passado em begin() (o mesmo da tarefa de render).
//  - O CubeView precisa ter setOrigin() e pending() (ver patch).
//  - Os textos não têm acento: a fonte padrão do Adafruit_GFX não tem.
#pragma once
#include <Arduino.h>
#include <STM32FreeRTOS.h>

#include "CubeSolver.h"
#include "CubeState.h"
#include "CubeView.h"
#include "TFT_FSMC.h"

class CubeHUD {
   public:
	enum State : uint8_t { ST_IDLE, ST_SEARCHING, ST_SOLVING, ST_MANUAL, ST_SOLVED, ST_ERROR };

	CubeHUD(TFT_FSMC& tft, CubeView& cube, CubeState& state, CubeSolver& solver);

	/* --- Configuração (mude antes de begin()) --- */
	bool touchButtons = true;  // botões MISTURA / RESOLVE na tela
	bool celebrate = true;     // cubo gira sozinho por 5 s quando é resolvido
	uint8_t scrambleLen = 20;  // tamanho do embaralhamento (máx. 30)

	/* Chamar depois de tft.begin() e cube.begin(). Posiciona o cubo à esquerda (setOrigin). */
	void begin();

	/* --- Tarefas FreeRTOS: passe o ponteiro do HUD como parâmetro --- */
	static void solveTask(void* hud);  // procura a solução (prioridade 1, pilha 1536 words)

	/* ...ou, se preferir, chame você mesmo em loop: */
	void updateUI();    // a cada ~25 ms
	void solveStep();   // a cada ~20 ms
	void invalidate();  // força redesenho completo (ao voltar para esta tela)

	/* --- Ações (podem ser chamadas de qualquer tarefa) --- */
	void requestSolve();                // K1 / botão RESOLVE: o cronômetro conta a partir daqui
	bool scramble(uint8_t n = 0);       // 0 = usa scrambleLen; false se travado ou fila cheia
	void selectFace(uint8_t face);      // face do encoder (também alinha a câmera)
	bool turnSelected(bool clockwise);  // gira a face selecionada (liga o cronômetro manual se armado)
	bool turn(uint8_t face, uint8_t turns);
	void onTouch(int16_t x, int16_t y);         // chamado pelo dispatcher (hit-test dos botões na tela)
	void notify(const char* m) { showMsg(m); }  // mensagem temporária de 2,5 s na linha de status

	/* --- Consulta --- */
	bool inputLocked() const;  // true durante busca / execução / embaralhamento
	State state() const { return _st; }
	uint8_t face() const { return _face; }
	uint32_t elapsedMs() const;  // valor mostrado no cronômetro
	uint32_t bestManualMs() const { return _bestMs; }
	uint32_t lastSearchMs() const { return _lastSearchMs; }

   private:
	TFT_FSMC& _tft;
	CubeView& _cube;
	CubeState& _cs;
	CubeSolver& _solver;

	/* estado compartilhado entre tarefas */
	volatile State _st = ST_IDLE;
	volatile uint32_t _tStart = 0, _tEnd = 0, _clickMs = 0, _msgUntil = 0, _movesVer = 0;
	volatile bool _solveReq = false, _solving = false, _armed = false, _scrambleActive = false, _solvedAuto = false;
	volatile uint8_t _movesKind = 0;  // 0 nada, 1 embaralhamento, 2 solução
	char _movesText[160] = "";
	char _msg[24] = "", _err[24] = "";
	char _sol[128];
	uint8_t _face = FACE_F;
	uint32_t _lastSearchMs = 0, _lastNodes = 0, _bestMs = 0;
	uint16_t _autoSolves = 0, _manualSolves = 0, _manualMoves = 0;

	/* estado só da tarefa de UI */
	struct TxtCache {
		char s[24];
		uint16_t fg;
	};
	TxtCache _tc[10];
	char _tok[32][3];
	uint8_t _ntok = 0, _kind = 0;
	State _lastBadge = (State)255;
	bool _lastLocked = false, _lastProg = false, _needStatic = true;
	uint32_t _lastVer = 0xFFFFFFFF, _idleSince = 0, _spinUntil = 0;
	int16_t _lastDone = -2, _barFx = -1, _barFw = -1;
	uint16_t _barCol = 0;

	void showMsg(const char* m);
	void setError(const char* m);
	void setMoves(const char* s, uint8_t kind);
	void onManualMove();
	const char* statusText(uint32_t now) const;
	uint8_t stickersOk() const;

	void lock() {}
	void unlock() {}
	void text(uint8_t slot, int16_t x, int16_t y, uint8_t size, uint16_t fg, const char* s, uint8_t width);
	void drawStatic();
	void drawButtons(bool enabled);
	void drawBadge(State s);
	void drawBar(int16_t fx, int16_t fw, uint16_t col);
	void parseTokens(const char* s);
	void drawToken(uint8_t i, uint8_t st);
	void redrawList(bool prog, int16_t done);
	uint8_t tokState(uint8_t i, bool prog, int16_t done) const;
};