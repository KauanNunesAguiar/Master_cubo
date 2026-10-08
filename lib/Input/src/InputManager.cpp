// InputManager.cpp
#include "InputManager.h"

bool InputManager::begin() {
	_q = xQueueCreate(16, sizeof(InputEvent));

	pinMode(ENCODER_CLK, INPUT_PULLUP);
	pinMode(ENCODER_DT, INPUT_PULLUP);
	pinMode(ENCODER_SW, INPUT_PULLUP);
	pinMode(BTN_K0, INPUT_PULLUP);
	pinMode(BTN_K1, INPUT_PULLUP);

	_enc.pin = ENCODER_SW;
	_k0.pin = BTN_K0;
	_k1.pin = BTN_K1;  // todos ativos em nível baixo (activeLow = true)
	_lastClk = digitalRead(ENCODER_CLK);
	return _q != nullptr;
}

void InputManager::post(uint8_t type, int16_t a, int16_t b) {
	InputEvent e = {type, a, b};
	if (xQueueSend(_q, &e, 0) != pdPASS) _dropped++;
}

bool InputManager::next(InputEvent& e, TickType_t wait) { return xQueueReceive(_q, &e, wait) == pdPASS; }

int8_t InputManager::update(Btn& b) {
	bool pressed = (digitalRead(b.pin) == (b.activeLow ? LOW : HIGH));
	if (pressed == b.state) {
		b.cnt = 0;
		return 0;
	}
	if (++b.cnt < DEBOUNCE_TICKS) return 0;
	b.cnt = 0;
	b.state = pressed;
	return pressed ? 1 : -1;
}

void InputManager::poll() {
	const uint32_t now = millis();

	// encoder (rotação)
	bool clk = digitalRead(ENCODER_CLK);
	if (clk != _lastClk && clk == LOW) post(IN_ENC_ROT, digitalRead(ENCODER_DT) ? 1 : -1);
	_lastClk = clk;

	// botão do encoder: clique curto ao soltar, ou longo assim que passa de LONG_MS
	int8_t e = update(_enc);
	if (e > 0) {
		_enc.t0 = now;
		_enc.longSent = false;
	} else if (e < 0 && !_enc.longSent) {
		post(IN_ENC_CLICK);
	}
	if (_enc.state && !_enc.longSent && now - _enc.t0 >= LONG_MS) {
		_enc.longSent = true;
		post(IN_ENC_LONG);
	}

	// K0 / K1
	if (update(_k0) > 0) post(IN_K0);
	if (update(_k1) > 0) post(IN_K1);

	// touch: a cada 20 ms; só aceita novo toque após 100 ms solto
	if (_touch && ++_touchTick >= 10) {
		_touchTick = 0;
		int16_t x, y;
		if (_touch->read(x, y)) {
			if (_touchRel >= 5) post(IN_TOUCH, x, y);
			_touchRel = 0;
		} else if (_touchRel < 255) {
			_touchRel++;
		}
	}
}

void InputManager::task(void* self) {
	InputManager* m = (InputManager*)self;
	for (;;) {
		m->poll();
		vTaskDelay(pdMS_TO_TICKS(2));
	}
}