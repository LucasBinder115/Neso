#include "renderer.h"
#include "mapper.h"
#include "palette.h"
#include <android/log.h>

void renderScreen(uint32_t* pixelBuffer, PPU& ppu) {
    if (!ppu.mapper) return;

    bool showBackground = (ppu.ppumask & 0x08);
    bool showSprites = (ppu.ppumask & 0x10);

    // Initial clear with Universal Background Color
    uint32_t bgColor = nesPalette[ppu.paletteTable[0] & 0x3F];
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) pixelBuffer[i] = bgColor;

    if (showBackground) {
        uint16_t bgPatternBase = (ppu.ppuctrl & 0x10) ? 0x1000 : 0x0000;
        int fineX = ppu.scrollX % 8;
        int fineY = ppu.scrollY % 8;
        int startTileX = ppu.scrollX / 8;
        int startTileY = ppu.scrollY / 8;

        for (int row = 0; row < 31; row++) {
            int tileY = (startTileY + row) % 60;
            uint16_t rowNT = 0x2000 + (ppu.ppuctrl & 0x03) * 0x400;
            if (tileY >= 30) rowNT ^= 0x0800;

            for (int col = 0; col < 33; col++) {
                int tileX = (startTileX + col) % 64;
                uint16_t currentNT = rowNT;
                if (tileX >= 32) currentNT ^= 0x0400;

                uint16_t ntAddr = currentNT + (tileY % 30) * 32 + (tileX % 32);
                uint8_t tileId = ppu.mapper->ppuRead(ntAddr);

                uint16_t attrAddr = currentNT + 0x3C0 + ((tileY % 30) / 4) * 8 + ((tileX % 32) / 4);
                uint8_t attrByte = ppu.mapper->ppuRead(attrAddr);
                uint8_t paletteIdx = (attrByte >> ((((tileY % 30) / 2) % 2) * 4 + (((tileX % 32) / 2) % 2) * 2)) & 0x03;

                for (int py = 0; py < 8; py++) {
                    int pixelY = row * 8 + py - fineY;
                    if (pixelY < 0 || pixelY >= SCREEN_HEIGHT) continue;

                    uint8_t p1 = ppu.mapper->ppuRead(bgPatternBase + (tileId * 16) + py);
                    uint8_t p2 = ppu.mapper->ppuRead(bgPatternBase + (tileId * 16) + py + 8);

                    for (int px = 0; px < 8; px++) {
                        int pixelX = col * 8 + px - fineX;
                        if (pixelX < 0 || pixelX >= SCREEN_WIDTH) continue;

                        uint8_t colorBit = ((p1 >> (7 - px)) & 1) | (((p2 >> (7 - px)) & 1) << 1);
                        if (colorBit != 0) {
                            uint8_t finalColorIdx = ppu.paletteTable[paletteIdx * 4 + colorBit];
                            pixelBuffer[pixelY * SCREEN_WIDTH + pixelX] = nesPalette[finalColorIdx & 0x3F];
                        }
                    }
                }
            }
        }
    }

    if (showSprites) {
        uint16_t spritePatternBase = (ppu.ppuctrl & 0x08) ? 0x1000 : 0x0000;
        for (int i = 63; i >= 0; i--) {
            Sprite& s = ppu.sprites[i];
            if (s.y >= 239 || s.y == 0) continue;

            uint8_t tileId = s.tile_index;
            uint8_t paletteIdx = (s.attributes & 0x03);
            bool flipH = (s.attributes & 0x40);
            bool flipV = (s.attributes & 0x80);

            for (int py = 0; py < 8; py++) {
                int row = flipV ? (7 - py) : py;
                uint8_t p1 = ppu.mapper->ppuRead(spritePatternBase + (tileId * 16) + row);
                uint8_t p2 = ppu.mapper->ppuRead(spritePatternBase + (tileId * 16) + row + 8);

                for (int px = 0; px < 8; px++) {
                    int col = flipH ? px : (7 - px);
                    uint8_t colorBit = ((p1 >> col) & 1) | (((p2 >> col) & 1) << 1);
                    if (colorBit != 0) {
                        int outX = s.x + px;
                        int outY = s.y + 1 + py;
                        if (outX < SCREEN_WIDTH && outY < SCREEN_HEIGHT) {
                            uint8_t finalColorIdx = ppu.paletteTable[16 + paletteIdx * 4 + colorBit];
                            pixelBuffer[outY * SCREEN_WIDTH + outX] = nesPalette[finalColorIdx & 0x3F];
                        }
                    }
                }
            }
        }
    }
}
