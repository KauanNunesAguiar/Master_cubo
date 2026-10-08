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

/* ================================================================= *
 * 2. BOTÕES DE SELEÇÃO DE FACES (U, R, F, D, L, B)                 *
 * ================================================================= */
#define BTN_FACE_U PC3  // Branco
#define BTN_FACE_R PA3  // Vermelho
#define BTN_FACE_F PC1  // Verde
#define BTN_FACE_D PA2  // Amarelo
#define BTN_FACE_L PC0  // Laranja
#define BTN_FACE_B PC2  // Azul

/* Autotestes no boot (poda + solver). Ponha 1 para ligar. */
#define RUN_SELFTESTS 0

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

void taskRender(void*) {
	cube.alignCameraToFace(FACE_U);

	for (;;) {
		xSemaphoreTake(tftMutex, portMAX_DELAY);
		cube.update();
		xSemaphoreGive(tftMutex);

		vTaskDelay(pdMS_TO_TICKS(20));  // ~50 FPS
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
		if (k1 && !lastK1) hud.requestSolve();
		lastK1 = k1;

		vTaskDelay(pdMS_TO_TICKS(30));
	}
}

// Encoder KY-040 por polling: girar = gira a face selecionada, clicar = embaralha
void taskEncoder(void*) {
	pinMode(ENCODER_CLK, INPUT_PULLUP);
	pinMode(ENCODER_DT, INPUT_PULLUP);
	pinMode(ENCODER_SW, INPUT_PULLUP);

	lastClkState = digitalRead(ENCODER_CLK);

	for (;;) {
		bool currentClk = digitalRead(ENCODER_CLK);
		if (currentClk != lastClkState && currentClk == LOW) {
			hud.turnSelected(digitalRead(ENCODER_DT) == HIGH);  // HIGH = horário
		}
		lastClkState = currentClk;

		if (digitalRead(ENCODER_SW) == LOW) {
			hud.scramble();
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
				hud.selectFace(i);
				Serial.printf("Face selecionada: %d\n", i);
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

	pinMode(BTN_K0, INPUT_PULLUP);
	pinMode(BTN_K1, INPUT_PULLUP);
	pinMode(BTN_WKUP, INPUT_PULLDOWN);

	touch.begin(tft.width(), tft.height());
	touch.setCalibration(200, 3900, 200, 3900, true, false, false);

	tftMutex = xSemaphoreCreateMutex();

	// hud.touchButtons = false;  // desliga os botões na tela
	// hud.celebrate = false;     // desliga o giro de comemoração
	hud.scrambleLen = 30;  // tamanho do embaralhamento
	hud.begin(tftMutex);

	xTaskCreate(taskLed, "led", 256, NULL, 1, NULL);
	xTaskCreate(CubeHUD::uiTask, "ui", 1024, &hud, 2, NULL);
	xTaskCreate(CubeHUD::solveTask, "solve", 1536, &hud, 1, NULL);
	xTaskCreate(taskRender, "render", 2048, NULL, 1, NULL);
	xTaskCreate(taskButtons, "buttons", 256, NULL, 2, NULL);
	xTaskCreate(taskEncoder, "encoder", 256, NULL, 3, NULL);
	xTaskCreate(taskFaceButtons, "facebtns", 256, NULL, 2, NULL);

	vTaskStartScheduler();
}

void loop() {
	// Não usado: todas as tarefas rodam no FreeRTOS
}