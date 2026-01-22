#include "mapper.h"
#include "rom.h"
#include <android/log.h>

Mapper0::Mapper0(Rom* rom) : Mapper(rom) {
    reset();
}

void Mapper0::reset() {}

uint8_t Mapper0::cpuRead(uint16_t addr) {
    if (addr >= 0x8000) {
        uint16_t mask = (rom->getPrgSize() > 16384) ? 0x7FFF : 0x3FFF;
        return rom->prgROM[addr & mask];
    }
    return 0;
}
void Mapper0::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {}
uint8_t Mapper0::ppuRead(uint16_t addr) {
    if (addr < 0x2000) return rom->safeChrRead(addr);
    if (addr >= 0x2000 && addr <= 0x3EFF) {
        MirrorMode mode = rom->isVerticalMirroring() ? MirrorMode::Vertical : MirrorMode::Horizontal;
        return ppuVram[getMirrorAddr(addr, mode) % 2048];
    }
    return 0;
}
void Mapper0::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000 && rom->getChrSize() == 8192) {
        if (addr < rom->chrROM.size()) rom->chrROM[addr] = val;
    }
    else if (addr >= 0x2000 && addr <= 0x3EFF) {
        MirrorMode mode = rom->isVerticalMirroring() ? MirrorMode::Vertical : MirrorMode::Horizontal;
        ppuVram[getMirrorAddr(addr, mode) % 2048] = val;
    }
}

Mapper2::Mapper2(Rom* rom) : Mapper(rom) {
    numPrgBanks = rom->getPrgSize() / 16384;
    // Power of 2 mask for PRG bank selection robustness
    prgBankMask = numPrgBanks - 1; 
    reset();
}

void Mapper2::reset() {
    prgBankSelect = 0;
}

uint8_t Mapper2::cpuRead(uint16_t addr) {
    if (addr >= 0x8000 && addr <= 0xBFFF) {
        // Lower bank: Switchable
        uint8_t bank = prgBankSelect & prgBankMask;
        return rom->safePrgRead((uint32_t)bank * 16384 + (addr & 0x3FFF));
    } else if (addr >= 0xC000) {
        // Upper bank: Fixed to last bank
        int lastBank = numPrgBanks - 1;
        return rom->safePrgRead((uint32_t)lastBank * 16384 + (addr & 0x3FFF));
    }
    return 0;
}

void Mapper2::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
    if (addr >= 0x8000) {
        // UxROM uses the lower bits for bank selection. 
        // Some variants use more bits, 0x7F covers most large games like Contra.
        prgBankSelect = val; 
    }
}
uint8_t Mapper2::ppuRead(uint16_t addr) {
    if (addr < 0x2000) return rom->safeChrRead(addr);
    if (addr >= 0x2000 && addr <= 0x3EFF) {
        MirrorMode mode = rom->isVerticalMirroring() ? MirrorMode::Vertical : MirrorMode::Horizontal;
        return ppuVram[getMirrorAddr(addr, mode) % 2048];
    }
    return 0;
}
void Mapper2::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        rom->chrROM[addr] = val;
    } else if (addr >= 0x2000 && addr <= 0x3EFF) {
        MirrorMode mode = rom->isVerticalMirroring() ? MirrorMode::Vertical : MirrorMode::Horizontal;
        ppuVram[getMirrorAddr(addr, mode) % 2048] = val;
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

Mapper3::Mapper3(Rom* rom) : Mapper(rom) {
    numChrBanks = rom->getChrSize() / 8192;
    chrBankMask = numChrBanks - 1;
    reset();
}

void Mapper3::reset() {
    chrBankSelect = 0;
}

void Mapper3::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
    if (addr >= 0x8000) chrBankSelect = val;
}

uint8_t Mapper3::ppuRead(uint16_t addr) {
    if (addr < 0x2000) {
        int bank = chrBankSelect & chrBankMask;
        return rom->safeChrRead((uint32_t)bank * 8192 + addr);
    }
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
        MirrorMode mode = rom->isVerticalMirroring() ? MirrorMode::Vertical : MirrorMode::Horizontal;
        ppuVram[getMirrorAddr(addr, mode) % 2048] = val;
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

Mapper1::Mapper1(Rom* rom) : Mapper(rom) {
    numPrgBanks = rom->getPrgSize() / 16384;
    numChrBanks = rom->getChrSize() / 4096;
    reset();
}

void Mapper1::updateOffsets() {
    // CHR Banks
    if (numChrBanks > 0) {
        if (control & 0x10) { // 4KB mode
            chrOffsets[0] = (chrBank0 % numChrBanks) * 4096;
            chrOffsets[1] = (chrBank1 % numChrBanks) * 4096;
        } else { // 8KB mode
            chrOffsets[0] = ((chrBank0 & 0xFE) % numChrBanks) * 4096;
            chrOffsets[1] = ((chrBank0 | 0x01) % numChrBanks) * 4096;
        }
    }

    // PRG Banks
    if (numPrgBanks == 0) return;
    uint8_t mode = (control >> 2) & 0x03;
    switch (mode) {
        case 0: case 1: // 32KB mode
            prgOffsets[0] = ((prgBank & 0xFE) % numPrgBanks) * 16384;
            prgOffsets[1] = ((prgBank | 0x01) % numPrgBanks) * 16384;
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
        return rom->safePrgRead(prgOffsets[idx] + (addr & 0x3FFF));
    }
    return 0;
}

void Mapper1::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
    if (addr < 0x8000) return;

    // Consecutive write suppression: MMC1 ignores writes on consecutive CPU cycles.
    if (cycles - lastWriteCycle < 2) return; 
    lastWriteCycle = cycles;

    if (val & 0x80) {
        // Reset shift register and set control bits
        shiftReg = 0x10;
        control |= 0x0C;
        updateOffsets();
    } else {
        // Load bit into shift register
        bool complete = (shiftReg & 1);
        shiftReg >>= 1;
        shiftReg |= (val & 1) << 4;

        if (complete) {
            uint8_t data = shiftReg;
            // Write to internal registers based on address
            if (addr <= 0x9FFF) {
                control = data;
                // Note: bit 0-1 of control register handle mirroring
            } else if (addr <= 0xBFFF) {
                chrBank0 = data;
            } else if (addr <= 0xDFFF) {
                chrBank1 = data;
            } else {
                prgBank = data & 0x0F; // Only lower 4 bits for PRG bank
                // Bit 4 of prgBank is used for PRG RAM disable on some boards, 
                // but usually data & 0x1F is fine for higher bank counts.
                // Let's use 0x1F to support larger MMC1 games (512KB).
                prgBank = data & 0x1F;
            }
            updateOffsets();
            shiftReg = 0x10;
        }
    }
}

uint8_t Mapper1::ppuRead(uint16_t addr) {
    if (addr < 0x2000) {
        int idx = (addr >= 0x1000);
        return rom->safeChrRead(chrOffsets[idx] + (addr & 0x0FFF));
    }
    // Nametables
    uint8_t m = control & 0x03;
    MirrorMode mode;
    switch (m) {
        case 0: mode = MirrorMode::SingleScreenLower; break;
        case 1: mode = MirrorMode::SingleScreenUpper; break;
        case 2: mode = MirrorMode::Vertical; break;
        case 3: mode = MirrorMode::Horizontal; break;
        default: mode = MirrorMode::Vertical; break;
    }
    return ppuVram[getMirrorAddr(addr, mode) % 2048];
}

void Mapper1::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        if (rom->getChrSize() == 0) rom->chrROM[addr] = val; // CHR-RAM
    } else if (addr >= 0x2000 && addr <= 0x3EFF) {
        uint8_t m = control & 0x03;
        MirrorMode mode;
        switch (m) {
            case 0: mode = MirrorMode::SingleScreenLower; break;
            case 1: mode = MirrorMode::SingleScreenUpper; break;
            case 2: mode = MirrorMode::Vertical; break;
            case 3: mode = MirrorMode::Horizontal; break;
            default: mode = MirrorMode::Vertical; break;
        }
        ppuVram[getMirrorAddr(addr, mode) % 2048] = val;
    }
}

// --- Mapper 7 (AOROM) ---
Mapper7::Mapper7(Rom* rom) : Mapper(rom) {
    numPrgBanks = rom->getPrgSize() / 32768;
    prgBankMask = numPrgBanks - 1;
    reset();
}

void Mapper7::reset() {
    prgBank = 0;
    mirroring = 0;
}

uint8_t Mapper7::cpuRead(uint16_t addr) {
    if (addr >= 0x8000) {
        int bank = prgBank & prgBankMask;
        return rom->safePrgRead((uint32_t)bank * 32768 + (addr & 0x7FFF));
    }
    return 0;
}

void Mapper7::cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
    if (addr >= 0x8000) {
        prgBank = val & 0x07; // Usually 3 bits are enough for 256KB games
        // Bit 4 selects nametable for Single-Screen mirroring
        mirroring = (val >> 4) & 1;
    }
}

uint8_t Mapper7::ppuRead(uint16_t addr) {
    if (addr < 0x2000) return rom->safeChrRead(addr);
    MirrorMode mode = mirroring ? MirrorMode::SingleScreenUpper : MirrorMode::SingleScreenLower;
    return ppuVram[getMirrorAddr(addr, mode) % 2048];
}

void Mapper7::ppuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        if (addr < rom->chrROM.size()) rom->chrROM[addr] = val;
    }
    else if (addr >= 0x2000 && addr <= 0x3EFF) {
        MirrorMode mode = mirroring ? MirrorMode::SingleScreenUpper : MirrorMode::SingleScreenLower;
        ppuVram[getMirrorAddr(addr, mode) % 2048] = val;
    }
}
