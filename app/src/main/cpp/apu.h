#ifndef APU_H
#define APU_H

#include <cstdint>
#include <cstring>

class AudioRingBuffer {
public:
    static const int SIZE = 8192; 
    uint8_t buffer[SIZE];
    int head = 0; 
    int tail = 0; 

    AudioRingBuffer() {
        memset(buffer, 128, SIZE);
    }

    void write(uint8_t sample) {
        buffer[tail] = sample;
        tail = (tail + 1) % SIZE;
        if (tail == head) head = (head + 1) % SIZE;
    }

    int read(uint8_t* out, int maxCount) {
        int count = 0;
        while (count < maxCount && head != tail) {
            out[count++] = buffer[head];
            head = (head + 1) % SIZE;
        }
        return count;
    }

    int getLevelPct() {
        int count = (tail >= head) ? (tail - head) : (SIZE - (head - tail));
        return (count * 100) / SIZE;
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
    static const uint8_t LENGTH_TABLE[32];

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

struct APU {
    SquareChannel square1;
    SquareChannel square2;
    AudioRingBuffer ringBuffer;
    
    // Timing
    uint32_t totalSamplesGenerated = 0;
    double accumulatedCycles = 0;
    
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

    void reset();
    void write(uint16_t addr, uint8_t val);
    void step(int cycles);
    
    void clockQuarterFrame();
    void clockHalfFrame();
};

#endif
