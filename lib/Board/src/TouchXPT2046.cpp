// TouchXPT2046.cpp
#include "TouchXPT2046.h"

TouchXPT2046::TouchXPT2046(uint32_t cs, uint32_t pen, uint32_t mosi, uint32_t miso, uint32_t sck)
    : _spi(mosi, miso, sck), _cs(cs), _pen(pen) {}

void TouchXPT2046::begin(int16_t w, int16_t h) {
	_w = w;
	_h = h;
	pinMode(_cs, OUTPUT);
	digitalWrite(_cs, HIGH);
	pinMode(_pen, INPUT_PULLUP);
	_spi.begin();
}

void TouchXPT2046::setCalibration(uint16_t xMin, uint16_t xMax, uint16_t yMin, uint16_t yMax, bool swapXY, bool invX,
                                  bool invY) {
	_xMin = xMin;
	_xMax = xMax;
	_yMin = yMin;
	_yMax = yMax;
	_swap = swapXY;
	_invX = invX;
	_invY = invY;
}

uint16_t TouchXPT2046::readChannel(uint8_t cmd) {
	digitalWrite(_cs, LOW);
	_spi.transfer(cmd);
	uint8_t hi = _spi.transfer(0);
	uint8_t lo = _spi.transfer(0);
	digitalWrite(_cs, HIGH);
	return (((hi << 8) | lo) >> 3) & 0x0FFF;
}

bool TouchXPT2046::read(int16_t& x, int16_t& y) {
	if (digitalRead(_pen) != LOW) return false;

	const int N = 8;
	uint32_t sx = 0, sy = 0;
	_spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
	readChannel(0xD0);  // leitura descartada
	for (int i = 0; i < N; i++) {
		sx += readChannel(0xD0);
		sy += readChannel(0x90);
	}
	_spi.endTransaction();

	if (digitalRead(_pen) != LOW) return false;

	rawX = sx / N;
	rawY = sy / N;

	int32_t rx = rawX, ry = rawY;
	int32_t xMin = _xMin, xMax = _xMax, yMin = _yMin, yMax = _yMax;
	if (_swap) {
		int32_t t = rx;
		rx = ry;
		ry = t;
	}

	int32_t px = (rx - xMin) * (_w - 1) / (xMax - xMin);
	int32_t py = (ry - yMin) * (_h - 1) / (yMax - yMin);
	px = constrain(px, 0, _w - 1);
	py = constrain(py, 0, _h - 1);
	if (_invX) px = _w - 1 - px;
	if (_invY) py = _h - 1 - py;

	x = px;
	y = py;
	return true;
}