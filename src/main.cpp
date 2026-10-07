#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <stdlib.h>
#include <string.h>

#include "Coords.h"
#include "CubeState.h"
#include "CubeView.h"
#include "CubieCube.h"
#include "TFT_FSMC.h"
#include "TouchXPT2046.h"
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

TouchXPT2046 touch;
TFT_FSMC tft;
SemaphoreHandle_t tftMutex;

CubeState cubeState;
CubeView cube(tft, cubeState);  // Corrigido: injetando o CubeState

// Estado atual do controle
uint8_t selectedFace = FACE_F;  // Face padrão inicial (Front)
volatile int encoderDelta = 0;
bool lastClkState = HIGH;

static bool inP2(uint8_t m) {
	for (uint8_t i = 0; i < N_MOVES_P2; i++)
		if (P2_MOVES[i] == m) return true;
	return false;
}

static bool testCoord(CoordGet get, CoordSet set, uint16_t n, bool p2) {
	CubieCube s;
	if (get(s) != 0) return false;
	for (int it = 0; it < 300; it++) {
		CubieCube c;
		for (int k = 0; k < 30; k++) c.multiply(moveCubie(p2 ? P2_MOVES[rand() % N_MOVES_P2] : rand() % N_MOVES));
		uint16_t v = get(c);
		if (v >= n) return false;
		CubieCube d;
		set(d, v);
		if (get(d) != v) return false;  // ida e volta
		for (uint8_t m = 0; m < N_MOVES; m++) {
			if (p2 && !inP2(m)) continue;
			CubieCube a = c, b = d;
			a.multiply(moveCubie(m));
			b.multiply(moveCubie(m));
			if (get(a) != get(b)) return false;  // é o que a tabela de movimento assume
		}
	}
	return true;
}

void taskTouch(void*) {
	int16_t x, y;
	for (;;) {
		if (touch.read(x, y)) {
			xSemaphoreTake(tftMutex, portMAX_DELAY);
			tft.fillCircle(x, y, 3, TFT_WHITE);
			xSemaphoreGive(tftMutex);
			Serial.printf("raw=%u,%u  x=%d y=%d\n", touch.rawX, touch.rawY, x, y);
		}
		vTaskDelay(pdMS_TO_TICKS(10));
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
void taskButtons(void*) {
	int8_t dir = 1;
	bool lastWk = false;
	for (;;) {
		bool wk = digitalRead(BTN_WKUP) == HIGH;
		if (wk && !lastWk) dir = -dir;  // WKUP inverte o sentido
		lastWk = wk;

		if (digitalRead(BTN_K0) == LOW) cube.rotate(0, 4.0f * dir);  // K0: eixo Y
		if (digitalRead(BTN_K1) == LOW) cube.rotate(4.0f * dir, 0);  // K1: eixo X
		vTaskDelay(pdMS_TO_TICKS(30));
	}
}

// Tarefa para ler o Encoder KY-040 por interrupção ou polling otimizado
void taskEncoder(void*) {
	pinMode(ENCODER_CLK, INPUT_PULLUP);
	pinMode(ENCODER_DT, INPUT_PULLUP);
	pinMode(ENCODER_SW, INPUT_PULLUP);

	lastClkState = digitalRead(ENCODER_CLK);

	for (;;) {
		bool currentClk = digitalRead(ENCODER_CLK);
		if (currentClk != lastClkState && currentClk == LOW) {
			if (digitalRead(ENCODER_DT) == HIGH) {
				cube.move(selectedFace, 1);  // Horário
			} else {
				cube.move(selectedFace, 3);  // Anti-horário
			}
		}
		lastClkState = currentClk;

		// --- CLIQUE DO BOTÃO DO ENCODER EMBARALHA O CUBO ---
		if (digitalRead(ENCODER_SW) == LOW) {
			srand(millis());
			cube.scramble(20);  // Embaralha com 20 movimentos aleatórios
			Serial.println("Cubo embaralhado!");
			vTaskDelay(pdMS_TO_TICKS(400));  // Debounce robusto para evitar múltiplos cliques seguidos
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

	CubeState::init();

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

	xTaskCreate(taskLed, "led", 256, NULL, 1, NULL);
	xTaskCreate(taskTouch, "touch", 1024, NULL, 2, NULL);
	xTaskCreate(taskRender, "render", 2048, NULL, 1, NULL);
	xTaskCreate(taskButtons, "buttons", 256, NULL, 2, NULL);
	xTaskCreate(taskEncoder, "encoder", 256, NULL, 3, NULL);
	xTaskCreate(taskFaceButtons, "facebtns", 256, NULL, 2, NULL);

	vTaskStartScheduler();
}

void loop() {
	// Não usado: todas as tarefas rodam no FreeRTOS
}