// TouchXPT2046.cpp
#include "TouchXPT2046.h"

TouchXPT2046::TouchXPT2046(uint32_t cs, uint32_t pen, uint32_t mosi, uint32_t miso, uint32_t sck)
    : _spi(mosi, miso, sck), _cs(cs), _pen(pen) {}

void TouchXPT2046::begin(int16_t w, int16_t h) {
	_w = w;
	_h = h;
	pinMode(_cs, OUTPUT);
	digitalWrite(_cs, HIGH);
	pinMode(_pen, INPUT_PULLUP);  // não é usado para detectar toque (instável logo após a conversão)
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

static uint16_t median5(uint16_t* v) {
	for (uint8_t i = 1; i < 5; i++) {
		uint16_t k = v[i];
		int8_t j = (int8_t)i - 1;
		while (j >= 0 && v[j] > k) {
			v[j + 1] = v[j];
			j--;
		}
		v[j + 1] = k;
	}
	return v[2];
}

bool TouchXPT2046::readRaw(uint16_t& rx, uint16_t& ry) {
	uint16_t xs[5], ys[5];

	_spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
	uint16_t z1 = readChannel(0xB0);  // Z1
	uint16_t z2 = readChannel(0xC0);  // Z2
	int32_t z = (int32_t)z1 + 4095 - (int32_t)z2;
	pressure = z > 0 ? (uint16_t)z : 0;
	bool touched = z >= (int32_t)minPressure;
	if (touched) {
		readChannel(0xD0);  // leituras descartadas (tempo de assentamento)
		readChannel(0x90);
		for (uint8_t i = 0; i < 5; i++) {
			xs[i] = readChannel(0xD0);
			ys[i] = readChannel(0x90);
		}
	}
	_spi.endTransaction();

	if (!touched) return false;

	uint16_t mx = median5(xs), my = median5(ys);
	if (mx == 0 || mx >= 4095 || my == 0 || my >= 4095) return false;  // leitura inválida
	rx = mx;
	ry = my;
	return true;
}

bool TouchXPT2046::read(int16_t& x, int16_t& y) {
	uint16_t rx16, ry16;
	if (!readRaw(rx16, ry16)) return false;
	rawX = rx16;
	rawY = ry16;

	int32_t rx = rx16, ry = ry16;
	int32_t xMin = _xMin, xMax = _xMax, yMin = _yMin, yMax = _yMax;
	if (xMax <= xMin || yMax <= yMin) return false;
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