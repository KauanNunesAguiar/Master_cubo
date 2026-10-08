#pragma once
#include <Adafruit_GFX.h>
#include <Arduino.h>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_YELLOW 0xFFE0
#define TFT_CYAN 0x07FF
#define TFT_MAGENTA 0xF81F

class TFT_FSMC : public Adafruit_GFX {
   public:
	TFT_FSMC(int16_t w = 240, int16_t h = 320, uint32_t backlightPin = PB1);

	void begin();
	uint32_t readID();
	void setBacklight(bool on);
	void setAddrWindow(int16_t x, int16_t y, int16_t w, int16_t h);
	void pushColors(const uint16_t* data, uint32_t len);  // para imagens/bitmaps

	// Sobrescritas do Adafruit_GFX
	void drawPixel(int16_t x, int16_t y, uint16_t color) override;
	void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
	void fillScreen(uint16_t color) override;
	void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
	void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
	void setRotation(uint8_t r) override;

   private:
	uint32_t _blPin;
	void fsmcInit();
	void lcdInit();
};