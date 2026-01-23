/* 
 * Mapper Module (Base & Implementation)
 * Responsibility: Handle cartridge banking, PRG/CHR ROM mapping, and Nametable mirroring.
 * Supported Mappers: 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 7 (AxROM).
 */

#ifndef MAPPER_H
#define MAPPER_H

#include <cstdint>
#include "rom.h"

enum class MirrorMode {
    Horizontal,
    Vertical,
    SingleScreenLower,
    SingleScreenUpper
};

class Mapper {
public:
    Mapper(Rom* rom) : rom(rom) {}
    virtual ~Mapper() {}

    virtual uint8_t cpuRead(uint16_t addr) {
        if (addr >= 0x6000 && addr <= 0x7FFF) return prgRam[addr - 0x6000];
        return 0;
    }
    virtual void cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) {
        if (addr >= 0x6000 && addr <= 0x7FFF) prgRam[addr - 0x6000] = val;
    }
    virtual uint8_t ppuRead(uint16_t addr) = 0;
    virtual void ppuWrite(uint16_t addr, uint8_t val) = 0;
    virtual void reset() {}

    uint16_t getMirrorAddr(uint16_t addr, MirrorMode mode) {
        uint16_t ntAddr = addr & 0x0FFF;
        switch (mode) {
            case MirrorMode::Horizontal:
                return ((ntAddr & 0x0800) >> 1) | (ntAddr & 0x03FF);
            case MirrorMode::Vertical:
                return ntAddr & 0x07FF;
            case MirrorMode::SingleScreenLower:
                return ntAddr & 0x03FF;
            case MirrorMode::SingleScreenUpper:
                return 0x0400 | (ntAddr & 0x03FF);
            default:
                return ntAddr & 0x07FF;
        }
    }

protected:
    Rom* rom;
    uint8_t ppuVram[2048] = {0}; // 2KB internal Nametable memory
    uint8_t prgRam[8192] = {0};  // 8KB Work/PRG RAM ($6000-$7FFF)
};

class Mapper0 : public Mapper {
public:
    Mapper0(Rom* rom);
    uint8_t cpuRead(uint16_t addr) override;
    void cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) override;
    uint8_t ppuRead(uint16_t addr) override;
    void ppuWrite(uint16_t addr, uint8_t val) override;
    void reset() override;
};

class Mapper2 : public Mapper { // UxROM
public:
    Mapper2(Rom* rom);
    uint8_t cpuRead(uint16_t addr) override;
    void cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) override;
    uint8_t ppuRead(uint16_t addr) override;
    void ppuWrite(uint16_t addr, uint8_t val) override;
    void reset() override;
private:
    uint16_t prgBankSelect = 0;
    int numPrgBanks = 0;
    int prgBankMask = 0;
};

class Mapper3 : public Mapper { // CNROM
public:
    Mapper3(Rom* rom);
    uint8_t cpuRead(uint16_t addr) override;
    void cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) override;
    uint8_t ppuRead(uint16_t addr) override;
    void ppuWrite(uint16_t addr, uint8_t val) override;
    void reset() override;
private:
    uint8_t chrBankSelect = 0;
    int numChrBanks = 0;
    int chrBankMask = 0;
};

class Mapper1 : public Mapper { // MMC1
public:
    Mapper1(Rom* rom);
    uint8_t cpuRead(uint16_t addr) override;
    void cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) override;
    uint8_t ppuRead(uint16_t addr) override;
    void ppuWrite(uint16_t addr, uint8_t val) override;
public:
    void reset() override;
    void updateOffsets();
    uint8_t shiftReg = 0x10;
    uint8_t control = 0x0C;
    uint8_t prgBank = 0;
    uint8_t chrBank0 = 0;
    uint8_t chrBank1 = 0;
    uint32_t prgOffsets[2] = {0};
    uint32_t chrOffsets[2] = {0};
    uint64_t lastWriteCycle = 0;
    int numPrgBanks = 0;
    int numChrBanks = 0;
};

class Mapper7 : public Mapper { // AOROM
public:
    Mapper7(Rom* rom);
    uint8_t cpuRead(uint16_t addr) override;
    void cpuWrite(uint16_t addr, uint8_t val, uint64_t cycles) override;
    uint8_t ppuRead(uint16_t addr) override;
    void ppuWrite(uint16_t addr, uint8_t val) override;
    void reset() override;
private:
    uint8_t prgBank = 0;
    uint8_t mirroring = 0; // 0=screenA, 1=screenB
    int numPrgBanks = 0;
    int prgBankMask = 0;
};

#endif
