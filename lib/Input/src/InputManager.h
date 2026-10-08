// InputManager.h — camada de entrada: encoder, botões e touch viram eventos numa fila.
// Quem consome os eventos não sabe nada de pinos nem de SPI.
#pragma once
#include <Arduino.h>
#include <STM32FreeRTOS.h>

#include "TouchXPT2046.h"
#include "stm32f4ve_peripherals.h"

#ifndef ENCODER_CLK
#define ENCODER_CLK PA4
#define ENCODER_DT PA5
#define ENCODER_SW PA7
#endif

enum InputType : uint8_t {
	IN_NONE,
	IN_ENC_ROT,    // a = +1 (DT alto na borda de descida do CLK) ou -1
	IN_ENC_CLICK,  // clique curto (dispara ao soltar)
	IN_ENC_LONG,   // segurou ~0,7 s (dispara uma vez, sem esperar soltar)
	IN_K0,         // K0 pressionado
	IN_K1,         // K1 pressionado
	IN_TOUCH       // a = x, b = y (só na borda de subida)
};

struct InputEvent {
	uint8_t type;
	int16_t a, b;
};

class InputManager {
   public:
	explicit InputManager(TouchXPT2046* touch = nullptr) : _touch(touch) {}

	bool begin();                  // cria a fila e configura os pinos (antes de iniciar o scheduler)
	static void task(void* self);  // tarefa de polling (2 ms)
	void poll();                   // uma passada; chamada pela task
	bool next(InputEvent& e, TickType_t wait = portMAX_DELAY);  // consumidor
	uint32_t dropped() const { return _dropped; }               // eventos perdidos (fila cheia)

   private:
	struct Btn {
		uint8_t pin = 0;
		bool activeLow = true;
		bool state = false;  // true = pressionado (já filtrado)
		uint8_t cnt = 0;
		uint32_t t0 = 0;
		bool longSent = false;
	};
	static const uint8_t DEBOUNCE_TICKS = 8;  // 8 x 2 ms
	static const uint32_t LONG_MS = 700;

	TouchXPT2046* _touch;
	QueueHandle_t _q = nullptr;
	Btn _enc, _k0, _k1;
	bool _lastClk = true;
	uint8_t _touchTick = 0, _touchRel = 10;
	uint32_t _dropped = 0;

	void post(uint8_t type, int16_t a = 0, int16_t b = 0);
	int8_t update(Btn& b);  // +1 apertou, -1 soltou, 0 nada
};