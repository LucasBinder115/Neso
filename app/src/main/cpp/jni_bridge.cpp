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

static uint32_t screenBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static CPU* cpuGlobal = nullptr;
static PPU ppuGlobal;
static APU apuGlobal;
static Rom* currentRom = nullptr;
static Mapper* currentMapper = nullptr;

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_neso_core_MainActivity_createCpu(JNIEnv* env, jobject thiz) {
    if (cpuGlobal) delete cpuGlobal;
    cpuGlobal = new CPU();
    cpuGlobal->ppu = &ppuGlobal;
    cpuGlobal->apu = &apuGlobal;
    cpuGlobal->reset();
    ppuGlobal.reset();
    ppuGlobal.pixelBuffer = screenBuffer;
    apuGlobal.cpu = cpuGlobal;
    apuGlobal.reset();
    return (jlong)cpuGlobal;
}

JNIEXPORT void JNICALL
Java_com_neso_core_MainActivity_loadRom(JNIEnv* env, jobject thiz, jbyteArray data) {
    jsize len = env->GetArrayLength(data);
    jbyte* buf = env->GetByteArrayElements(data, 0);

    if (currentRom) delete currentRom;
    if (currentMapper) delete currentMapper;

    currentRom = new Rom((uint8_t*)buf, (size_t)len);
    if (currentRom->isValid()) {
        int mapperId = currentRom->getMapperId();
        if (mapperId == 0) currentMapper = new Mapper0(currentRom);
        else if (mapperId == 1) currentMapper = new Mapper1(currentRom);
        else if (mapperId == 2) currentMapper = new Mapper2(currentRom);
        else if (mapperId == 3) currentMapper = new Mapper3(currentRom);
        else if (mapperId == 7) currentMapper = new Mapper7(currentRom);
        else {
            LOGD("Unsupported Mapper: %d - Defaulting to Mapper 0", mapperId);
            currentMapper = new Mapper0(currentRom);
        }
        cpuGlobal->mapper = currentMapper;
        ppuGlobal.mapper = currentMapper;
        LOGD("Mapper %d initialized, resetting CPU...", mapperId);
        cpuGlobal->reset();
        
        // --- Vector Verification ---
        uint8_t lo = cpuGlobal->read(0xFFFC);
        uint8_t hi = cpuGlobal->read(0xFFFD);
        uint16_t resetVec = lo | (hi << 8);
        
        lo = cpuGlobal->read(0xFFFA);
        hi = cpuGlobal->read(0xFFFB);
        uint16_t nmiVec = lo | (hi << 8);
        
        LOGD("PRG-ROM Loaded. Reset Vector: 0x%04X, NMI Vector: 0x%04X", resetVec, nmiVec);
    } else {
        LOGD("ROM Validation FAILED!");
    }

    env->ReleaseByteArrayElements(data, buf, 0);
}

    JNIEXPORT void JNICALL
    Java_com_neso_core_MainActivity_stepCpu(JNIEnv* env, jobject thiz, jlong ptr) {
        if (!cpuGlobal || !cpuGlobal->mapper) return;
        
        // --- Core Execution Loop ---
        int cyclesThisFrame = 0;
        while (cyclesThisFrame < 29780) { // Authentic NTSC cycles per frame
            int cycles = cpuGlobal->step();
            ppuGlobal.step(cycles, cpuGlobal);
            apuGlobal.step(cycles);
            
            if (ppuGlobal.nmiOccurred) {
                cpuGlobal->triggerNMI();
                ppuGlobal.nmiOccurred = false;
            } else if (cpuGlobal->irqPending) {
                cpuGlobal->triggerIRQ();
            }
            cyclesThisFrame += cycles;
        }

        // --- Production Telemetry (Phase 20) ---
        static uint16_t lastPC = 0;
        static int stagnantFrames = 0;
        static int frameCounter = 0;
        frameCounter++;

        if (cpuGlobal->pc == lastPC) {
            stagnantFrames++;
        } else {
            stagnantFrames = 0;
            lastPC = cpuGlobal->pc;
        }

        // Log Heartbeat every 300 frames (~5 seconds)
        // Helps identify "Hangs" during wide compatibility testing
        if (frameCounter % 300 == 0) {
            LOGD("💓 Heartbeat: PC=%04X Sl=%d Cyc=%d Stagnant=%d", 
                 cpuGlobal->pc, ppuGlobal.scanline, ppuGlobal.cycle, stagnantFrames);
            
            if (stagnantFrames > 300) { 
                LOGW("⚠️ WARNING: CPU might be stuck! PC=0x%04X", cpuGlobal->pc);
            }
        }
    }
JNIEXPORT void JNICALL
Java_com_neso_core_MainActivity_renderFrame(JNIEnv* env, jobject thiz, jintArray output) {
    if (!output) return;
    env->SetIntArrayRegion(output, 0, SCREEN_WIDTH * SCREEN_HEIGHT, (const jint*)screenBuffer);
}

JNIEXPORT jint JNICALL
Java_com_neso_core_MainActivity_getAudioSamples(JNIEnv* env, jobject thiz, jbyteArray out) {
    jsize len = env->GetArrayLength(out);
    static uint8_t temp[AudioRingBuffer::SIZE];
    if (len > AudioRingBuffer::SIZE) len = AudioRingBuffer::SIZE;
    
    int read = apuGlobal.ringBuffer.read(temp, (int)len);
    if (read > 0) {
        env->SetByteArrayRegion(out, 0, read, (const jbyte*)temp);
    }
    
    // Instrumentation
    static int counter = 0;
    static int totalRead = 0;
    static uint32_t lastGenCount = 0;
    totalRead += read;
    if (++counter % 300 == 0) {
        uint32_t genDelta = apuGlobal.totalSamplesGenerated - lastGenCount;
        LOGD("Audio Buffer: %d%% | Gen: %u | Cons: %d (per 300 calls)", 
             apuGlobal.ringBuffer.getLevelPct(), genDelta, totalRead);
        lastGenCount = apuGlobal.totalSamplesGenerated;
        totalRead = 0;
    }
    
    return (jint)read;
}

JNIEXPORT jint JNICALL
Java_com_neso_core_MainActivity_getAudioBufferLevel(JNIEnv* env, jobject thiz) {
    return apuGlobal.ringBuffer.getLevelPct();
}

JNIEXPORT void JNICALL
Java_com_neso_core_MainActivity_setButtonState(JNIEnv* env, jobject thiz, jint button, jboolean pressed) {
    if (cpuGlobal) {
        if (pressed) {
            cpuGlobal->controller.buttons |= (1 << button);
        } else {
            cpuGlobal->controller.buttons &= ~(1 << button);
        }
    }
}

}