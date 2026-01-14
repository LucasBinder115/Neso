#ifndef ROM_H
#define ROM_H

#include <cstdint>
#include <vector>

struct iNESHeader {
    uint8_t name[4];
    uint8_t prg_chunks;
    uint8_t chr_chunks;
    uint8_t flags6;
    uint8_t flags7;
    uint8_t prg_ram_size;
    uint8_t tv_system1;
    uint8_t tv_system2;
    uint8_t unused[5];
};

class Rom {
public:
    Rom(const uint8_t* data, size_t size);
    
    bool isValid() const { return valid; }
    size_t getPrgSize() const { return prgSize; }
    size_t getChrSize() const { return chrSize; }
    bool isVerticalMirroring() const { return verticalMirroring; }
    uint8_t getMapperId() const { return mapperId; }
    
    std::vector<uint8_t> prgROM;
    std::vector<uint8_t> chrROM;

private:
    bool valid = false;
    size_t prgSize = 0;
    size_t chrSize = 0;
    uint8_t mapperId = 0;
    bool verticalMirroring = false;
};

#endif
