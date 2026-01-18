#ifndef CPU_H
#define CPU_H

#include <cstdint>
#include <android/log.h>

struct PPU; // Forward declaration
struct APU; // Forward declaration

struct Controller {
    uint8_t buttons = 0;       // Estado atual (A B Select Start Up Down Left Right)
    uint8_t shiftRegister = 0; // Registro de deslocamento para leitura serial

    void latch() {
        shiftRegister = buttons;
    }

    uint8_t read() {
        uint8_t bit = shiftRegister & 1;
        shiftRegister >>= 1;
        if (bit) __android_log_print(ANDROID_LOG_DEBUG, "NesoInput", "Button bit: %d (Shift: %d)", bit, shiftRegister);
        return bit | 0x40; 
    }
};

struct CPU {
    // Registradores 6502
    uint8_t a;      // Acumulador
    uint8_t x;      // Registrador X
    uint8_t y;      // Registrador Y
    uint8_t sp;     // Stack Pointer
    uint16_t pc;    // Program Counter
    uint8_t status; // Flags (N V - B D I Z C)

    uint8_t ram[2048];    // 2KB de RAM de trabalho ($0000-$07FF)
    
    PPU* ppu; // Referência para a PPU para Memory Mapped I/O
    class Mapper* mapper = nullptr; // Referência para o Cartucho
    Controller controller; // Controle NES 1
    struct APU* apu = nullptr; // Referência para a APU

    void reset();
    int step(); // Agora retorna o número de ciclos consumidos
    void triggerNMI();
    
    int cyclesToStall = 0; // Para DMA ($4014)
    uint64_t totalCycles = 0; // Para Mappers e APU timing

    void setZN(uint8_t val);
    
    // Memory Map
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