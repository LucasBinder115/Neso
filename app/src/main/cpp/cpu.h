/* 
 * 6502 CPU Module
 * Responsibility: Instruction execution, Bus management, and Interrupt handling.
 * Part of the cycle-accurate synchronization loop.
 */

#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include <android/log.h>

struct PPU;
struct APU;
class Mapper;

struct Controller {
    uint8_t buttons = 0;       // Current button state (A B Select Start Up Down Left Right)
    uint8_t shiftRegister = 0; // Shift register for serial reading

    void latch() {
        shiftRegister = buttons;
    }

    uint8_t read() {
        uint8_t bit = shiftRegister & 1;
        shiftRegister >>= 1;
        return bit | 0x40; 
    }
};

struct CPU {
    // 6502 Registers
    uint8_t a;      // Accumulator
    uint8_t x;      // X Register
    uint8_t y;      // Y Register
    uint8_t sp;     // Stack Pointer
    uint16_t pc;    // Program Counter
    uint8_t status; // Status Flags (N V - B D I Z C)

    uint8_t ram[2048];    // 2KB Work RAM ($0000-$07FF)
    
    PPU* ppu; // Reference to PPU for Memory Mapped I/O
    class Mapper* mapper = nullptr; // Reference to Mapper
    Controller controller; // Controller Port 1
    struct APU* apu = nullptr; // Reference to APU

    void reset();
    int step(); // Returns number of cycles consumed
    void triggerNMI();
    void triggerIRQ();

    bool irqPending = false;
    int cyclesToStall = 0; // For DMA ($4014) handling
    uint64_t totalCycles = 0; // Cumulative cycles for timing

    inline void setZN(uint8_t val) {
        status = (status & ~0x82) | (val == 0 ? 0x02 : 0) | (val & 0x80);
    }
    
    // Memory Map (Defined in cpu.cpp to avoid circular deps if needed, but inlined for perf)
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t val);

private:
    void push(uint8_t val) { write(0x100 | sp--, val); }
    uint8_t pop() { return read(0x100 | ++sp); }
    uint16_t read16(uint16_t addr) {
        if (addr < 0x100) return read(addr) | (read((addr + 1) & 0xFF) << 8);
        return read(addr) | (read(addr + 1) << 8);
    }
};

void step(CPU& cpu);

#endif

// --- Inline Implementations ---
// We define these here (outside the struct but in the header) to allow include-based inlining
// while avoiding member function definition order issues.

#include "ppu.h"
#include "apu.h"
#include "mapper.h"

inline uint8_t CPU::read(uint16_t addr) {
    if (addr <= 0x1FFF) return ram[addr & 0x7FF];
    if (addr >= 0x2000 && addr <= 0x3FFF) return ppu->readRegister(addr);
    if (addr == 0x4015 && apu) return apu->readStatus();
    if (addr == 0x4016) return controller.read();
    if (addr >= 0x8000 && mapper) return mapper->cpuRead(addr);
    return 0x00;
}

inline void CPU::write(uint16_t addr, uint8_t val) {
    if (addr <= 0x1FFF) ram[addr & 0x7FF] = val;
    else if (addr >= 0x2000 && addr <= 0x3FFF) ppu->writeRegister(addr, val);
    else if (addr == 0x4014) {
        // OAM DMA: Copy 256 bytes to OAM
        uint16_t base = (uint16_t)val << 8;
        for (int i = 0; i < 256; i++) {
            ((uint8_t*)ppu->sprites)[i] = read(base + i);
        }
        cyclesToStall = 513;
    } else if (addr == 0x4016) {
        if (val & 1) controller.latch();
    } else if (addr >= 0x4000 && addr <= 0x4017 && apu) apu->write(addr, val);
    else if (addr >= 0x8000 && mapper) mapper->cpuWrite(addr, val, totalCycles);
}