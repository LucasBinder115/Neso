/*
 * Neso Emulator JNI Bridge
 * Responsibility: Interface between Android Activity and C++ Core.
 * Manages the main execution loop and audio/video data transfer.
 */

#include <jni.h>
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "rom.h"
#include "mapper.h"
#include "renderer.h"
#include <cstring>
#include <android/log.h>
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "NesoJNI", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "NesoJNI", __VA_ARGS__)

struct NesoSystem {
    uint32_t screenBuffer[SCREEN_WIDTH * SCREEN_HEIGHT] = {0};
    CPU* cpu = nullptr;
    PPU ppu;
    APU apu;
    Rom* rom = nullptr;
    Mapper* mapper = nullptr;

    // Telemetry
    uint16_t lastPC = 0;
    int stagnantFrames = 0;
    int frameCounter = 0;

    ~NesoSystem() {
        if (cpu) delete cpu;
        if (rom) delete rom;
        if (mapper) delete mapper;
    }
};

static NesoSystem* systemGlobal = nullptr;

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_neso_core_MainActivity_createCpu(JNIEnv* env, jobject thiz) {
    if (systemGlobal) delete systemGlobal;
    systemGlobal = new NesoSystem();
    
    systemGlobal->cpu = new CPU();
    systemGlobal->cpu->ppu = &systemGlobal->ppu;
    systemGlobal->cpu->apu = &systemGlobal->apu;
    systemGlobal->cpu->reset();
    
    systemGlobal->ppu.reset();
    systemGlobal->ppu.pixelBuffer = systemGlobal->screenBuffer;
    
    systemGlobal->apu.cpu = systemGlobal->cpu;
    systemGlobal->apu.reset();
    
    return (jlong)systemGlobal->cpu;
}

JNIEXPORT void JNICALL
Java_com_neso_core_MainActivity_loadRom(JNIEnv* env, jobject thiz, jbyteArray data) {
    if (!systemGlobal) return;
    jsize len = env->GetArrayLength(data);
    jbyte* buf = env->GetByteArrayElements(data, 0);

    if (systemGlobal->rom) delete systemGlobal->rom;
    if (systemGlobal->mapper) delete systemGlobal->mapper;

    systemGlobal->rom = new Rom((uint8_t*)buf, (size_t)len);
    if (systemGlobal->rom->isValid()) {
        int mapperId = systemGlobal->rom->getMapperId();
        if (mapperId == 0) systemGlobal->mapper = new Mapper0(systemGlobal->rom);
        else if (mapperId == 1) systemGlobal->mapper = new Mapper1(systemGlobal->rom);
        else if (mapperId == 2) systemGlobal->mapper = new Mapper2(systemGlobal->rom);
        else if (mapperId == 3) systemGlobal->mapper = new Mapper3(systemGlobal->rom);
        else if (mapperId == 7) systemGlobal->mapper = new Mapper7(systemGlobal->rom);
        else {
            LOGD("Unsupported Mapper: %d - Defaulting to Mapper 0", mapperId);
            systemGlobal->mapper = new Mapper0(systemGlobal->rom);
        }
        systemGlobal->cpu->mapper = systemGlobal->mapper;
        systemGlobal->ppu.mapper = systemGlobal->mapper;
        LOGD("Mapper %d initialized, resetting CPU...", mapperId);
        systemGlobal->cpu->reset();
        
        // --- Vector Verification ---
        uint8_t lo = systemGlobal->cpu->read(0xFFFC);
        uint8_t hi = systemGlobal->cpu->read(0xFFFD);
        uint16_t resetVec = lo | (hi << 8);
        
        lo = systemGlobal->cpu->read(0xFFFA);
        hi = systemGlobal->cpu->read(0xFFFB);
        uint16_t nmiVec = lo | (hi << 8);
        
        LOGD("PRG-ROM Loaded. Reset Vector: 0x%04X, NMI Vector: 0x%04X", resetVec, nmiVec);
    } else {
        LOGD("ROM Validation FAILED!");
    }

    env->ReleaseByteArrayElements(data, buf, 0);
}

    JNIEXPORT void JNICALL
    Java_com_neso_core_MainActivity_stepCpu(JNIEnv* env, jobject thiz, jlong ptr) {
        if (!systemGlobal || !systemGlobal->cpu || !systemGlobal->mapper) return;
        
        // --- Core Execution Loop ---
        int cyclesThisFrame = 0;
        while (cyclesThisFrame < 29780) { // Authentic NTSC cycles per frame
            int cycles = systemGlobal->cpu->step();
            systemGlobal->ppu.step(cycles, systemGlobal->cpu);
            systemGlobal->apu.step(cycles);
            
            if (systemGlobal->ppu.nmiOccurred) {
                systemGlobal->cpu->triggerNMI();
                systemGlobal->ppu.nmiOccurred = false;
            } else if (systemGlobal->cpu->irqPending) {
                systemGlobal->cpu->triggerIRQ();
            }
            cyclesThisFrame += cycles;
        }

        // --- Production Telemetry (Phase 20) ---
        systemGlobal->frameCounter++;

        if (systemGlobal->cpu->pc == systemGlobal->lastPC) {
            systemGlobal->stagnantFrames++;
        } else {
            systemGlobal->stagnantFrames = 0;
            systemGlobal->lastPC = systemGlobal->cpu->pc;
        }

        if (systemGlobal->frameCounter % 300 == 0) {
            LOGD("💓 Heartbeat: PC=%04X Sl=%d Cyc=%d Stagnant=%d Audit=%08X", 
                 systemGlobal->cpu->pc, systemGlobal->ppu.scanline, systemGlobal->ppu.cycle, 
                 systemGlobal->stagnantFrames, systemGlobal->cpu->getChecksum());
            
            if (systemGlobal->stagnantFrames > 300) { 
                LOGW("⚠️ WARNING: CPU might be stuck! PC=0x%04X", systemGlobal->cpu->pc);
            }
        }
    }
JNIEXPORT void JNICALL
Java_com_neso_core_MainActivity_renderFrame(JNIEnv* env, jobject thiz, jintArray output) {
    if (!output || !systemGlobal) return;
    env->SetIntArrayRegion(output, 0, SCREEN_WIDTH * SCREEN_HEIGHT, (const jint*)systemGlobal->screenBuffer);
}

JNIEXPORT jint JNICALL
Java_com_neso_core_MainActivity_getAudioSamples(JNIEnv* env, jobject thiz, jbyteArray out) {
    if (!systemGlobal) return 0;
    jsize len = env->GetArrayLength(out);
    static uint8_t temp[AudioRingBuffer::SIZE];
    if (len > AudioRingBuffer::SIZE) len = AudioRingBuffer::SIZE;
    
    int read = systemGlobal->apu.ringBuffer.read(temp, (int)len);
    if (read > 0) {
        env->SetByteArrayRegion(out, 0, read, (const jbyte*)temp);
    }
    
    // Instrumentation
    static int counter = 0;
    static int totalRead = 0;
    static uint32_t lastGenCount = 0;
    totalRead += read;
    if (++counter % 300 == 0) {
        uint32_t genDelta = systemGlobal->apu.totalSamplesGenerated - lastGenCount;
        LOGD("Audio Buffer: %d%% | Gen: %u | Cons: %d (per 300 calls)", 
             systemGlobal->apu.ringBuffer.getLevelPct(), genDelta, totalRead);
        lastGenCount = systemGlobal->apu.totalSamplesGenerated;
        totalRead = 0;
    }
    
    return (jint)read;
}

JNIEXPORT jint JNICALL
Java_com_neso_core_MainActivity_getAudioBufferLevel(JNIEnv* env, jobject thiz) {
    if (!systemGlobal) return 0;
    return systemGlobal->apu.ringBuffer.getLevelPct();
}

JNIEXPORT void JNICALL
Java_com_neso_core_MainActivity_setButtonState(JNIEnv* env, jobject thiz, jint button, jboolean pressed) {
    if (systemGlobal && systemGlobal->cpu) {
        if (pressed) {
            systemGlobal->cpu->controller.buttons |= (1 << button);
        } else {
            systemGlobal->cpu->controller.buttons &= ~(1 << button);
        }
    }
}

}