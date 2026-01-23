#include "ppu.h"
#include <cstring>
#include <android/log.h>
#include "cpu.h"
#include "mapper.h"
#include "palette.h"
#include "renderer.h"

void PPU::reset() {
    memset(paletteTable, 0, sizeof(paletteTable));
    memset(sprites, 0, sizeof(sprites));
    memset(secondaryOAM, 0xFF, sizeof(secondaryOAM));
    
    spriteCount = 0;
    sprite0InSecondary = false;
    spriteOverflow = false;
    oddFrame = false;
    
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
    
    // NMI Suppression: If VBlank is read while it's being set, 
    // it's cleared in the result and NMI is suppressed for this frame.
    if (scanline == 241 && cycle == 1) {
        res &= ~0x80;
        nmiOccurred = false; 
    }
    
    ppustatus &= ~0x80;
    writeToggle = false; 
    return res;
}

uint8_t PPU::readRegister(uint16_t addr) {
    uint16_t reg = 0x2000 + (addr % 8);
    switch (reg) {
        case 0x2002: return readStatus();
        case 0x2004: return 0; // OAMDATA read not implemented
        case 0x2007: {
            uint8_t data = vramRead(vramAddr);
            if (vramAddr < 0x3F00) {
                uint8_t buffered = readBuffer;
                readBuffer = data;
                data = buffered;
            } else {
                readBuffer = mapper ? mapper->ppuRead(vramAddr - 0x1000) : 0;
            }
            vramAddr += (ppuctrl & 0x04) ? 32 : 1;
            return data;
        }
        default: return 0;
    }
}

void PPU::writeRegister(uint16_t addr, uint8_t val) {
    uint16_t reg = 0x2000 + (addr % 8);
    switch (reg) {
        case 0x2000: {
            uint8_t oldCtrl = ppuctrl;
            ppuctrl = val;
            if (!(oldCtrl & 0x80) && (val & 0x80) && (ppustatus & 0x80)) {
                nmiOccurred = true;
            }
            tempAddr = (tempAddr & 0xF3FF) | ((val & 0x03) << 10);
            break;
        }
        case 0x2001: ppumask = val; break;
        case 0x2003: oamAddr = val; break;
        case 0x2004: {
            ((uint8_t*)sprites)[oamAddr++] = val;
            break;
        }
        case 0x2005: {
            if (!writeToggle) {
                tempAddr = (tempAddr & 0xFFE0) | (val >> 3);
                fineX = val & 0x07;
            } else {
                tempAddr = (tempAddr & 0x8FFF) | ((val & 0x07) << 12);
                tempAddr = (tempAddr & 0xFC1F) | ((val & 0xF8) << 2);
            }
            writeToggle = !writeToggle;
            break;
        }
        case 0x2006: {
            if (!writeToggle) {
                tempAddr = (tempAddr & 0x00FF) | ((val & 0x3F) << 8);
            } else {
                tempAddr = (tempAddr & 0xFF00) | val;
                vramAddr = tempAddr;
            }
            writeToggle = !writeToggle;
            break;
        }
        case 0x2007: {
            vramWrite(vramAddr, val);
            vramAddr += (ppuctrl & 0x04) ? 32 : 1;
            break;
        }
    }
}

void PPU::step(int cpuCycles, CPU* cpu) {
    int ppuCycles = cpuCycles * 3;
    for (int i = 0; i < ppuCycles; i++) {
        // Odd Frame Skip
        if (scanline == 261 && cycle == 339 && oddFrame && (ppumask & 0x18)) {
            cycle = 0;
            scanline = 0;
            oddFrame = !oddFrame;
            ppustatus &= ~0xE0;
            continue;
        }

        cycle++;
        if (cycle >= 341) {
            cycle = 0;
            scanline++;
            if (scanline >= 262) {
                scanline = 0;
                oddFrame = !oddFrame;
                ppustatus &= ~0xE0;
            }
        }

        processBackground();
        processSprites();
        handleVBlank();
    }
}

void PPU::processBackground() {
    bool isVisibleOrPrerender = (scanline < 240) || (scanline == 261);
    if (isVisibleOrPrerender && (ppumask & 0x18)) {
        updateShifters();
        
        if (scanline < 240 && cycle >= 1 && cycle <= 256) {
            renderPixel();
        }
        
        if ((cycle >= 1 && cycle <= 256) || (cycle >= 321 && cycle <= 336)) {
            int step = (cycle - 1) % 8;
            if (step == 1) {
                uint16_t ntAddr = 0x2000 | (vramAddr & 0x0FFF);
                bgNextTileId = vramRead(ntAddr);
            }
            if (step == 3) {
                uint16_t atAddr = 0x23C0 | (vramAddr & 0x0C00) | ((vramAddr >> 4) & 0x38) | ((vramAddr >> 2) & 0x07);
                bgNextTileAttr = vramRead(atAddr);
                if (vramAddr & 0x0040) bgNextTileAttr >>= 4;
                if (vramAddr & 0x0002) bgNextTileAttr >>= 2;
                bgNextTileAttr &= 0x03;
            }
            if (step == 5) {
                uint16_t patternAddr = ((ppuctrl & 0x10) ? 0x1000 : 0x0000) + ((uint16_t)bgNextTileId << 4) + ((vramAddr >> 12) & 0x07);
                bgNextTileLo = vramRead(patternAddr);
            }
            if (step == 7) {
                uint16_t patternAddr = ((ppuctrl & 0x10) ? 0x1000 : 0x0000) + ((uint16_t)bgNextTileId << 4) + ((vramAddr >> 12) & 0x07) + 8;
                bgNextTileHi = vramRead(patternAddr);
                incrementX();
                loadBackgroundShifters();
            }
            if (cycle == 256) incrementY();
        }
        
        if (cycle == 257) copyX();
        if (scanline == 261 && cycle >= 280 && cycle <= 304) copyY();
    }
}

void PPU::processSprites() {
    bool isVisibleOrPrerender = (scanline < 240) || (scanline == 261);
    if (!isVisibleOrPrerender) return;

    // Cycles 1-64: Clear secondary OAM
    if (cycle >= 1 && cycle <= 64) {
        int idx = ((cycle - 1) / 2) % 32;
        if (cycle % 2 == 0) secondaryOAM[idx] = 0xFF;
    }
    
    // Cycle 257: Instant Sprite Evaluation & Pre-fetch for NEXT scanline
    if (cycle == 257 && scanline < 239) { // We evaluate for scanline + 1
        int nextScanline = scanline + 1;
        spriteCount = 0;
        sprite0InSecondary = false;
        
        int n = 0; 
        int spriteHeight = (ppuctrl & 0x20) ? 16 : 8;

        while (n < 64 && spriteCount < 8) {
            int sy = sprites[n].y;
            int diff = nextScanline - sy;
            if (diff >= 0 && diff < spriteHeight) {
                int secIdx = spriteCount * 4;
                secondaryOAM[secIdx + 0] = sprites[n].y;
                secondaryOAM[secIdx + 1] = sprites[n].tile_index;
                secondaryOAM[secIdx + 2] = sprites[n].attributes;
                secondaryOAM[secIdx + 3] = sprites[n].x;
                
                // Pre-fetch Pattern Data
                int row = nextScanline - sy;
                uint8_t sa = sprites[n].attributes;
                uint8_t st = sprites[n].tile_index;
                if (sa & 0x80) row = ((ppuctrl & 0x20) ? 15 : 7) - row;
                
                uint16_t patternBase = (ppuctrl & 0x08) ? 0x1000 : 0x0000;
                if (ppuctrl & 0x20) {
                    patternBase = (st & 1) ? 0x1000 : 0x0000;
                    st &= 0xFE;
                    if (row >= 8) { st++; row -= 8; }
                }
                
                spriteFetchedLo[spriteCount] = vramRead(patternBase + (st * 16) + row);
                spriteFetchedHi[spriteCount] = vramRead(patternBase + (st * 16) + row + 8);

                if (n == 0) sprite0InSecondary = true;
                spriteCount++;
            }
            n++;
        }

        // Sprite Overflow
        int m = 0;
        while (n < 64) {
            int y = ((uint8_t*)sprites)[n * 4 + m];
            int diff = nextScanline - y;
            if (diff >= 0 && diff < spriteHeight) {
                ppustatus |= 0x20;
                break;
            } else {
                n++;
                m = (m + 1) & 0x03;
            }
        }
    }
}

void PPU::handleVBlank() {
    if (scanline == 241 && cycle == 1) {
        ppustatus |= 0x80;
        if (ppuctrl & 0x80) nmiOccurred = true;
    }
    
    if (scanline == 261 && cycle == 1) {
        ppustatus &= ~0xE0;
        sprite0InSecondary = false;
    }
}

uint8_t PPU::vramRead(uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x3F00) {
        return mapper ? mapper->ppuRead(addr) : 0;
    } else {
        uint16_t paletteAddr = addr & 0x001F;
        if (paletteAddr >= 0x10 && (paletteAddr & 0x03) == 0) paletteAddr -= 0x10;
        return paletteTable[paletteAddr & 0x1F];
    }
}

void PPU::vramWrite(uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;
    if (addr < 0x3F00) {
        if (mapper) mapper->ppuWrite(addr, val);
    } else {
        uint16_t paletteAddr = addr & 0x001F;
        if (paletteAddr >= 0x10 && (paletteAddr & 0x03) == 0) paletteAddr -= 0x10;
        paletteTable[paletteAddr & 0x1F] = val;
    }
}

// Obsolete - functionality integrated into renderPixel for cycle-accuracy
// checkSprite0Hit was removed as its logic is now integrated into renderPixel

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
    
    // Attributes handling: Expand 2-bit attribute to 8 bits for the next 8 pixels
    bgShiftAttrLo = (bgShiftAttrLo & 0xFF00) | ((bgNextTileAttr & 1) ? 0x00FF : 0x0000);
    bgShiftAttrHi = (bgShiftAttrHi & 0xFF00) | ((bgNextTileAttr & 2) ? 0x00FF : 0x0000);
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
    bool bgVisible = (ppumask & 0x08);
    if (bgVisible) {
        // Handle background clipping (bit 1 of $2001)
        if (x >= 8 || (ppumask & 0x02)) {
            // Bit selection based on fineX
            uint16_t mask = 0x8000 >> fineX;
            
            uint8_t p0 = (bgShiftPatternLo & mask) ? 1 : 0;
            uint8_t p1 = (bgShiftPatternHi & mask) ? 1 : 0;
            bgPixel = (p1 << 1) | p0;
            
            uint8_t a0 = (bgShiftAttrLo & mask) ? 1 : 0;
            uint8_t a1 = (bgShiftAttrHi & mask) ? 1 : 0;
            bgPalette = (a1 << 1) | a0;
            
            bgOpaque = (bgPixel != 0);
        }
    }
    
    // 2. Sprite Pixel
    uint8_t sprPixel = 0;
    uint8_t sprPalette = 0;
    bool sprPriority = false; // false = front
    bool sprOpaque = false;
    bool isSprite0 = false;
    
    bool sprVisible = (ppumask & 0x10);
    if (sprVisible) {
        // Handle sprite clipping (bit 2 of $2001)
        if (x >= 8 || (ppumask & 0x04)) {
            // Iterate front-to-back because we want the first sprite that matches
            for (int i = 0; i < spriteCount; i++) {
                int idx = i * 4;
                int sa = secondaryOAM[idx + 2];
                int sx = secondaryOAM[idx + 3];
                
                if (x >= sx && x < sx + 8) {
                    uint8_t p0 = spriteFetchedLo[i];
                    uint8_t p1 = spriteFetchedHi[i];
                    
                    int col = x - sx;
                    if (sa & 0x40) col = 7 - col;
                    
                    uint8_t bit0 = (p0 >> (7 - col)) & 1;
                    uint8_t bit1 = (p1 >> (7 - col)) & 1;
                    uint8_t pixel = (bit1 << 1) | bit0;
                    
                    if (pixel != 0) {
                        sprPixel = pixel;
                        sprPalette = (sa & 0x03) + 4;
                        sprPriority = (sa & 0x20);
                        sprOpaque = true;
                        if (i == 0 && sprite0InSecondary) isSprite0 = true;
                        break;
                    }
                }
            }
        }
    }
    
    // 3. Sprite 0 Hit
    if (isSprite0 && bgOpaque && sprOpaque && x < 255) {
        // Double check rendering enabled for both
        if ((ppumask & 0x18) == 0x18) {
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
    if (pixelBuffer) {
        int index = scanline * SCREEN_WIDTH + (cycle - 1);
        if (index >= 0 && index < SCREEN_WIDTH * SCREEN_HEIGHT) {
            pixelBuffer[index] = nesPalette[paletteIndex & 0x3F];
        }
    }
}
