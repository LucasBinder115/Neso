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
        else if (mapperId == 2) currentMapper = new Mapper2(currentRom);
        else if (mapperId == 3) currentMapper = new Mapper3(currentRom);
        else {
            LOGD("Unsupported Mapper: %d - Defaulting to Mapper 0", mapperId);
            currentMapper = new Mapper0(currentRom);
        }
        cpuGlobal->mapper = currentMapper;
        ppuGlobal.mapper = currentMapper;
        LOGD("Mapper %d initialized, resetting CPU...", mapperId);
        cpuGlobal->reset();
    } else {
        LOGD("ROM Validation FAILED!");
    }

    env->ReleaseByteArrayElements(data, buf, 0);
}

JNIEXPORT void JNICALL
Java_com_neso_core_MainActivity_stepCpu(JNIEnv* env, jobject thiz, jlong ptr) {
    if (!cpuGlobal || !cpuGlobal->mapper) return;
    
    static uint16_t lastPC = 0;
    static int stagnantFrames = 0;

    int cyclesThisFrame = 0;
    while (cyclesThisFrame < 30000) { // Slightly more to ensure we cross frame boundaries
        int cycles = cpuGlobal->step();
        ppuGlobal.step(cycles, cpuGlobal);
        if (ppuGlobal.nmiOccurred) {
            cpuGlobal->triggerNMI();
            ppuGlobal.nmiOccurred = false;
        }
        cyclesThisFrame += cycles;
    }

    if (cpuGlobal->pc == lastPC) {
        stagnantFrames++;
        if (stagnantFrames % 60 == 0) {
            __android_log_print(ANDROID_LOG_DEBUG, "NesoCPU", "PC STUCK at 0x%04X for %d frames", cpuGlobal->pc, stagnantFrames);
        }
    } else {
        if (stagnantFrames > 0) {
            __android_log_print(ANDROID_LOG_DEBUG, "NesoCPU", "PC MOVED! 0x%04X -> 0x%04X (after %d stagnant frames)", lastPC, cpuGlobal->pc, stagnantFrames);
        }
        stagnantFrames = 0;
        lastPC = cpuGlobal->pc;
    }
}

JNIEXPORT void JNICALL
Java_com_neso_core_MainActivity_renderFrame(JNIEnv* env, jobject thiz, jintArray output) {
    // OLD: renderScreen(screenBuffer, ppuGlobal);
    // NEW: Cycle-accurate pixels are generated in PPU::step
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
    if (++counter % 120 == 0) {
        uint32_t genDelta = apuGlobal.totalSamplesGenerated - lastGenCount;
        LOGD("Audio Buffer: %d%% | Gen: %u | Cons: %d (per 120 calls)", 
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