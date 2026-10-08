// Screen.h — interface de tela + gerenciador. Só a task "display" desenha no TFT.
#pragma once
#include <Arduino.h>
#include <STM32FreeRTOS.h>

#include "InputManager.h"

class Screen {
   public:
	virtual ~Screen() {}
	virtual void onEnter() {}  // na task de display, ao entrar: redesenhe tudo
	virtual void onExit() {}
	virtual void update(uint32_t now) = 0;                     // lógica + desenho (~a cada 20 ms)
	virtual bool onInput(const InputEvent&) { return false; }  // chamada pelo dispatcher
	virtual bool canLeave() { return true; }                   // false = bloqueia a troca de tela
};

class ScreenManager {
   public:
	static const uint8_t MAX = 4;

	bool add(Screen* s) {
		if (_n >= MAX) return false;
		_s[_n++] = s;
		return true;
	}
	bool next() {  // false se não deu para trocar (tela atual bloqueou)
		if (_n < 2) return false;
		uint8_t c = _cur;
		if (c < _n && !_s[c]->canLeave()) return false;
		_want = (_want + 1) % _n;
		return true;  // a troca de fato acontece dentro de tick()
	}
	void onInput(const InputEvent& e) {
		uint8_t c = _cur;
		if (c < _n) _s[c]->onInput(e);
	}
	void tick(uint32_t now) {
		if (!_n) return;
		if (_want != _cur) {
			if (_cur < _n) _s[_cur]->onExit();
			_cur = _want;
			_s[_cur]->onEnter();
		}
		_s[_cur]->update(now);
	}
	void request(uint8_t idx) {  // troca direta (usada pelas próprias telas); ignora canLeave()
		if (idx < _n) _want = idx;
	}
	static void task(void* self) {
		ScreenManager* m = (ScreenManager*)self;
		for (;;) {
			m->tick(millis());
			vTaskDelay(pdMS_TO_TICKS(20));
		}
	}

   private:
	Screen* _s[MAX];
	uint8_t _n = 0;
	volatile uint8_t _cur = 255, _want = 0;
};