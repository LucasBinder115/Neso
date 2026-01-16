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
    Sprite sprites[64];  // Primary OAM
    uint32_t* pixelBuffer = nullptr; // For cycle-accurate rendering
    
    // Secondary OAM (8 sprites for current scanline)
    uint8_t secondaryOAM[32];  // 8 sprites × 4 bytes
    uint8_t spriteCount = 0;   // 0-8
    bool sprite0InSecondary = false;
    bool spriteOverflow = false;
    
    // Loopy registers (v, t, x, w) are below in "Internal Registers"

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
    void checkSprite0Hit(int x, int y, bool bgOpaque, bool spriteOpaque);
    
    // Loopy register helpers
    void incrementX();
    void incrementY();
    void copyX();
    void copyY();
    
    // Shift Registers (Background)
    uint16_t bgShiftPatternLo = 0;
    uint16_t bgShiftPatternHi = 0;
    uint8_t bgShiftAttrLo = 0;
    uint8_t bgShiftAttrHi = 0;

    // Latches for next tile data
    uint8_t bgNextTileId = 0;
    uint8_t bgNextTileAttr = 0;
    uint8_t bgNextTileLo = 0;
    uint8_t bgNextTileHi = 0;
    
    // Fetch Helpers
    void loadBackgroundShifters();
    void updateShifters();
    
    // Pixel Rendering
    void renderPixel();
};

#endif
