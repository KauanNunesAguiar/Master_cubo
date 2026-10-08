#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <stdlib.h>

#include "Coords.h"
#include "CubeHUD.h"
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

/* Autotestes no boot (poda + solver). Ponha 1 para ligar. */
#define RUN_SELFTESTS 1

TouchXPT2046 touch;
TFT_FSMC tft;
SemaphoreHandle_t tftMutex;

CubeState cubeState;
CubeView cube(tft, cubeState);

W25Q16 flash;
CubeSolver solver(flash);

CubeHUD hud(tft, cube, cubeState, solver, &touch);

bool lastClkState = HIGH;

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

void taskRender(void*) {
	cube.alignCameraToFace(FACE_U);

	for (;;) {
		xSemaphoreTake(tftMutex, portMAX_DELAY);
		cube.update();
		xSemaphoreGive(tftMutex);

		vTaskDelay(pdMS_TO_TICKS(20));  // ~50 FPS
	}
}

void taskEncoder(void*) {
	pinMode(ENCODER_CLK, INPUT_PULLUP);
	pinMode(ENCODER_DT, INPUT_PULLUP);
	pinMode(ENCODER_SW, INPUT_PULLUP);

	lastClkState = digitalRead(ENCODER_CLK);

	for (;;) {
		bool currentClk = digitalRead(ENCODER_CLK);
		if (currentClk != lastClkState && currentClk == LOW) {
			if (digitalRead(ENCODER_DT))
				cube.rotate(0, 15.0f);
			else
				cube.rotate(0, -15.0f);
		}
		lastClkState = currentClk;

		if (digitalRead(ENCODER_SW) == LOW) {
			hud.scramble();
			vTaskDelay(pdMS_TO_TICKS(400));  // Debounce
		}

		vTaskDelay(pdMS_TO_TICKS(2));
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

	pinMode(BTN_K0, INPUT_PULLUP);
	pinMode(BTN_K1, INPUT_PULLUP);
	pinMode(BTN_WKUP, INPUT_PULLDOWN);

	touch.begin(tft.width(), tft.height());
	touch.setCalibration(205, 3954, 230, 3870, true, true, true);
	if (digitalRead(BTN_K0) == LOW) calibrateTouch();  // segure K0 ao ligar

	tftMutex = xSemaphoreCreateMutex();

	// hud.touchButtons = false;  // desliga os botões na tela
	// hud.celebrate = false;     // desliga o giro de comemoração
	hud.scrambleLen = 30;  // tamanho do embaralhamento
	hud.begin(tftMutex);

	xTaskCreate(taskLed, "led", 256, NULL, 1, NULL);
	xTaskCreate(CubeHUD::uiTask, "ui", 1024, &hud, 2, NULL);
	xTaskCreate(CubeHUD::solveTask, "solve", 1536, &hud, 1, NULL);
	xTaskCreate(taskRender, "render", 2048, NULL, 1, NULL);
	xTaskCreate(taskEncoder, "encoder", 256, NULL, 3, NULL);

	vTaskStartScheduler();
}

void loop() {
	// Não usado: todas as tarefas rodam no FreeRTOS
}