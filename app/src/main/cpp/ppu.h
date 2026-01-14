#ifndef PPU_H
#define PPU_H

#include <cstdint>

struct Sprite {
    uint8_t y;
    uint8_t tile_index;
    uint8_t attributes;
    uint8_t x;
};

struct PPU {
    uint8_t paletteTable[32];
    Sprite sprites[64];
    
    uint16_t scrollX = 0;
    uint16_t scrollY = 0;

    class Mapper* mapper = nullptr;

    // Registradores PPU ($2000-$2002)
    uint8_t ppuctrl = 0;   // $2000
    uint8_t ppumask = 0;   // $2001
    uint8_t ppustatus = 0; // $2002

    // Timing
    int scanline = 0;
    int cycle = 0;
    uint8_t oamAddr = 0; // NEW
    bool nmiOccurred = false;
    bool nmiPrevious = false; // For edge detection

    // Internal Registers & Latches ($2005, $2006)
    uint16_t vramAddr = 0; // v
    uint16_t tempAddr = 0; // t
    uint8_t fineX = 0;     // x
    bool writeToggle = false; // w
    uint8_t readBuffer = 0; 

    void reset();
    void step(int cycles, struct CPU* cpu);
    uint8_t readStatus();
    uint8_t vramRead(uint16_t addr);
    void vramWrite(uint16_t addr, uint8_t val);
};

#endif
