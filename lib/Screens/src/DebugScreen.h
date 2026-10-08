// DebugScreen.h — stub da tela de debug (Etapa 5)
#pragma once
#include "Screen.h"
#include "TFT_FSMC.h"

class DebugScreen : public Screen {
   public:
	explicit DebugScreen(TFT_FSMC& tft) : _tft(tft) {}

	void onEnter() override {
		_tft.fillScreen(TFT_BLACK);
		_last = 0;
	}
	void update(uint32_t now) override {
		if (_last && now - _last < 500) return;
		_last = now | 1;
		_tft.setTextColor(TFT_CYAN, TFT_BLACK);
		_tft.setTextSize(2);
		_tft.setCursor(8, 8);
		_tft.print("DEBUG");
		_tft.setTextColor(TFT_WHITE, TFT_BLACK);
		_tft.setTextSize(1);
		_tft.setCursor(8, 40);
		_tft.printf("uptime: %lu s   ", (unsigned long)(now / 1000));
		_tft.setCursor(8, 52);
		_tft.printf("heap livre: %u B   ", (unsigned)xPortGetFreeHeapSize());
		_tft.setCursor(8, 76);
		_tft.print("K0 longo = voltar");
	}

   private:
	TFT_FSMC& _tft;
	uint32_t _last = 0;
};