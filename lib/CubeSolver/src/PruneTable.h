// PruneTable.h — tabelas de poda (4 bits por entrada) guardadas na W25Q16
#pragma once
#include "W25Q16.h"

constexpr uint32_t PRUNE_ADDR_TWIST_SLICE = 0x000000;
constexpr uint32_t PRUNE_ADDR_FLIP_SLICE = 0x085000;
constexpr uint32_t PRUNE_ADDR_CPERM_SP = 0x101000;
constexpr uint32_t PRUNE_ADDR_UDPERM_SP = 0x178000;

struct PruneTable {
	W25Q16* flash;
	uint32_t base;
	// índice = coordA * nB + coordB; par = nibble baixo, ímpar = nibble alto
	uint8_t get(uint32_t idx) const {
		uint8_t b;
		flash->read(base + (idx >> 1), &b, 1);
		return (idx & 1) ? (b >> 4) : (b & 0x0F);
	}
};