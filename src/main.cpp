#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <stdlib.h>

#include "Coords.h"
#include "CubeHUD.h"
#include "CubeSolver.h"
#include "CubeState.h"
#include "CubeView.h"
#include "DebugScreen.h"
#include "HudScreen.h"
#include "InputManager.h"
#include "Metrics.h"
#include "PruneTable.h"
#include "Screen.h"
#include "TFT_FSMC.h"
#include "TouchXPT2046.h"
#include "TreeScreen.h"
#include "W25Q16.h"
#include "stm32f4ve_peripherals.h"

/* Autotestes no boot (poda + solver). Ponha 1 para ligar. */
#define RUN_SELFTESTS 1

TFT_FSMC tft;
TouchXPT2046 touch;
InputManager input(&touch);

W25Q16 flash;

CubeState cubeState;
CubeView cube(tft, cubeState);
CubeSolver solver(flash);
CubeHUD hud(tft, cube, cubeState, solver);

ScreenManager screens;
DebugScreen debugScreen(tft, input);
HudScreen hudScreen(cube, hud);
TreeScreen treeScreen(tft, hud, screens, 0);

static TaskHandle_t hDisplay, hSolve, hIn, hDisp, hLed, hMet;

#if RUN_SELFTESTS
static void selfTests() {
	srand(12345);  // seed fixa: mesmo baseline em toda execução

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

	// 2) Solver: mesmos parâmetros do HUD (embaralho de 30, maxDepth 26, timeout 60 s, refino 1500 ms)
	{
		const int N = 5;
		int okCount = 0;
		uint32_t sumMs = 0, sumLen = 0;
		for (int n = 0; n < N; n++) {
			Serial.printf("Solver teste %d\n", n + 1);
			CubeState s;
			int last = -1;
			for (int i = 0; i < 30; i++) {  // igual ao HUD: sem repetir a face anterior
				int f;
				do { f = rand() % 6; } while (f == last);
				last = f;
				s.applyMove(f, 1 + rand() % 3);
			}
			char sol[128];
			uint32_t t0 = millis();
			int len = solver.solve(s, sol, sizeof(sol), 26, 60000, 1500);
			uint32_t dt = millis() - t0;
			if (len >= 0) s.applyMoves(sol);
			bool solved = s.isSolved();
			Serial.printf("len=%d  %lu ms  %lu nos  resolvido=%d\n  %s\n", len, (unsigned long)dt,
			              (unsigned long)solver.nodes(), solved, len >= 0 ? sol : "");
			metricsPrintSolve(Serial);
			if (len >= 0 && solved) {
				okCount++;
				sumMs += dt;
				sumLen += len;
			}
		}
		if (okCount)
			Serial.printf("RESUMO: %d/%d resolvidos, media %lu giros, %lu ms\n", okCount, N,
			              (unsigned long)(sumLen / okCount), (unsigned long)(sumMs / okCount));
		else
			Serial.printf("RESUMO: 0/%d resolvidos\n", N);
	}

	// 3) verify(): canto torcido => solve() deve dar -1 com verifyError() = -5
	{
		CubeState bad;
		bad.set(FACE_U, 8, FACE_R);
		bad.set(FACE_R, 0, FACE_F);
		bad.set(FACE_F, 2, FACE_U);
		char sol[16];
		int r = solver.solve(bad, sol, sizeof(sol));
		Serial.printf("verify: r=%d err=%d (esperado -1 / -5)\n", r, solver.verifyError());
	}
}
#endif

// ---- Calibração do touch (segure K0 ao ligar) ----
static void drawTarget(int16_t x, int16_t y) {
	tft.fillScreen(TFT_BLACK);
	tft.setTextSize(2);
	tft.setTextColor(TFT_WHITE);
	tft.setCursor(82, 110);
	tft.print("Toque no alvo");
	tft.drawCircle(x, y, 10, TFT_RED);
	tft.drawFastHLine(x - 14, y, 29, TFT_RED);
	tft.drawFastVLine(x, y - 14, 29, TFT_RED);
}

static void waitTouchRaw(uint16_t& rx, uint16_t& ry) {
	uint16_t x = 0, y = 0;
	while (!touch.readRaw(x, y)) delay(10);
	delay(150);  // deixa o dedo assentar
	uint32_t sx = 0, sy = 0;
	uint8_t n = 0;
	for (uint8_t i = 0; i < 10; i++) {
		if (touch.readRaw(x, y)) {
			sx += x;
			sy += y;
			n++;
		}
		delay(10);
	}
	rx = n ? sx / n : x;
	ry = n ? sy / n : y;
	uint8_t freeCnt = 0;  // espera soltar (200 ms sem toque)
	while (freeCnt < 20) {
		freeCnt = touch.readRaw(x, y) ? 0 : freeCnt + 1;
		delay(10);
	}
}

static void calibrateTouch() {
	const int16_t W = tft.width(), H = tft.height(), M = 20;
	const int16_t px[3] = {M, (int16_t)(W - 1 - M), (int16_t)(W - 1 - M)};  // topo-esq, topo-dir, baixo-dir
	const int16_t py[3] = {M, M, (int16_t)(H - 1 - M)};
	int32_t r[3][2];
	for (uint8_t i = 0; i < 3; i++) {
		drawTarget(px[i], py[i]);
		uint16_t x, y;
		waitTouchRaw(x, y);
		r[i][0] = x;
		r[i][1] = y;
	}

	// P0->P1 só muda o X da tela; P1->P2 só muda o Y da tela
	bool swap = abs(r[1][0] - r[0][0]) < abs(r[1][1] - r[0][1]);
	int sx = swap ? 1 : 0, sy = swap ? 0 : 1;  // canal bruto que vira X / Y da tela
	int32_t x0 = r[0][sx], x1 = r[1][sx];
	int32_t y1 = r[1][sy], y2 = r[2][sy];
	bool invX = x1 < x0, invY = y2 < y1;
	int32_t xa = x0 < x1 ? x0 : x1, xb = x0 < x1 ? x1 : x0;
	int32_t ya = y1 < y2 ? y1 : y2, yb = y1 < y2 ? y2 : y1;
	int32_t ex = (xb - xa) * M / (W - 1 - 2 * M);  // extrapola do alvo até a borda da tela
	int32_t ey = (yb - ya) * M / (H - 1 - 2 * M);
	uint16_t xMin = constrain(xa - ex, 0, 4095), xMax = constrain(xb + ex, 0, 4095);
	uint16_t yMin = constrain(ya - ey, 0, 4095), yMax = constrain(yb + ey, 0, 4095);

	touch.setCalibration(xMin, xMax, yMin, yMax, swap, invX, invY);
	Serial.printf("touch.setCalibration(%u, %u, %u, %u, %s, %s, %s);\n", xMin, xMax, yMin, yMax,
	              swap ? "true" : "false", invX ? "true" : "false", invY ? "true" : "false");
	tft.fillScreen(TFT_BLACK);
}

static void handleInput(const InputEvent& e) {
	if (e.type == IN_K0_LONG) {  // troca de tela (global)
		if (!screens.next()) hud.notify("Aguarde...");
		return;
	}
	screens.onInput(e);
}

void taskDispatch(void*) {
	InputEvent e;
	for (;;)
		if (input.next(e)) handleInput(e);
}

#if METRICS_ENABLED
void taskMetrics(void*) {
	for (;;) {
		if (digitalRead(BTN_WKUP) == HIGH) {  // WKUP liga em 3V3
			metricsPrintRender(Serial);
			metricsPrintSolve(Serial);
			solverTracePrintCsv(Serial);
			metricsResetRender();  // próxima leitura = nova janela
			Serial.printf("[M] pilha livre minima (words): display=%u solve=%u in=%u disp=%u\n",
			              (unsigned)uxTaskGetStackHighWaterMark(hDisplay),
			              (unsigned)uxTaskGetStackHighWaterMark(hSolve), (unsigned)uxTaskGetStackHighWaterMark(hIn),
			              (unsigned)uxTaskGetStackHighWaterMark(hDisp));
			vTaskDelay(pdMS_TO_TICKS(500));
		}
		vTaskDelay(pdMS_TO_TICKS(50));
	}
}
#endif

void taskLed(void*) {
	pinMode(LED_D2, OUTPUT);

	for (;;) {
		digitalToggle(LED_D2);
		vTaskDelay(pdMS_TO_TICKS(250));
	}
}

void setup() {
	Serial.begin(115200);
	metricsInit();
	Serial.printf("SYSCLK: %lu Hz\n", (unsigned long)SystemCoreClock);

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
	cube.setAnimation(true, 150);

	pinMode(BTN_K0, INPUT_PULLUP);
	pinMode(BTN_K1, INPUT_PULLUP);
	pinMode(BTN_WKUP, INPUT_PULLDOWN);

	touch.begin(tft.width(), tft.height());
	touch.setCalibration(205, 3954, 230, 3870, true, true, true);
	if (digitalRead(BTN_K0) == LOW) calibrateTouch();  // segure K0 ao ligar

	// hud.touchButtons = false;  // desliga os botões na tela
	// hud.celebrate = false;     // desliga o giro de comemoração
	hud.scrambleLen = 30;  // tamanho do embaralhamento
	hud.begin();
	cube.alignCameraToFace(FACE_U);  // era feito no início da taskRender

	// Telas
	screens.add(&hudScreen);
	screens.add(&debugScreen);
	screens.add(&treeScreen);

	hudScreen.setAutoTree(&screens, 2);  // 2 = índice da TreeScreen

	debugScreen.addTask("display", &hDisplay, 768);
	debugScreen.addTask("solve", &hSolve, 768);
	debugScreen.addTask("input", &hIn, 192);
	debugScreen.addTask("dispatch", &hDisp, 512);
	debugScreen.addTask("led", &hLed, 128);
	debugScreen.addTask("metrics", &hMet, 512);

	input.begin();

	xTaskCreate(taskLed, "led", 128, NULL, 1, &hLed);
	xTaskCreate(ScreenManager::task, "display", 768, &screens, 1, &hDisplay);
	xTaskCreate(CubeHUD::solveTask, "solve", 768, &hud, 1, &hSolve);
	xTaskCreate(InputManager::task, "input", 192, &input, 3, &hIn);
	xTaskCreate(taskDispatch, "dispatch", 512, NULL, 2, &hDisp);
#if METRICS_ENABLED
	if (xTaskCreate(taskMetrics, "metrics", 512, NULL, 1, &hMet) != pdPASS) {
		Serial.println("Sem heap para taskMetrics");
	}
#endif

	vTaskStartScheduler();
}

void loop() {
	// Não usado: todas as tarefas rodam no FreeRTOS
}