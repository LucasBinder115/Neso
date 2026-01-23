/* 
 * APU (Audio Processing Unit) Module
 * Responsibility: Sound synthesis, Frame Counter timing, and Mixer.
 * Supported: 2 Pulse channels, 1 Triangle, 1 Noise. (DMC is placeholder).
 */

#ifndef APU_H
#define APU_H

#include <cstdint>
#include <cstring>

class AudioRingBuffer {
public:
    static const int SIZE = 2048; // Smaller buffer for lower latency
    uint8_t buffer[SIZE];
    int head = 0; 
    int tail = 0; 

    AudioRingBuffer() {
        memset(buffer, 128, SIZE); // Initialize with silent DC (128)
    }

    void write(uint8_t sample) {
        buffer[tail] = sample;
        tail = (tail + 1) % SIZE;
        if (tail == head) {
             // Buffer overflow: push head forward to keep most recent audio (drop samples)
             head = (head + 1) % SIZE;
        }
    }

    int read(uint8_t* out, int maxCount) {
        int count = 0;
        int level = getLevel();
        
        // If buffer is too deep (> 70%), skip some samples to catch up (Sync/Lag fix)
        if (level > (SIZE * 0.7)) {
            head = (tail - (int)(SIZE * 0.3) + SIZE) % SIZE;
        }

        while (count < maxCount && head != tail) {
            out[count++] = buffer[head];
            head = (head + 1) % SIZE;
        }
        return count;
    }

    int getLevel() {
        return (tail >= head) ? (tail - head) : (SIZE - (head - tail));
    }

    int getLevelPct() {
        return (getLevel() * 100) / SIZE;
    }
};

struct SquareChannel {
    // 11-bit timer
    uint16_t timerPeriod = 0;
    uint16_t timerValue = 0;
    
    // Envelope
    uint8_t envelopeDivider = 0;
    uint8_t envelopeCounter = 0;
    uint8_t envelopeVolume = 0;
    bool envelopeStart = false;
    bool envelopeLoop = false;  // Also halt length counter
    bool constantVolume = false;
    uint8_t constantVolumeValue = 0;
    
    // Length Counter
    uint8_t lengthCounter = 0;
    bool lengthEnabled = true;
    
    // Sweep
    bool sweepEnabled = false;
    bool sweepNegate = false;
    uint8_t sweepShift = 0;
    uint8_t sweepPeriod = 0;
    uint8_t sweepDivider = 0;
    bool sweepReload = false;
    
    // Output
    bool enabled = false;
    uint8_t dutyPos = 0;
    uint8_t dutyCycle = 2;

    static const uint8_t DUTIES[4][8];

    void clockTimer() {
        if (timerValue == 0) {
            timerValue = timerPeriod;
            dutyPos = (dutyPos + 1) & 7;
        } else {
            timerValue--;
        }
    }
    
    void clockEnvelope() {
        if (envelopeStart) {
            envelopeStart = false;
            envelopeVolume = 15;
            envelopeDivider = constantVolumeValue;
        } else if (envelopeDivider > 0) {
            envelopeDivider--;
        } else {
            envelopeDivider = constantVolumeValue;
            if (envelopeVolume > 0) {
                envelopeVolume--;
            } else if (envelopeLoop) {
                envelopeVolume = 15;
            }
        }
    }
    
    void clockLength() {
        if (lengthCounter > 0 && !envelopeLoop) {
            lengthCounter--;
        }
    }
    
    void clockSweep(bool isChannel1) {
        if (sweepDivider == 0 && sweepEnabled && sweepShift > 0 && timerPeriod >= 8) {
            uint16_t delta = timerPeriod >> sweepShift;
            if (sweepNegate) {
                timerPeriod -= delta;
                if (isChannel1) timerPeriod--; // Channel 1 uses one's complement
            } else {
                timerPeriod += delta;
            }
        }
        
        if (sweepDivider == 0 || sweepReload) {
            sweepDivider = sweepPeriod;
            sweepReload = false;
        } else {
            sweepDivider--;
        }
    }
    
    bool isMuted() {
        return timerPeriod < 8 || timerPeriod > 0x7FF;
    }

    uint8_t getOutput() {
        if (!enabled || lengthCounter == 0 || isMuted()) return 0;
        if (!DUTIES[dutyCycle][dutyPos]) return 0;
        return constantVolume ? constantVolumeValue : envelopeVolume;
    }
};

struct TriangleChannel {
    uint16_t timerPeriod = 0;
    uint16_t timerValue = 0;
    uint8_t lengthCounter = 0;
    uint8_t linearCounter = 0;
    uint8_t linearCounterReload = 0;
    bool linearCounterReloadFlag = false;
    bool lengthEnabled = true;
    bool controlFlag = false; // Also used for length counter halt
    uint8_t step = 0;
    bool enabled = false;

    static const uint8_t TRIANGLE_STEPS[32];

    void clockTimer() {
        if (enabled && timerPeriod >= 2) {
            if (timerValue == 0) {
                timerValue = timerPeriod;
                if (lengthCounter > 0 && linearCounter > 0) {
                    step = (step + 1) & 0x1F;
                }
            } else {
                timerValue--;
            }
        }
    }

    void clockLinear() {
        if (linearCounterReloadFlag) {
            linearCounter = linearCounterReload;
        } else if (linearCounter > 0) {
            linearCounter--;
        }
        if (!controlFlag) {
            linearCounterReloadFlag = false;
        }
    }

    void clockLength() {
        if (lengthCounter > 0 && !controlFlag) {
            lengthCounter--;
        }
    }

    uint8_t getOutput() {
        if (!enabled || lengthCounter == 0 || linearCounter == 0) return 0;
        return TRIANGLE_STEPS[step];
    }
};

struct NoiseChannel {
    uint16_t timerPeriod = 0;
    uint16_t timerValue = 0;
    uint16_t shiftRegister = 1;
    bool mode = false;
    
    // Envelope
    uint8_t envelopeDivider = 0;
    uint8_t envelopeCounter = 0;
    uint8_t envelopeVolume = 0;
    bool envelopeStart = false;
    bool envelopeLoop = false;
    bool constantVolume = false;
    uint8_t constantVolumeValue = 0;
    
    uint8_t lengthCounter = 0;
    bool enabled = false;

    static const uint16_t PERIOD_TABLE[16];

    void clockTimer() {
        if (timerValue == 0) {
            timerValue = timerPeriod;
            uint16_t feedback = (shiftRegister & 1) ^ ((mode ? (shiftRegister >> 6) : (shiftRegister >> 1)) & 1);
            shiftRegister = (shiftRegister >> 1) | (feedback << 14);
        } else {
            timerValue--;
        }
    }

    void clockEnvelope() {
        if (envelopeStart) {
            envelopeStart = false;
            envelopeVolume = 15;
            envelopeDivider = constantVolumeValue;
        } else if (envelopeDivider > 0) {
            envelopeDivider--;
        } else {
            envelopeDivider = constantVolumeValue;
            if (envelopeVolume > 0) {
                envelopeVolume--;
            } else if (envelopeLoop) {
                envelopeVolume = 15;
            }
        }
    }

    void clockLength() {
        if (lengthCounter > 0 && !envelopeLoop) {
            lengthCounter--;
        }
    }

    uint8_t getOutput() {
        if (!enabled || lengthCounter == 0 || (shiftRegister & 1)) return 0;
        return constantVolume ? constantVolumeValue : envelopeVolume;
    }
};

struct DMCChannel {
    bool enabled = false;
    // Basic structure, to be expanded if needed
    uint8_t outputLevel = 0;

    void clock() {
        // Placeholder for samples
    }

    uint8_t getOutput() {
        return outputLevel;
    }
};

struct APU {
    SquareChannel square1;
    SquareChannel square2;
    TriangleChannel triangle;
    NoiseChannel noise;
    DMCChannel dmc;
    AudioRingBuffer ringBuffer;
    
    // Timing
    uint32_t totalSamplesGenerated = 0;
    double accumulatedCycles = 0;
    float filterAccumulator = 128.0f;
    
    // Frame Counter ($4017)
    bool frameCounterMode = false; // false=4-step, true=5-step
    bool frameIRQDisable = false;
    uint32_t frameCounterCycles = 0;
    uint8_t frameStep = 0;
    
    // NTSC Constants
    static constexpr double CPU_FREQ = 1789773.0;
    static constexpr double SAMPLE_RATE = 44100.0;
    static constexpr double CYCLES_PER_SAMPLE = CPU_FREQ / SAMPLE_RATE;
    
    // Frame Counter timing (CPU cycles)
    // 4-step mode: Quarter frames at 3729, 7457, 11186, 14915
    // Half frames at 7457, 14915
    static constexpr uint32_t FRAME_COUNTER_RATE = 14915;
    static const uint8_t LENGTH_TABLE[32];

    void reset();
    void write(uint16_t addr, uint8_t val);
    void step(int cycles);
    uint8_t readStatus();
    
    struct CPU* cpu = nullptr;
    
    void clockQuarterFrame();
    void clockHalfFrame();
};

#endif
