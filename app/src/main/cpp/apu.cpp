#include "apu.h"

const uint8_t SquareChannel::DUTIES[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0}, // 25%
    {0, 1, 1, 1, 1, 0, 0, 0}, // 50%
    {1, 0, 0, 1, 1, 1, 1, 1}  // 75%
};

void APU::reset() {
    square1.enabled = false;
    square1.volume = 0;
    square1.timer = 0;
    square1.timerCounter = 0;
    square1.dutyPos = 0;
    square1.dutyCycle = 2;
    accumulatedCycles = 0;
}

void APU::write(uint16_t addr, uint8_t val) {
    switch (addr) {
        case 0x4000:
            square1.volume = val & 0x0F;
            square1.dutyCycle = (val >> 6) & 0x03;
            break;
        case 0x4002:
            square1.timer = (square1.timer & 0x0700) | val;
            break;
        case 0x4003:
            square1.timer = (square1.timer & 0x00FF) | ((val & 0x07) << 8);
            square1.dutyPos = 0;
            break;
        case 0x4015:
            square1.enabled = (val & 0x01);
            break;
    }
}

void APU::step(int cycles) {
    static int genCounter = 0;
    static int totalGen = 0;

    for (int i = 0; i < cycles; i++) {
        // Pulse timer decrements every 2 CPU cycles
        static bool phase = false;
        phase = !phase;
        if (phase) square1.step();
        
        accumulatedCycles += 1.0;
        if (accumulatedCycles >= CYCLES_PER_SAMPLE) {
            uint8_t vol = square1.getOutput();
            uint8_t sample = 128 + (vol * 4); 
            ringBuffer.write(sample);
            accumulatedCycles -= CYCLES_PER_SAMPLE;
            totalSamplesGenerated++;
        }
    }

    if (++genCounter % 3000 == 0) { // Approx once per frame (30k cycles)
        // LOGD is fine here as it's infrequent
        // We'll use JNI bridge for the combined log to avoid spam
    }
}
