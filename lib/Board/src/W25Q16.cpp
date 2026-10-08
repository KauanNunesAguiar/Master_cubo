// W25Q16.cpp
#include "W25Q16.h"

static const uint32_t SPI_HZ = 10000000;  // conservador; dá para subir depois de medir

W25Q16::W25Q16(uint32_t cs, uint32_t mosi, uint32_t miso, uint32_t sck) : _spi(mosi, miso, sck), _cs(cs) {}

bool W25Q16::begin() {
	pinMode(_cs, OUTPUT);
	digitalWrite(_cs, HIGH);
	_spi.begin();
	return readId() == 0xEF4015;
}

void W25Q16::select() {
	_spi.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
	digitalWrite(_cs, LOW);
}
void W25Q16::deselect() {
	digitalWrite(_cs, HIGH);
	_spi.endTransaction();
}

void W25Q16::sendAddr(uint8_t cmd, uint32_t addr) {  // chamar com o CS já baixo
	_spi.transfer(cmd);
	_spi.transfer((addr >> 16) & 0xFF);
	_spi.transfer((addr >> 8) & 0xFF);
	_spi.transfer(addr & 0xFF);
}

uint32_t W25Q16::readId() {
	select();
	_spi.transfer(0x9F);
	uint8_t a = _spi.transfer(0), b = _spi.transfer(0), c = _spi.transfer(0);
	deselect();
	return ((uint32_t)a << 16) | (b << 8) | c;
}

bool W25Q16::waitBusy(uint32_t timeoutMs) {
	uint32_t t0 = millis();
	for (;;) {
		select();
		_spi.transfer(0x05);  // status register 1
		uint8_t sr = _spi.transfer(0);
		deselect();
		if (!(sr & 1)) return true;  // bit 0 = BUSY
		if (millis() - t0 > timeoutMs) return false;
	}
}

void W25Q16::writeEnable() {
	select();
	_spi.transfer(0x06);
	deselect();
}

void W25Q16::read(uint32_t addr, uint8_t* buf, uint32_t len) {
	select();
	sendAddr(0x03, addr);
	for (uint32_t i = 0; i < len; i++) buf[i] = _spi.transfer(0);
	deselect();
}

bool W25Q16::eraseSector(uint32_t addr) {
	if (!waitBusy(1000)) return false;
	writeEnable();
	select();
	sendAddr(0x20, addr);
	deselect();
	return waitBusy(1000);
}

void W25Q16::programPage(uint32_t addr, const uint8_t* data, uint16_t len) {
	waitBusy(1000);
	writeEnable();
	select();
	sendAddr(0x02, addr);
	for (uint16_t i = 0; i < len; i++) _spi.transfer(data[i]);
	deselect();
	waitBusy(1000);
}

void W25Q16::write(uint32_t addr, const uint8_t* data, uint32_t len) {
	while (len) {
		uint16_t n = 256 - (addr & 0xFF);  // não pode atravessar o limite da página
		if (n > len) n = len;
		programPage(addr, data, n);
		addr += n;
		data += n;
		len -= n;
	}
}