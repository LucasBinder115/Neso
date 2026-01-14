#ifndef RENDERER_H
#define RENDERER_H

#include <cstdint>
#include "ppu.h"

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 240

void renderScreen(uint32_t* pixelBuffer, PPU& ppu);

#endif
