// W25Q16.h — flash SPI de 2 MB (leitura, apagar setor de 4 KB, gravar páginas de 256 B)
#pragma once
#include <Arduino.h>
#include <SPI.h>

#include "stm32f4ve_peripherals.h"

class W25Q16 {
   public:
	W25Q16(uint32_t cs = W25Q16_CS, uint32_t mosi = W25Q16_DI, uint32_t miso = W25Q16_DO, uint32_t sck = W25Q16_CLK);

	bool begin();       // true se o ID for EF 40 15
	uint32_t readId();  // 0xEF4015
	void read(uint32_t addr, uint8_t* buf, uint32_t len);
	uint8_t read8(uint32_t addr);     // leitura de 1 byte pelos registradores do SPI (rápida)
	bool eraseSector(uint32_t addr);  // apaga 4 KB (addr alinhado); bloqueia até terminar
	void write(uint32_t addr, const uint8_t* data, uint32_t len);  // o trecho precisa estar apagado

   private:
	SPIClass _spi;
	uint32_t _cs;

	bool _fast = false;
	SPI_TypeDef* _spiReg = nullptr;
	GPIO_TypeDef* _csPort = nullptr;
	uint32_t _csMask = 0;
	void select();
	void deselect();
	void writeEnable();
	bool waitBusy(uint32_t timeoutMs);
	void programPage(uint32_t addr, const uint8_t* data, uint16_t len);
	void sendAddr(uint8_t cmd, uint32_t addr);
};