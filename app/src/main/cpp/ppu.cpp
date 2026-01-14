#include "ppu.h"
#include <cstring>
#include "cpu.h"
#include "mapper.h"
#include <android/log.h>

void PPU::reset() {
    memset(paletteTable, 0, sizeof(paletteTable));
    memset(sprites, 0, sizeof(sprites));
    
    scrollX = 0;
    scrollY = 0;
    scanline = 0;
    cycle = 0;
    ppuctrl = 0;
    ppumask = 0;
    ppustatus = 0;
    vramAddr = 0;
    tempAddr = 0;
    fineX = 0;
    writeToggle = false;
    nmiOccurred = false;
}

uint8_t PPU::readStatus() {
    uint8_t res = ppustatus;
    ppustatus &= ~0x80; 
    writeToggle = false; 
    return res;
}

void PPU::step(int cpuCycles, CPU* cpu) {
    int ppuCycles = cpuCycles * 3;
    for (int i = 0; i < ppuCycles; i++) {
        cycle++;
        if (cycle >= 341) {
            cycle = 0;
            scanline++;
            if (scanline >= 262) {
                scanline = 0;
                ppustatus &= ~0xE0; // Clear VBlank, Sprite 0 Hit
            }
        }

        if (scanline == 241 && cycle == 1) {
            ppustatus |= 0x80; // Set VBlank
            if (ppuctrl & 0x80) nmiOccurred = true;
        }

        // Sprite 0 Hit Stub (Essential for Mario)
        if (scanline == 30 && cycle == 10) {
            ppustatus |= 0x40;
        }
    }
}

uint8_t PPU::vramRead(uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x3F00) {
        return mapper ? mapper->ppuRead(addr) : 0;
    } else {
        uint16_t paletteAddr = addr & 0x001F;
        if (paletteAddr >= 0x10 && (paletteAddr & 0x03) == 0) paletteAddr -= 0x10;
        return paletteTable[paletteAddr];
    }
}

void PPU::vramWrite(uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;
    if (addr < 0x3F00) {
        if (mapper) mapper->ppuWrite(addr, val);
    } else {
        uint16_t paletteAddr = addr & 0x001F;
        if (paletteAddr >= 0x10 && (paletteAddr & 0x03) == 0) paletteAddr -= 0x10;
        paletteTable[paletteAddr] = val;
    }
}
