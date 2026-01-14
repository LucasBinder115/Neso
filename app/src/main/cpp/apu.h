#ifndef APU_H
#define APU_H

#include <cstdint>
#include <cstring>
#include <android/log.h>

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
        if (tail == head) head = (head + 1) % SIZE; // Overflow
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
    uint16_t timer = 0;
    uint16_t timerCounter = 0;
    uint8_t volume = 0;
    bool enabled = false;
    uint8_t dutyPos = 0;
    uint8_t dutyCycle = 2; // Default 50%

    // NES Duty Cycles (Approximate)
    static const uint8_t DUTIES[4][8];

    void step() {
        if (timerCounter > 0) {
            timerCounter--;
        } else {
            timerCounter = timer;
            dutyPos = (dutyPos + 1) % 8;
        }
    }

    uint8_t getOutput() {
        if (!enabled || volume == 0) return 0;
        return DUTIES[dutyCycle][dutyPos] ? volume : 0;
    }
};

struct APU {
    SquareChannel square1;
    AudioRingBuffer ringBuffer;
    
    uint32_t totalSamplesGenerated = 0;
    double accumulatedCycles = 0;
    const double CPU_FREQ = 1789773.0;
    const double SAMPLE_RATE = 44100.0;
    const double CYCLES_PER_SAMPLE = CPU_FREQ / SAMPLE_RATE;

    void reset();
    void write(uint16_t addr, uint8_t val);
    void step(int cycles);
};

#endif
