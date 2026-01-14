#ifndef MAPPER_H
#define MAPPER_H

#include <cstdint>
#include "rom.h"

class Mapper {
public:
    Mapper(Rom* rom) : rom(rom) {}
    virtual ~Mapper() {}

    virtual uint8_t cpuRead(uint16_t addr) = 0;
    virtual void cpuWrite(uint16_t addr, uint8_t val) = 0;
    virtual uint8_t ppuRead(uint16_t addr) = 0;
    virtual void ppuWrite(uint16_t addr, uint8_t val) = 0;

protected:
    Rom* rom;
    uint8_t ppuVram[2048] = {0}; // 2KB para Nametables
};

class Mapper0 : public Mapper {
public:
    Mapper0(Rom* rom) : Mapper(rom) {}
    uint8_t cpuRead(uint16_t addr) override;
    void cpuWrite(uint16_t addr, uint8_t val) override;
    uint8_t ppuRead(uint16_t addr) override;
    void ppuWrite(uint16_t addr, uint8_t val) override;
};

class Mapper2 : public Mapper { // UxROM
public:
    Mapper2(Rom* rom) : Mapper(rom) {}
    uint8_t cpuRead(uint16_t addr) override;
    void cpuWrite(uint16_t addr, uint8_t val) override;
    uint8_t ppuRead(uint16_t addr) override;
    void ppuWrite(uint16_t addr, uint8_t val) override;
private:
    uint16_t prgBankSelect = 0;
};

class Mapper3 : public Mapper { // CNROM
public:
    Mapper3(Rom* rom) : Mapper(rom) {}
    uint8_t cpuRead(uint16_t addr) override;
    void cpuWrite(uint16_t addr, uint8_t val) override;
    uint8_t ppuRead(uint16_t addr) override;
    void ppuWrite(uint16_t addr, uint8_t val) override;
private:
    uint8_t chrBankSelect = 0;
};

#endif
