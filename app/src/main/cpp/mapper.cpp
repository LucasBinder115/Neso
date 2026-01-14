#include "mapper.h"
#include "rom.h"
#include <android/log.h>

// --- Mapper 0 (NROM) ---
uint8_t Mapper0::cpuRead(uint16_t addr) {
    if (addr >= 0x8000) {
        uint16_t mask = (rom->getPrgSize() > 16384) ? 0x7FFF : 0x3FFF;
        return rom->prgROM[addr & mask];
    }
    return 0;
}
void Mapper0::cpuWrite(uint16_t addr, uint8_t val) {}
uint8_t Mapper0::ppuRead(uint16_t addr) {
    if (addr < 0x2000) return rom->chrROM[addr];
    if (addr >= 0x2000 && addr <= 0x3EFF) {
        uint16_t ntAddr = addr & 0x0FFF;
        if (rom->isVerticalMirroring()) ntAddr &= 0x07FF;
        else ntAddr = ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF);
        return ppuVram[ntAddr % 2048];
    }
    return 0;
}
void Mapper0::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000 && rom->getChrSize() == 8192) rom->chrROM[addr] = val;
    else if (addr >= 0x2000 && addr <= 0x3EFF) {
        uint16_t ntAddr = addr & 0x0FFF;
        if (rom->isVerticalMirroring()) ntAddr &= 0x07FF;
        else ntAddr = ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF);
        ppuVram[ntAddr % 2048] = val;
    }
}

// --- Mapper 2 (UxROM) ---
uint8_t Mapper2::cpuRead(uint16_t addr) {
    if (addr >= 0x8000 && addr <= 0xBFFF) {
        return rom->prgROM[prgBankSelect * 16384 + (addr & 0x3FFF)];
    } else if (addr >= 0xC000) {
        uint32_t lastBankOffset = rom->getPrgSize() - 16384;
        return rom->prgROM[lastBankOffset + (addr & 0x3FFF)];
    }
    return 0;
}
void Mapper2::cpuWrite(uint16_t addr, uint8_t val) {
    if (addr >= 0x8000) {
        prgBankSelect = val & 0x0F;
        __android_log_print(ANDROID_LOG_DEBUG, "NesoROM", "Mapper 2 Bank Switch: %d", prgBankSelect);
    }
}
uint8_t Mapper2::ppuRead(uint16_t addr) {
    if (addr < 0x2000) return rom->chrROM[addr];
    if (addr >= 0x2000 && addr <= 0x3EFF) {
        uint16_t ntAddr = addr & 0x0FFF;
        if (rom->isVerticalMirroring()) ntAddr &= 0x07FF;
        else ntAddr = ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF);
        return ppuVram[ntAddr % 2048];
    }
    return 0;
}
void Mapper2::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr >= 0x2000 && addr <= 0x3EFF) {
        uint16_t ntAddr = addr & 0x0FFF;
        if (rom->isVerticalMirroring()) ntAddr &= 0x07FF;
        else ntAddr = ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF);
        ppuVram[ntAddr % 2048] = val;
    }
}

// --- Mapper 3 (CNROM) ---
uint8_t Mapper3::cpuRead(uint16_t addr) {
    if (addr >= 0x8000) {
        uint16_t mask = (rom->getPrgSize() > 16384) ? 0x7FFF : 0x3FFF;
        return rom->prgROM[addr & mask];
    }
    return 0;
}
void Mapper3::cpuWrite(uint16_t addr, uint8_t val) {
    if (addr >= 0x8000) chrBankSelect = val & 0x03;
}
uint8_t Mapper3::ppuRead(uint16_t addr) {
    if (addr < 0x2000) return rom->chrROM[chrBankSelect * 8192 + addr];
    if (addr >= 0x2000 && addr <= 0x3EFF) {
        uint16_t ntAddr = addr & 0x0FFF;
        if (rom->isVerticalMirroring()) ntAddr &= 0x07FF;
        else ntAddr = ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF);
        return ppuVram[ntAddr % 2048];
    }
    return 0;
}
void Mapper3::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr >= 0x2000 && addr <= 0x3EFF) {
        uint16_t ntAddr = addr & 0x0FFF;
        if (rom->isVerticalMirroring()) ntAddr &= 0x07FF;
        else ntAddr = ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF);
        ppuVram[ntAddr % 2048] = val;
    }
}
