#include "ppu.h"
#include <cstring>
#include "cpu.h"
#include "mapper.h"
#include "palette.h"
#include "renderer.h"
#include <android/log.h>

void PPU::reset() {
    memset(paletteTable, 0, sizeof(paletteTable));
    memset(sprites, 0, sizeof(sprites));
    memset(secondaryOAM, 0xFF, sizeof(secondaryOAM));
    
    spriteCount = 0;
    sprite0InSecondary = false;
    spriteOverflow = false;
    
    spriteCount = 0;
    sprite0InSecondary = false;
    spriteOverflow = false;
    
    bgShiftPatternLo = 0;
    bgShiftPatternHi = 0;
    bgShiftAttrLo = 0;
    bgShiftAttrHi = 0;
    
    bgNextTileId = 0;
    bgNextTileAttr = 0;
    bgNextTileLo = 0;
    bgNextTileHi = 0;

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
                ppustatus &= ~0xE0; // Clear VBlank, Sprite 0 Hit, Sprite Overflow
            }
        }

        // Visible scanlines (0-239) and pre-render (261)
        bool isVisibleOrPrerender = (scanline < 240) || (scanline == 261);
        
        if (isVisibleOrPrerender) {
            // --- Background Fetch Pipeline ---
            if (ppumask & 0x18) { // Rendering enabled
                updateShifters();
                
                // Pixel Output (Cycle 1-256, visible scanlines only)
                if (scanline < 240 && cycle >= 1 && cycle <= 256) {
                    renderPixel();
                }
                
                // Cycles 1-256 (Scanlines 0-239 & 261)
                if ((cycle >= 1 && cycle <= 256) || (cycle >= 321 && cycle <= 336)) {
                    int step = (cycle - 1) % 8;
                    
                    // NT Byte (Cycle 2)
                    if (step == 1) {
                        uint16_t ntAddr = 0x2000 | (vramAddr & 0x0FFF);
                        bgNextTileId = vramRead(ntAddr);
                    }
                    
                    // AT Byte (Cycle 4)
                    if (step == 3) {
                        uint16_t atAddr = 0x23C0 | (vramAddr & 0x0C00) | ((vramAddr >> 4) & 0x38) | ((vramAddr >> 2) & 0x07);
                        bgNextTileAttr = vramRead(atAddr);
                        if (vramAddr & 0x0040) bgNextTileAttr >>= 4; // Bottom
                        if (vramAddr & 0x0002) bgNextTileAttr >>= 2; // Right
                        bgNextTileAttr &= 0x03;
                    }
                    
                    // Pattern Lo (Cycle 6)
                    if (step == 5) {
                        uint16_t patternAddr = ((ppuctrl & 0x10) ? 0x1000 : 0x0000) + ((uint16_t)bgNextTileId << 4) + ((vramAddr >> 12) & 0x07);
                        bgNextTileLo = vramRead(patternAddr);
                    }
                    
                    // Pattern Hi (Cycle 8)
                    if (step == 7) {
                        uint16_t patternAddr = ((ppuctrl & 0x10) ? 0x1000 : 0x0000) + ((uint16_t)bgNextTileId << 4) + ((vramAddr >> 12) & 0x07) + 8;
                        bgNextTileHi = vramRead(patternAddr);
                        
                        incrementX();
                        loadBackgroundShifters();
                    }
                    
                    // Vertical Increment (Cycle 256)
                    if (cycle == 256) {
                        incrementY();
                    }
                }
                
                // Copy horizontal bits from t to v (Cycle 257)
                if (cycle == 257) {
                    loadBackgroundShifters();
                    copyX();
                }
                
                // Copy vertical bits from t to v (Pre-render 280-304)
                if (scanline == 261 && cycle >= 280 && cycle <= 304) {
                    copyY();
                }
            }

            // --- Sprite Evaluation Pipeline (Unchanged) ---
            // Cycles 1-64: Clear secondary OAM
            if (cycle >= 1 && cycle <= 64) {
                int idx = ((cycle - 1) / 2) % 32;
                if (cycle % 2 == 0) {
                    secondaryOAM[idx] = 0xFF;
                }
            }
            
            // Cycle 65: Initialize sprite evaluation
            if (cycle == 65) {
                spriteCount = 0;
                sprite0InSecondary = false;
                spriteOverflow = false;
            }
            
            // Cycles 65-256: Sprite Evaluation
            if (cycle >= 65 && cycle <= 256) {
                int oamIdx = (cycle - 65) / 8; // 0-23 (24 sprites checked per scanline)
                int subCycle = (cycle - 65) % 8;
                
                if (oamIdx < 64 && spriteCount < 8 && subCycle == 0) {
                    uint8_t spriteY = sprites[oamIdx].y;
                    int spriteHeight = (ppuctrl & 0x20) ? 16 : 8;
                    int nextScanline = (scanline == 261) ? 0 : scanline + 1;
                    
                    // Check if sprite is on next scanline
                    int diff = nextScanline - spriteY;
                    if (diff >= 0 && diff < spriteHeight) {
                        // Copy to secondary OAM
                        // Note: should check priority/overflow here too but logic is simplified
                        int secIdx = spriteCount * 4;
                        secondaryOAM[secIdx + 0] = sprites[oamIdx].y;
                        secondaryOAM[secIdx + 1] = sprites[oamIdx].tile_index;
                        secondaryOAM[secIdx + 2] = sprites[oamIdx].attributes;
                        secondaryOAM[secIdx + 3] = sprites[oamIdx].x;
                        
                        if (oamIdx == 0) {
                            sprite0InSecondary = true;
                        }
                        
                        spriteCount++;
                    }
                } else if (oamIdx < 64 && spriteCount >= 8 && subCycle == 0) {
                    // Check for 9th sprite (overflow)
                    uint8_t spriteY = sprites[oamIdx].y;
                    int spriteHeight = (ppuctrl & 0x20) ? 16 : 8;
                    int nextScanline = (scanline == 261) ? 0 : scanline + 1;
                    int diff = nextScanline - spriteY;
                    
                    if (diff >= 0 && diff < spriteHeight) {
                        spriteOverflow = true;
                        ppustatus |= 0x20; // Set sprite overflow flag
                    }
                }
            }
        }

        // VBlank
        if (scanline == 241 && cycle == 1) {
            ppustatus |= 0x80;
            if (ppuctrl & 0x80) nmiOccurred = true;
        }
        
        // Pre-render scanline: Clear flags
        if (scanline == 261 && cycle == 1) {
            ppustatus &= ~0xE0; // Clear VBlank, Sprite 0 Hit, Sprite Overflow
            sprite0InSecondary = false;
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

void PPU::checkSprite0Hit(int x, int y, bool bgOpaque, bool spriteOpaque) {
    if (!sprite0InSecondary) return;
    if (x >= 255) return;
    if (scanline >= 240) return;
    if (!(ppumask & 0x18)) return;
    if (ppustatus & 0x40) return;
    
    if (bgOpaque && spriteOpaque) {
        ppustatus |= 0x40;
    }
}

// Loopy register helpers
void PPU::incrementX() {
    if ((vramAddr & 0x001F) == 31) {
        vramAddr &= ~0x001F;  // Reset coarse X to 0
        vramAddr ^= 0x0400;   // Switch horizontal nametable
    } else {
        vramAddr++;  // Increment coarse X
    }
}

void PPU::incrementY() {
    if ((vramAddr & 0x7000) != 0x7000) {
        vramAddr += 0x1000;  // Increment fine Y
    } else {
        vramAddr &= ~0x7000;  // Reset fine Y to 0
        int y = (vramAddr & 0x03E0) >> 5;  // Get coarse Y
        if (y == 29) {
            y = 0;
            vramAddr ^= 0x0800;  // Switch vertical nametable
        } else if (y == 31) {
            y = 0;  // Out of bounds, wrap to 0
        } else {
            y++;
        }
        vramAddr = (vramAddr & ~0x03E0) | (y << 5);
    }
}

void PPU::copyX() {
    // Copy horizontal bits from t to v
    vramAddr = (vramAddr & 0xFBE0) | (tempAddr & 0x041F);
}

void PPU::copyY() {
    // Copy vertical bits from t to v
    vramAddr = (vramAddr & 0x841F) | (tempAddr & 0x7BE0);
}

void PPU::loadBackgroundShifters() {
    bgShiftPatternLo = (bgShiftPatternLo & 0xFF00) | bgNextTileLo;
    bgShiftPatternHi = (bgShiftPatternHi & 0xFF00) | bgNextTileHi;
    
    // Attributes handling is tricky, simplified here:
    // Expand 2-bit attribute to 8 bits (00 => 00000000, 01 => 11111111 usually? No, it's 2 bits per pixel)
    // Actually, getting the right bit for the quadrant.
    // For now simplistic approach:
    uint8_t attr = bgNextTileAttr; 
    bgShiftAttrLo = (bgShiftAttrLo & 0xFF00) | ((attr & 1) ? 0xFF : 0);
    bgShiftAttrHi = (bgShiftAttrHi & 0xFF00) | ((attr & 2) ? 0xFF : 0);
}

void PPU::updateShifters() {
    if (ppumask & 0x18) { // If rendering enabled
        bgShiftPatternLo <<= 1;
        bgShiftPatternHi <<= 1;
        bgShiftAttrLo <<= 1;
        bgShiftAttrHi <<= 1;
    }
}

void PPU::renderPixel() {
    uint8_t bgPixel = 0;
    uint8_t bgPalette = 0;
    bool bgOpaque = false;
    int x = cycle - 1;
    
    // 1. Background Pixel
    if (ppumask & 0x08) {
        // Bit selection based on fineX
        // Since we shift left every cycle, the bit for the current pixel is always at MSB (0x8000)
        // adjusted by fineX?
        // Actually, NES standard shift registers shift left.
        // The pixel to output is bit 15 - fineX.
        uint16_t mask = 0x8000 >> fineX;
        
        uint8_t p0 = (bgShiftPatternLo & mask) ? 1 : 0;
        uint8_t p1 = (bgShiftPatternHi & mask) ? 1 : 0;
        bgPixel = (p1 << 1) | p0;
        
        uint8_t a0 = (bgShiftAttrLo & mask) ? 1 : 0;
        uint8_t a1 = (bgShiftAttrHi & mask) ? 1 : 0;
        bgPalette = (a1 << 1) | a0;
        
        bgOpaque = (bgPixel != 0);
    }
    
    // 2. Sprite Pixel (Simple iteration for now)
    uint8_t sprPixel = 0;
    uint8_t sprPalette = 0;
    bool sprPriority = false; // false = front
    bool sprOpaque = false;
    bool isSprite0 = false;
    
    if (ppumask & 0x10) {
        // Iterate front-to-back because we want the first sprite that matches
        // (Priority is first in OAM wins)
        // Wait, Secondary OAM is already sorted by X? No, by index in OAM.
        // Iterate through valid sprites
        // Iterate through valid sprites
        for (int i = 0; i < spriteCount; i++) {
            int idx = i * 4;
            int sy = secondaryOAM[idx];
            int st = secondaryOAM[idx + 1];
            int sa = secondaryOAM[idx + 2];
            int sx = secondaryOAM[idx + 3];
            
            if (x >= sx && x < sx + 8) {
                // Determine sprite row
                int row = scanline - sy; // 0-7 or 0-15
                // Handle V-Flip
                if (sa & 0x80) row = ((ppuctrl & 0x20) ? 15 : 7) - row;
                
                // Fetch pattern
                // Optimization: Cached fetches would be better, but doing it here for simplicity
                uint16_t patternBase = (ppuctrl & 0x08) ? 0x1000 : 0x0000;
                
                // 8x16 mode check
                if (ppuctrl & 0x20) {
                    patternBase = (st & 1) ? 0x1000 : 0x0000;
                    st &= 0xFE;
                    if (row >= 8) { st++; row -= 8; }
                }
                
                uint8_t p0 = vramRead(patternBase + (st * 16) + row);
                uint8_t p1 = vramRead(patternBase + (st * 16) + row + 8);
                
                // Handle H-Flip
                int col = x - sx; // 0-7
                if (sa & 0x40) col = 7 - col;
                
                uint8_t bit0 = (p0 >> (7 - col)) & 1;
                uint8_t bit1 = (p1 >> (7 - col)) & 1;
                uint8_t pixel = (bit1 << 1) | bit0;
                
                if (pixel != 0) {
                    sprPixel = pixel;
                    sprPalette = (sa & 0x03) + 4; // Sprites use palette 4-7
                    sprPriority = (sa & 0x20);
                    sprOpaque = true;
                    if (i == 0 && sprite0InSecondary) isSprite0 = true;
                    break; // Found highest priority sprite
                }
            }
        }
    }
    
    // 3. Sprite 0 Hit
    if (isSprite0 && bgOpaque && sprOpaque && x < 255) {
        if ((ppumask & 0x1E) == 0x1E) { // Ensure both BG and Sprites enabled (and not clipped? close enough)
             ppustatus |= 0x40;
        }
    }
    
    // 4. Multiplexing
    uint8_t finalPixel = 0;
    uint8_t finalPalette = 0;
    
    if (bgOpaque && sprOpaque) {
         if (sprPriority) { // Behind BG
             finalPixel = bgPixel;
             finalPalette = bgPalette;
         } else { // Front of BG
             finalPixel = sprPixel;
             finalPalette = sprPalette;
         }
    } else if (sprOpaque) {
        finalPixel = sprPixel;
        finalPalette = sprPalette;
    } else {
        finalPixel = bgPixel;
        finalPalette = bgPalette;
    }
    
    // 5. Output
    uint16_t paletteIndex = paletteTable[finalPalette * 4 + finalPixel];
    if (finalPixel == 0) paletteIndex = paletteTable[0]; // Global background color
    
    // Apply Grayscale
    if (ppumask & 0x01) paletteIndex &= 0x30;
    
    // Write to buffer
    // Note: Emulation of emphasis bits (5-7) requires color modification, 
    // skipping for now as nesPalette is pre-computed.
    if (pixelBuffer) {
        pixelBuffer[scanline * SCREEN_WIDTH + (cycle - 1)] = nesPalette[paletteIndex & 0x3F];
    }
}
