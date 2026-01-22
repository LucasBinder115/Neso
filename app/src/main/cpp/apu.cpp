#include "apu.h"

const uint8_t SquareChannel::DUTIES[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0}, // 25%
    {0, 1, 1, 1, 1, 0, 0, 0}, // 50%
    {1, 0, 0, 1, 1, 1, 1, 1}  // 75%
};

const uint8_t APU::LENGTH_TABLE[32] = {
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

const uint8_t TriangleChannel::TRIANGLE_STEPS[32] = {
    15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};

const uint16_t NoiseChannel::PERIOD_TABLE[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};



void APU::reset() {
    square1 = {};
    square2 = {};
    triangle = {};
    noise = {};
    dmc = {};
    
    accumulatedCycles = 0;
    totalSamplesGenerated = 0;
    filterAccumulator = 128.0f;
    
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
            if (square1.enabled) {
                square1.lengthCounter = APU::LENGTH_TABLE[val >> 3];
            }
            square1.dutyPos = 0;
            square1.envelopeStart = true;
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
            if (square2.enabled) {
                square2.lengthCounter = APU::LENGTH_TABLE[val >> 3];
            }
            square2.dutyPos = 0;
            square2.envelopeStart = true;
            break;

        // Triangle
        case 0x4008:
            triangle.controlFlag = val & 0x80;
            triangle.linearCounterReload = val & 0x7F;
            break;
        case 0x400A:
            triangle.timerPeriod = (triangle.timerPeriod & 0x0700) | val;
            break;
        case 0x400B:
            triangle.timerPeriod = (triangle.timerPeriod & 0x00FF) | ((val & 0x07) << 8);
            if (triangle.enabled) {
                triangle.lengthCounter = APU::LENGTH_TABLE[val >> 3];
            }
            triangle.linearCounterReloadFlag = true;
            break;

        // Noise
        case 0x400C:
            noise.envelopeLoop = val & 0x20;
            noise.constantVolume = val & 0x10;
            noise.constantVolumeValue = val & 0x0F;
            break;
        case 0x400E:
            noise.mode = val & 0x80;
            noise.timerPeriod = NoiseChannel::PERIOD_TABLE[val & 0x0F];
            break;
        case 0x400F:
            if (noise.enabled) {
                noise.lengthCounter = APU::LENGTH_TABLE[val >> 3];
            }
            noise.envelopeStart = true;
            break;
            
        // Channel enable
        case 0x4015:
            square1.enabled = (val & 0x01);
            square2.enabled = (val & 0x02);
            triangle.enabled = (val & 0x04);
            noise.enabled = (val & 0x08);
            dmc.enabled = (val & 0x10);
            
            if (!square1.enabled) square1.lengthCounter = 0;
            if (!square2.enabled) square2.lengthCounter = 0;
            if (!triangle.enabled) triangle.lengthCounter = 0;
            if (!noise.enabled) noise.lengthCounter = 0;
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
            // 4-step mode (Approx 60Hz)
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
            // 5-step mode (Approx 48Hz)
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
        
        // Triangle clocks every CPU cycle
        triangle.clockTimer();

        // Pulse and Noise clock every 2 CPU cycles
        static bool apuClock = false;
        apuClock = !apuClock;
        if (apuClock) {
            square1.clockTimer();
            square2.clockTimer();
            noise.clockTimer();
        }
        
        // Sample generation
        accumulatedCycles += 1.0;
        if (accumulatedCycles >= CYCLES_PER_SAMPLE) {
            float pulse1 = square1.getOutput();
            float pulse2 = square2.getOutput();
            float tri = triangle.getOutput();
            float nse = noise.getOutput();
            float _dmc = dmc.getOutput();
            
            // NES Mixer (Non-linear)
            float pulse_out = 0;
            if (pulse1 + pulse2 > 0) {
                pulse_out = 95.88f / ((8128.0f / (pulse1 + pulse2)) + 100.0f);
            }
            
            float tnd_out = 0;
            float tnd_divisor = (tri / 8227.0f) + (nse / 12241.0f) + (_dmc / 22638.0f);
            if (tnd_divisor > 0) {
                tnd_out = 159.79f / ((1.0f / tnd_divisor) + 100.0f);
            }
            
            float output = pulse_out + tnd_out;
            
            // Convert to 0-255 range (output is roughly 0.0 to 1.0, but scaling for volume)
            float targetSample = 128.0f + (output * 600.0f); 
            
            // Hardening: Clamp and sanitize
            if (targetSample > 255.0f) targetSample = 255.0f;
            else if (targetSample < 0.0f) targetSample = 0.0f;
            else if (!(targetSample >= 0.0f && targetSample <= 255.0f)) targetSample = 128.0f; // NaN protection
            
            // Low-Pass Filter
            filterAccumulator = filterAccumulator + 0.25f * (targetSample - filterAccumulator);
            ringBuffer.write((uint8_t)filterAccumulator);
            
            accumulatedCycles -= CYCLES_PER_SAMPLE;
            totalSamplesGenerated++;
        }
    }
}

void APU::clockQuarterFrame() {
    square1.clockEnvelope();
    square2.clockEnvelope();
    triangle.clockLinear();
    noise.clockEnvelope();
}

void APU::clockHalfFrame() {
    square1.clockLength();
    square1.clockSweep(true);
    square2.clockLength();
    square2.clockSweep(false);
    triangle.clockLength();
    noise.clockLength();
}
