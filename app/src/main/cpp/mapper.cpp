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
void Mapper0::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {}
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
        // Mask prgBankSelect to fit numBanks
        int numBanks = rom->getPrgSize() / 16384;
        uint16_t bank = prgBankSelect % numBanks;
        return rom->prgROM[bank * 16384 + (addr & 0x3FFF)];
    } else if (addr >= 0xC000) {
        uint32_t lastBankOffset = rom->getPrgSize() - 16384;
        return rom->prgROM[lastBankOffset + (addr & 0x3FFF)];
    }
    return 0;
}
void Mapper2::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
    if (addr >= 0x8000) {
        prgBankSelect = val & 0x7F; // UxROM can have many banks
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
    if (addr < 0x2000) {
        rom->chrROM[addr] = val;
    } else if (addr >= 0x2000 && addr <= 0x3EFF) {
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
void Mapper3::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
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

// --- Mapper 1 (MMC1) ---
void Mapper1::reset() {
    shiftReg = 0x10;
    control = 0x0C; // PRG bank mode 3, CHR 8KB
    prgBank = 0;
    chrBank0 = 0;
    chrBank1 = 0;
    updateOffsets();
}

void Mapper1::updateOffsets() {
    // CHR Banks
    if (control & 0x10) { // 4KB mode
        chrOffsets[0] = chrBank0 * 4096;
        chrOffsets[1] = chrBank1 * 4096;
    } else { // 8KB mode
        chrOffsets[0] = (chrBank0 & 0xFE) * 4096;
        chrOffsets[1] = chrOffsets[0] + 4096;
    }

    // PRG Banks
    int numPrgBanks = rom->getPrgSize() / 16384;
    if (numPrgBanks == 0) return;
    uint8_t mode = (control >> 2) & 0x03;
    switch (mode) {
        case 0: case 1: // 32KB mode
            prgOffsets[0] = (prgBank & 0xFE) * 16384;
            prgOffsets[1] = prgOffsets[0] + 16384;
            break;
        case 2: // Fix first, switch last
            prgOffsets[0] = 0;
            prgOffsets[1] = (prgBank % numPrgBanks) * 16384;
            break;
        case 3: // Switch first, fix last
            prgOffsets[0] = (prgBank % numPrgBanks) * 16384;
            prgOffsets[1] = (numPrgBanks - 1) * 16384;
            break;
    }
}

uint8_t Mapper1::cpuRead(uint16_t addr) {
    if (addr >= 0x8000) {
        int idx = (addr >= 0xC000);
        return rom->prgROM[prgOffsets[idx] + (addr & 0x3FFF)];
    }
    return 0;
}

void Mapper1::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
    if (addr < 0x8000) return;

    // Consecutive write suppression (MMC1 behavior)
    if (cycles - lastWriteCycle < 2) return; 
    lastWriteCycle = cycles;

    if (val & 0x80) {
        shiftReg = 0x10;
        control |= 0x0C;
        updateOffsets();
    } else {
        bool complete = (shiftReg & 1);
        shiftReg >>= 1;
        shiftReg |= (val & 1) << 4;
        if (complete) {
            uint8_t data = shiftReg;
            if (addr <= 0x9FFF) control = data;
            else if (addr <= 0xBFFF) chrBank0 = data;
            else if (addr <= 0xDFFF) chrBank1 = data;
            else prgBank = data & 0x1F;
            updateOffsets();
            shiftReg = 0x10;
        }
    }
}

uint8_t Mapper1::ppuRead(uint16_t addr) {
    if (addr < 0x2000) {
        int idx = (addr >= 0x1000);
        return rom->chrROM[chrOffsets[idx] + (addr & 0x0FFF)];
    }
    // Nametables
    uint16_t ntAddr = addr & 0x0FFF;
    uint8_t m = control & 0x03;
    switch (m) {
        case 0: ntAddr &= 0x03FF; break; // 1-screen A
        case 1: ntAddr = 0x0400 | (ntAddr & 0x03FF); break; // 1-screen B
        case 2: ntAddr &= 0x07FF; break; // Vertical
        case 3: ntAddr = ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF); break; // Horizontal
    }
    return ppuVram[ntAddr % 2048];
}

void Mapper1::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        if (rom->getChrSize() == 0) rom->chrROM[addr] = val; // CHR-RAM
    } else {
        uint16_t ntAddr = addr & 0x0FFF;
        uint8_t m = control & 0x03;
        switch (m) {
            case 0: ntAddr &= 0x03FF; break;
            case 1: ntAddr = 0x0400 | (ntAddr & 0x03FF); break;
            case 2: ntAddr &= 0x07FF; break;
            case 3: ntAddr = ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF); break;
        }
        ppuVram[ntAddr % 2048] = val;
    }
}

// --- Mapper 7 (AOROM) ---
uint8_t Mapper7::cpuRead(uint16_t addr) {
    if (addr >= 0x8000) {
        int bank = prgBank % (rom->getPrgSize() / 32768);
        return rom->prgROM[bank * 32768 + (addr & 0x7FFF)];
    }
    return 0;
}

void Mapper7::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
    if (addr >= 0x8000) {
        prgBank = val & 0x0F;
        mirroring = (val >> 4) & 1;
    }
}

uint8_t Mapper7::ppuRead(uint16_t addr) {
    if (addr < 0x2000) return rom->chrROM[addr];
    uint16_t ntAddr = (mirroring ? 0x0400 : 0x0000) | (addr & 0x03FF);
    return ppuVram[ntAddr % 2048];
}

void Mapper7::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) rom->chrROM[addr] = val;
    else {
        uint16_t ntAddr = (mirroring ? 0x0400 : 0x0000) | (addr & 0x03FF);
        ppuVram[ntAddr % 2048] = val;
    }
}
