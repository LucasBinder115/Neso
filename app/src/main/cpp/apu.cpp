#include "apu.h"

const uint8_t SquareChannel::DUTIES[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0}, // 25%
    {0, 1, 1, 1, 1, 0, 0, 0}, // 50%
    {1, 0, 0, 1, 1, 1, 1, 1}  // 75%
};

const uint8_t SquareChannel::LENGTH_TABLE[32] = {
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

void APU::reset() {
    square1.enabled = false;
    square1.timerPeriod = 0;
    square1.timerValue = 0;
    square1.dutyPos = 0;
    square1.dutyCycle = 2;
    square1.lengthCounter = 0;
    square1.envelopeVolume = 0;
    square1.envelopeStart = false;
    
    square2.enabled = false;
    square2.timerPeriod = 0;
    square2.timerValue = 0;
    square2.dutyPos = 0;
    square2.dutyCycle = 2;
    square2.lengthCounter = 0;
    square2.envelopeVolume = 0;
    square2.envelopeStart = false;
    
    accumulatedCycles = 0;
    totalSamplesGenerated = 0;
    
    frameCounterCycles = 0;
    frameStep = 0;
    frameCounterMode = false;
    frameIRQDisable = false;
}

void APU::write(uint16_t addr, uint8_t val) {
    switch (addr) {
        // Square 1
        case 0x4000:
            square1.dutyCycle = (val >> 6) & 0x03;
            square1.envelopeLoop = val & 0x20;
            square1.constantVolume = val & 0x10;
            square1.constantVolumeValue = val & 0x0F;
            break;
        case 0x4001:
            square1.sweepEnabled = val & 0x80;
            square1.sweepPeriod = (val >> 4) & 0x07;
            square1.sweepNegate = val & 0x08;
            square1.sweepShift = val & 0x07;
            square1.sweepReload = true;
            break;
        case 0x4002:
            square1.timerPeriod = (square1.timerPeriod & 0x0700) | val;
            break;
        case 0x4003:
            square1.timerPeriod = (square1.timerPeriod & 0x00FF) | ((val & 0x07) << 8);
            square1.dutyPos = 0;
            square1.envelopeStart = true;
            if (square1.enabled) {
                square1.lengthCounter = SquareChannel::LENGTH_TABLE[val >> 3];
            }
            break;
            
        // Square 2
        case 0x4004:
            square2.dutyCycle = (val >> 6) & 0x03;
            square2.envelopeLoop = val & 0x20;
            square2.constantVolume = val & 0x10;
            square2.constantVolumeValue = val & 0x0F;
            break;
        case 0x4005:
            square2.sweepEnabled = val & 0x80;
            square2.sweepPeriod = (val >> 4) & 0x07;
            square2.sweepNegate = val & 0x08;
            square2.sweepShift = val & 0x07;
            square2.sweepReload = true;
            break;
        case 0x4006:
            square2.timerPeriod = (square2.timerPeriod & 0x0700) | val;
            break;
        case 0x4007:
            square2.timerPeriod = (square2.timerPeriod & 0x00FF) | ((val & 0x07) << 8);
            square2.dutyPos = 0;
            square2.envelopeStart = true;
            if (square2.enabled) {
                square2.lengthCounter = SquareChannel::LENGTH_TABLE[val >> 3];
            }
            break;
            
        // Channel enable
        case 0x4015:
            square1.enabled = (val & 0x01);
            square2.enabled = (val & 0x02);
            if (!square1.enabled) square1.lengthCounter = 0;
            if (!square2.enabled) square2.lengthCounter = 0;
            break;
            
        // Frame Counter
        case 0x4017:
            frameCounterMode = (val & 0x80);
            frameIRQDisable = (val & 0x40);
            frameCounterCycles = 0;
            frameStep = 0;
            // Mode 1 (5-step) clocks immediately
            if (frameCounterMode) {
                clockQuarterFrame();
                clockHalfFrame();
            }
            break;
    }
}

void APU::step(int cycles) {
    for (int i = 0; i < cycles; i++) {
        // Frame Counter
        frameCounterCycles++;
        
        if (!frameCounterMode) {
            // 4-step mode
            switch (frameCounterCycles) {
                case 3729:
                    clockQuarterFrame();
                    break;
                case 7457:
                    clockQuarterFrame();
                    clockHalfFrame();
                    break;
                case 11186:
                    clockQuarterFrame();
                    break;
                case 14915:
                    clockQuarterFrame();
                    clockHalfFrame();
                    frameCounterCycles = 0;
                    break;
            }
        } else {
            // 5-step mode
            switch (frameCounterCycles) {
                case 3729:
                    clockQuarterFrame();
                    break;
                case 7457:
                    clockQuarterFrame();
                    clockHalfFrame();
                    break;
                case 11186:
                    clockQuarterFrame();
                    break;
                case 18641:
                    clockQuarterFrame();
                    clockHalfFrame();
                    frameCounterCycles = 0;
                    break;
            }
        }
        
        // NES APU: Pulse timers clock every 2 CPU cycles
        static bool apuClock = false;
        apuClock = !apuClock;
        
        if (apuClock) {
            square1.clockTimer();
            square2.clockTimer();
        }
        
        // Sample generation
        accumulatedCycles += 1.0;
        if (accumulatedCycles >= CYCLES_PER_SAMPLE) {
            uint8_t out1 = square1.getOutput();
            uint8_t out2 = square2.getOutput();
            
            // NES Mixer (non-linear)
            // pulse_out = 95.88 / ((8128 / (pulse1 + pulse2)) + 100)
            float pulse_sum = out1 + out2;
            float pulse_out = 0;
            if (pulse_sum > 0) {
                pulse_out = 95.88f / ((8128.0f / pulse_sum) + 100.0f);
            }
            
            // Convert to 8-bit unsigned (0-255, centered at 128)
            uint8_t sample = 128 + (uint8_t)(pulse_out * 127.0f);
            ringBuffer.write(sample);
            
            accumulatedCycles -= CYCLES_PER_SAMPLE;
            totalSamplesGenerated++;
        }
    }
}

void APU::clockQuarterFrame() {
    square1.clockEnvelope();
    square2.clockEnvelope();
}

void APU::clockHalfFrame() {
    square1.clockLength();
    square1.clockSweep(true);
    square2.clockLength();
    square2.clockSweep(false);
}
