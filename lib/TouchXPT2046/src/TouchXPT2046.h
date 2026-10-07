#pragma once
#include <Arduino.h>
#include <SPI.h>

class TouchXPT2046 {
   public:
	TouchXPT2046(uint32_t cs = PB12, uint32_t pen = PC5, uint32_t mosi = PB15, uint32_t miso = PB14,
	             uint32_t sck = PB13);

	void begin(int16_t w, int16_t h);
	void setCalibration(uint16_t xMin, uint16_t xMax, uint16_t yMin, uint16_t yMax, bool swapXY = false,
	                    bool invX = false, bool invY = false);
	bool read(int16_t& x, int16_t& y);  // true se houver toque

	uint16_t rawX = 0, rawY = 0;  // últimos valores brutos (para calibrar)

   private:
	SPIClass _spi;
	uint32_t _cs, _pen;
	int16_t _w = 240, _h = 320;
	uint16_t _xMin = 200, _xMax = 3900, _yMin = 200, _yMax = 3900;
	bool _swap = false, _invX = false, _invY = false;

	uint16_t readChannel(uint8_t cmd);
};