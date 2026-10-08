// flasher.cpp — recebe arquivos pela serial e grava na W25Q16
#include <Arduino.h>

#include "W25Q16.h"

static const uint32_t BAUD = 921600;  // se der erro, use 115200 aqui e no script
W25Q16 flash;

static uint32_t crc32Step(uint32_t c, const uint8_t* p, uint32_t n) {  // estado inicial 0xFFFFFFFF
	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
	}
	return c;
}

void setup() {
	Serial.begin(BAUD);
	Serial.setTimeout(3000);
	flash.begin();
}

void loop() {
	if (!Serial.available()) return;
	int cmd = Serial.read();

	if (cmd == 'I') {  // devolve os 3 bytes do ID
		uint32_t id = flash.readId();
		Serial.write((uint8_t)(id >> 16));
		Serial.write((uint8_t)(id >> 8));
		Serial.write((uint8_t)id);
	} else if (cmd == 'W') {  // 'W' + addr(4) + len(4), little-endian
		uint32_t hdr[2];
		if (Serial.readBytes((char*)hdr, 8) != 8) {
			Serial.write('E');
			return;
		}
		uint32_t addr = hdr[0], len = hdr[1];
		if ((addr & 0xFFF) || addr + len > 0x200000) {
			Serial.write('E');
			return;
		}
		Serial.write('K');

		uint8_t buf[256];
		for (uint32_t off = 0; off < len; off += 256) {
			uint16_t n = (len - off < 256) ? (len - off) : 256;
			if (Serial.readBytes((char*)buf, n) != n) {
				Serial.write('E');
				return;
			}
			if (((addr + off) & 0xFFF) == 0 && !flash.eraseSector(addr + off)) {
				Serial.write('E');
				return;
			}
			flash.write(addr + off, buf, n);
			Serial.write('K');
		}

		uint32_t c = 0xFFFFFFFF;  // CRC32 do conteúdo lido de volta (compatível com zlib.crc32)
		for (uint32_t off = 0; off < len; off += 256) {
			uint16_t n = (len - off < 256) ? (len - off) : 256;
			flash.read(addr + off, buf, n);
			c = crc32Step(c, buf, n);
		}
		c = ~c;
		Serial.write((uint8_t*)&c, 4);
	}
}