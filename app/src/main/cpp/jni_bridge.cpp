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
        
        // --- Heartbeat Telemetry (Once per frame) ---
        // Log CPU state at the start of the frame execution block
        // This gives us a 60Hz sample of where the CPU is "hanging out".
        // Using static counter to log every 60 frames (1 sec) to be even lighter? 
        // User asked for "Heartbeat", 60Hz is fine for debugging "stuck" state.
        
        static int frameCounter = 0;
        frameCounter++;
        
        // Log every frame for now to catch the crash/hang immediately, 
        // or every 60 frames if we want to monitor long term.
        // Let's do every 60 frames (1 second) to be super safe against lag, 
        // BUT also if PC is suspicious (e.g. 0x0000 or same as last time).
        
        if (frameCounter % 60 == 0) {
            LOGD("💓 Heartbeat #%d: PC=%04X SP=%02X Scan=%d Cyc=%d VRAM=%04X Pal[0]=%02X Spr0Hit=%d Spr0Y=%d", 
                 frameCounter, cpuGlobal->pc, cpuGlobal->sp, ppuGlobal.scanline, ppuGlobal.cycle,
                 ppuGlobal.vramAddr, ppuGlobal.paletteTable[0], (ppuGlobal.ppustatus & 0x40) != 0, ppuGlobal.sprites[0].y);
        }

        static uint16_t lastPC = 0;
        static int stagnantFrames = 0;
    
        int cyclesThisFrame = 0;
        while (cyclesThisFrame < 29780) { // Authentic NTSC (approx)
            int cycles = cpuGlobal->step();
            ppuGlobal.step(cycles, cpuGlobal);
            if (ppuGlobal.nmiOccurred) {
                // LOGD("NMI Triggered at Frame %d", frameCounter); // Optional: Uncomment if NMI is suspect
                cpuGlobal->triggerNMI();
                ppuGlobal.nmiOccurred = false;
            }
            cyclesThisFrame += cycles;
        }
    
        /* 
        // Stuck detection removed for performance, relying on Heartbeat now.
        if (cpuGlobal->pc == lastPC) {
             // ...
        }
        */
        lastPC = cpuGlobal->pc;
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