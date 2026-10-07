// TFT_FSMC.cpp
#include "TFT_FSMC.h"

#define LCD_CMD (*(volatile uint16_t*)0x60000000)
#define LCD_DATA (*(volatile uint16_t*)0x60080000)

static void pinAF12(GPIO_TypeDef* g, uint8_t pin) {
	g->MODER = (g->MODER & ~(3u << (pin * 2))) | (2u << (pin * 2));
	g->OSPEEDR |= (3u << (pin * 2));
	g->OTYPER &= ~(1u << pin);
	g->PUPDR &= ~(3u << (pin * 2));
	g->AFR[pin >> 3] = (g->AFR[pin >> 3] & ~(0xFu << ((pin & 7) * 4))) | (12u << ((pin & 7) * 4));
}

TFT_FSMC::TFT_FSMC(int16_t w, int16_t h, uint32_t backlightPin) : Adafruit_GFX(w, h), _blPin(backlightPin) {}

void TFT_FSMC::fsmcInit() {
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
	RCC->AHB3ENR |= RCC_AHB3ENR_FSMCEN;

	const uint8_t pd[] = {0, 1, 4, 5, 7, 8, 9, 10, 13, 14, 15};
	for (uint8_t p : pd) pinAF12(GPIOD, p);
	for (uint8_t p = 7; p <= 15; p++) pinAF12(GPIOE, p);

	FSMC_Bank1->BTCR[0] = FSMC_BCR1_MBKEN | FSMC_BCR1_MWID_0 | FSMC_BCR1_WREN | FSMC_BCR1_EXTMOD;
	FSMC_Bank1->BTCR[1] = 15 | (60 << 8);
	FSMC_Bank1E->BWTR[0] = 5 | (10 << 8);
}

void TFT_FSMC::lcdInit() {
	LCD_CMD = 0x01;
	delay(120);
	LCD_CMD = 0x11;
	delay(120);
	LCD_CMD = 0x3A;
	LCD_DATA = 0x55;
	setRotation(0);
	LCD_CMD = 0x29;
	delay(20);
}

void TFT_FSMC::begin() {
	pinMode(_blPin, OUTPUT);
	setBacklight(true);
	fsmcInit();
	delay(50);
	lcdInit();
}

uint32_t TFT_FSMC::readID() {
	LCD_CMD = 0xD3;
	uint16_t dummy = LCD_DATA;
	(void)dummy;
	uint8_t a = LCD_DATA & 0xFF;
	uint8_t b = LCD_DATA & 0xFF;
	uint8_t c = LCD_DATA & 0xFF;
	return ((uint32_t)a << 16) | (b << 8) | c;  // ILI9341: 0x009341
}

void TFT_FSMC::setBacklight(bool on) { digitalWrite(_blPin, on ? HIGH : LOW); }

void TFT_FSMC::setRotation(uint8_t r) {
	rotation = r & 3;
	static const uint8_t madctl[] = {0x48, 0x28, 0x88, 0xE8};
	LCD_CMD = 0x36;
	LCD_DATA = madctl[rotation];
	if (rotation & 1) {
		_width = HEIGHT;
		_height = WIDTH;
	} else {
		_width = WIDTH;
		_height = HEIGHT;
	}
}

void TFT_FSMC::setAddrWindow(int16_t x, int16_t y, int16_t w, int16_t h) {
	int16_t x2 = x + w - 1, y2 = y + h - 1;
	LCD_CMD = 0x2A;
	LCD_DATA = x >> 8;
	LCD_DATA = x & 0xFF;
	LCD_DATA = x2 >> 8;
	LCD_DATA = x2 & 0xFF;
	LCD_CMD = 0x2B;
	LCD_DATA = y >> 8;
	LCD_DATA = y & 0xFF;
	LCD_DATA = y2 >> 8;
	LCD_DATA = y2 & 0xFF;
	LCD_CMD = 0x2C;
}

void TFT_FSMC::pushColors(const uint16_t* data, uint32_t len) {
	while (len--) LCD_DATA = *data++;
}

void TFT_FSMC::drawPixel(int16_t x, int16_t y, uint16_t color) {
	if (x < 0 || y < 0 || x >= _width || y >= _height) return;
	setAddrWindow(x, y, 1, 1);
	LCD_DATA = color;
}

void TFT_FSMC::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > _width) w = _width - x;
	if (y + h > _height) h = _height - y;
	if (w <= 0 || h <= 0) return;
	setAddrWindow(x, y, w, h);
	for (uint32_t i = (uint32_t)w * h; i; i--) LCD_DATA = color;
}

void TFT_FSMC::fillScreen(uint16_t color) { fillRect(0, 0, _width, _height, color); }
void TFT_FSMC::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) { fillRect(x, y, w, 1, c); }
void TFT_FSMC::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) { fillRect(x, y, 1, h, c); }