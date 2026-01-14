#include "rom.h"
#include <cstring>
#include <android/log.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "NesoROM", __VA_ARGS__)

Rom::Rom(const uint8_t* data, size_t size) {
    if (size < 16) return;

    iNESHeader header;
    memcpy(&header, data, 16);

    // 1. Validate iNES Header: "NES" + 0x1A
    if (header.name[0] != 'N' || header.name[1] != 'E' || 
        header.name[2] != 'S' || header.name[3] != 0x1A) {
        return;
    }

    // 2. Parse Sizes
    prgSize = header.prg_chunks * 16384;
    chrSize = header.chr_chunks * 8192;
    
    // 3. Extract Mapper ID (iNES 1.0)
    mapperId = (header.flags6 >> 4) | (header.flags7 & 0xF0);

    // 4. Flags
    verticalMirroring = (header.flags6 & 0x01);
    // Note: bits for trainer, persistent memory, etc. can be added later

    // 5. Load PRG ROM
    size_t offset = 16;
    if (header.flags6 & 0x04) offset += 512; // Skip trainer if present

    if (size < offset + prgSize) return;
    prgROM.assign(data + offset, data + offset + prgSize);

    // 6. Load CHR ROM / Allocate CHR RAM
    if (chrSize > 0) {
        if (size < offset + prgSize + chrSize) return;
        chrROM.assign(data + offset + prgSize, data + offset + prgSize + chrSize);
    } else {
        // CHR RAM is standard 8KB for most NROM/NROM-ish boards
        chrROM.resize(8192, 0);
        chrSize = 8192; 
    }

    valid = true;
    LOGD("ROM Loaded! Mapper: %d, PRG: %zu, CHR: %zu, Mirror: %s", 
         mapperId, prgSize, chrSize, verticalMirroring ? "Vertical" : "Horizontal");
}
