#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "mapper.h"
#include <cstring>
#include <android/log.h>

#define LOG_TAG "NesoCore"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

uint8_t CPU::read(uint16_t addr) {
    if (addr <= 0x1FFF) return ram[addr & 0x7FF];
    if (addr >= 0x2000 && addr <= 0x3FFF) {
        uint16_t reg = 0x2000 + (addr % 8);
        if (reg == 0x2002) return ppu->readStatus();
        if (reg == 0x2004) return 0; 
        if (reg == 0x2007) {
            uint8_t data = ppu->vramRead(ppu->vramAddr);
            if (ppu->vramAddr < 0x3F00) {
                uint8_t buffered = ppu->readBuffer;
                ppu->readBuffer = data;
                data = buffered;
            } else {
                ppu->readBuffer = ppu->mapper ? ppu->mapper->ppuRead(ppu->vramAddr - 0x1000) : 0;
            }
            ppu->vramAddr += (ppu->ppuctrl & 0x04) ? 32 : 1;
            return data;
        }
    }
    if (addr == 0x4016) return controller.read();
    if (addr >= 0x8000 && mapper) return mapper->cpuRead(addr);
    return 0x00;
}

void CPU::write(uint16_t addr, uint8_t val) {
    if (addr <= 0x1FFF) ram[addr & 0x7FF] = val;
    else if (addr >= 0x2000 && addr <= 0x3FFF) {
        uint16_t reg = 0x2000 + (addr % 8);
        if (reg == 0x2000) {
            uint8_t oldCtrl = ppu->ppuctrl;
            ppu->ppuctrl = val;
            if (!(oldCtrl & 0x80) && (val & 0x80) && (ppu->ppustatus & 0x80)) {
                ppu->nmiOccurred = true;
            }
        }
        if (reg == 0x2001) ppu->ppumask = val;
        if (reg == 0x2003) ppu->oamAddr = val;
        if (reg == 0x2004) {
            ((uint8_t*)ppu->sprites)[ppu->oamAddr++] = val;
        }
        // $2005 - PPUSCROLL (Loopy registers)
        if (reg == 0x2005) {
            if (!ppu->writeToggle) {
                // First write: Coarse X and Fine X
                ppu->tempAddr = (ppu->tempAddr & 0xFFE0) | (val >> 3);
                ppu->fineX = val & 0x07;
            } else {
                // Second write: Coarse Y and Fine Y
                ppu->tempAddr = (ppu->tempAddr & 0x8FFF) | ((val & 0x07) << 12);
                ppu->tempAddr = (ppu->tempAddr & 0xFC1F) | ((val & 0xF8) << 2);
            }
            ppu->writeToggle = !ppu->writeToggle;
        }
        // $2006 - PPUADDR (Loopy registers)
        if (reg == 0x2006) {
            if (!ppu->writeToggle) {
                // First write: High 6 bits of t
                ppu->tempAddr = (ppu->tempAddr & 0x00FF) | ((val & 0x3F) << 8);
            } else {
                // Second write: Low 8 bits of t, then copy t to v
                ppu->tempAddr = (ppu->tempAddr & 0xFF00) | val;
                ppu->vramAddr = ppu->tempAddr;
            }
            ppu->writeToggle = !ppu->writeToggle;
        }
        if (reg == 0x2007) {
            ppu->vramWrite(ppu->vramAddr, val);
            ppu->vramAddr += (ppu->ppuctrl & 0x04) ? 32 : 1;
        }
    } else if (addr == 0x4014) {
        // OAM DMA: Copy 256 bytes to OAM
        uint16_t base = (uint16_t)val << 8;
        for (int i = 0; i < 256; i++) {
            ((uint8_t*)ppu->sprites)[i] = read(base + i);
        }
        // DMA takes 513 cycles (+1 if on odd cycle)
        // For simplicity, always use 513 (close enough)
        cyclesToStall = 513;
    } else if (addr == 0x4016) {
        if (val & 1) controller.latch();
    } else if (addr >= 0x4000 && addr <= 0x4017 && apu) apu->write(addr, val);
    else if (addr >= 0x8000 && mapper) mapper->cpuWrite(addr, val);
}

void CPU::reset() {
    a = x = y = 0;
    sp = 0xFD;
    status = 0x34; 
    cyclesToStall = 0;
    pc = read16(0xFFFC);
    LOGD("CPU RESET! PC: 0x%04X", pc);
}

void CPU::setZN(uint8_t val) {
    status = (status & ~0x82) | (val == 0 ? 0x02 : 0) | (val & 0x80);
}

int CPU::step() {
    if (cyclesToStall > 0) { cyclesToStall--; return 1; }

    uint16_t curPC = pc;
    uint8_t opcode = read(pc++);
    int cycles = 2;

    // Helpers
    auto fetch8 = [&]() { return read(pc++); };
    auto fetch16 = [&]() { uint16_t l = read(pc++); return l | (read(pc++) << 8); };
    
    // Addressing modes
    auto addr_zp = [&]() { return fetch8(); };
    auto addr_zpx = [&]() { return (fetch8() + x) & 0xFF; };
    auto addr_zpy = [&]() { return (fetch8() + y) & 0xFF; };
    auto addr_abs = [&]() { return fetch16(); };
    auto addr_absx = [&]() { return fetch16() + x; };
    auto addr_absy = [&]() { return fetch16() + y; };
    auto addr_indx = [&]() { uint8_t zp = (fetch8() + x) & 0xFF; return read16(zp); };
    auto addr_indy = [&]() { uint8_t zp = fetch8(); return read16(zp) + y; };


    switch (opcode) {
        // --- LDA ---
        case 0xA9: a = fetch8(); setZN(a); break;
        case 0xA5: a = read(addr_zp()); setZN(a); cycles=3; break;
        case 0xB5: a = read(addr_zpx()); setZN(a); cycles=4; break;
        case 0xAD: a = read(addr_abs()); setZN(a); cycles=4; break;
        case 0xBD: a = read(addr_absx()); setZN(a); cycles=4; break;
        case 0xB9: a = read(addr_absy()); setZN(a); cycles=4; break;
        case 0xA1: a = read(addr_indx()); setZN(a); cycles=6; break;
        case 0xB1: a = read(addr_indy()); setZN(a); cycles=5; break;

        // --- LDX ---
        case 0xA2: x = fetch8(); setZN(x); break;
        case 0xA6: x = read(addr_zp()); setZN(x); cycles=3; break;
        case 0xB6: x = read(addr_zpy()); setZN(x); cycles=4; break;
        case 0xAE: x = read(addr_abs()); setZN(x); cycles=4; break;
        case 0xBE: x = read(addr_absy()); setZN(x); cycles=4; break;

        // --- LDY ---
        case 0xA0: y = fetch8(); setZN(y); break;
        case 0xA4: y = read(addr_zp()); setZN(y); cycles=3; break;
        case 0xB4: y = read(addr_zpx()); setZN(y); cycles=4; break;
        case 0xAC: y = read(addr_abs()); setZN(y); cycles=4; break;
        case 0xBC: y = read(addr_absx()); setZN(y); cycles=4; break;

        // --- STA ---
        case 0x85: write(addr_zp(), a); cycles=3; break;
        case 0x95: write(addr_zpx(), a); cycles=4; break;
        case 0x8D: write(addr_abs(), a); cycles=4; break;
        case 0x9D: write(addr_absx(), a); cycles=5; break;
        case 0x99: write(addr_absy(), a); cycles=5; break;
        case 0x81: write(addr_indx(), a); cycles=6; break;
        case 0x91: write(addr_indy(), a); cycles=6; break;

        // --- STX/STY ---
        case 0x86: write(addr_zp(), x); cycles=3; break;
        case 0x96: write(addr_zpy(), x); cycles=4; break;
        case 0x8E: write(addr_abs(), x); cycles=4; break;
        case 0x84: write(addr_zp(), y); cycles=3; break;
        case 0x94: write(addr_zpx(), y); cycles=4; break;
        case 0x8C: write(addr_abs(), y); cycles=4; break;

        // --- ORA ---
        case 0x09: a |= fetch8(); setZN(a); break;
        case 0x05: a |= read(addr_zp()); setZN(a); cycles=3; break;
        case 0x15: a |= read(addr_zpx()); setZN(a); cycles=4; break;
        case 0x0D: a |= read(addr_abs()); setZN(a); cycles=4; break;
        case 0x1D: a |= read(addr_absx()); setZN(a); cycles=4; break;
        case 0x19: a |= read(addr_absy()); setZN(a); cycles=4; break;
        case 0x01: a |= read(addr_indx()); setZN(a); cycles=6; break;
        case 0x11: a |= read(addr_indy()); setZN(a); cycles=5; break;

        // --- AND ---
        case 0x29: a &= fetch8(); setZN(a); break;
        case 0x25: a &= read(addr_zp()); setZN(a); cycles=3; break;
        case 0x35: a &= read(addr_zpx()); setZN(a); cycles=4; break;
        case 0x2D: a &= read(addr_abs()); setZN(a); cycles=4; break;
        case 0x3D: a &= read(addr_absx()); setZN(a); cycles=4; break;
        case 0x39: a &= read(addr_absy()); setZN(a); cycles=4; break;
        case 0x21: a &= read(addr_indx()); setZN(a); cycles=6; break;
        case 0x31: a &= read(addr_indy()); setZN(a); cycles=5; break;

        // --- EOR ---
        case 0x49: a ^= fetch8(); setZN(a); break;
        case 0x45: a ^= read(addr_zp()); setZN(a); cycles=3; break;
        case 0x55: a ^= read(addr_zpx()); setZN(a); cycles=4; break;
        case 0x4D: a ^= read(addr_abs()); setZN(a); cycles=4; break;
        case 0x5D: a ^= read(addr_absx()); setZN(a); cycles=4; break;
        case 0x59: a ^= read(addr_absy()); setZN(a); cycles=4; break;
        case 0x41: a ^= read(addr_indx()); setZN(a); cycles=6; break;
        case 0x51: a ^= read(addr_indy()); setZN(a); cycles=5; break;

        // --- ADC ---
        case 0x69: { uint8_t v=fetch8(); uint16_t t=a+v+(status&1); 
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^v)&(a^t)&0x80)>>1); a=(uint8_t)t; break; }
        case 0x65: { uint8_t v=read(addr_zp()); uint16_t t=a+v+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^v)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=3; break; }
        case 0x75: { uint8_t v=read(addr_zpx()); uint16_t t=a+v+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^v)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=4; break; }
        case 0x6D: { uint8_t v=read(addr_abs()); uint16_t t=a+v+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^v)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=4; break; }
        case 0x7D: { uint8_t v=read(addr_absx()); uint16_t t=a+v+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^v)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=4; break; }
        case 0x79: { uint8_t v=read(addr_absy()); uint16_t t=a+v+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^v)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=4; break; }

        // --- SBC ---
        case 0xE9: { uint8_t v=fetch8(); uint8_t val=~v; uint16_t t=a+val+(status&1); 
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^val)&(a^t)&0x80)>>1); a=(uint8_t)t; break; }
        case 0xE5: { uint8_t v=read(addr_zp()); uint8_t val=~v; uint16_t t=a+val+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^val)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=3; break; }
        case 0xF5: { uint8_t v=read(addr_zpx()); uint8_t val=~v; uint16_t t=a+val+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^val)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=4; break; }
        case 0xED: { uint8_t v=read(addr_abs()); uint8_t val=~v; uint16_t t=a+val+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^val)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=4; break; }
        case 0xFD: { uint8_t v=read(addr_absx()); uint8_t val=~v; uint16_t t=a+val+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^val)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=4; break; }
        case 0xF9: { uint8_t v=read(addr_absy()); uint8_t val=~v; uint16_t t=a+val+(status&1);
                     status=(status&~0xC3)|(t>0xFF?1:0)|(t&0x80)|((uint8_t)t==0?2:0)|((~(a^val)&(a^t)&0x80)>>1); a=(uint8_t)t; cycles=4; break; }

        // --- CMP ---
        case 0xC9: { uint8_t v=fetch8(); uint8_t r=a-v; status=(status&~0x83)|(r&0x80)|(a>=v?1:0)|(a==v?2:0); break; }
        case 0xC5: { uint8_t v=read(addr_zp()); uint8_t r=a-v; status=(status&~0x83)|(r&0x80)|(a>=v?1:0)|(a==v?2:0); cycles=3; break; }
        case 0xD5: { uint8_t v=read(addr_zpx()); uint8_t r=a-v; status=(status&~0x83)|(r&0x80)|(a>=v?1:0)|(a==v?2:0); cycles=4; break; }
        case 0xCD: { uint8_t v=read(addr_abs()); uint8_t r=a-v; status=(status&~0x83)|(r&0x80)|(a>=v?1:0)|(a==v?2:0); cycles=4; break; }
        case 0xDD: { uint8_t v=read(addr_absx()); uint8_t r=a-v; status=(status&~0x83)|(r&0x80)|(a>=v?1:0)|(a==v?2:0); cycles=4; break; }
        case 0xD9: { uint8_t v=read(addr_absy()); uint8_t r=a-v; status=(status&~0x83)|(r&0x80)|(a>=v?1:0)|(a==v?2:0); cycles=4; break; }
        
        // --- CPX/CPY ---
        case 0xE0: { uint8_t v=fetch8(); uint8_t r=x-v; status=(status&~0x83)|(r&0x80)|(x>=v?1:0)|(x==v?2:0); break; }
        case 0xE4: { uint8_t v=read(addr_zp()); uint8_t r=x-v; status=(status&~0x83)|(r&0x80)|(x>=v?1:0)|(x==v?2:0); cycles=3; break; }
        case 0xEC: { uint8_t v=read(addr_abs()); uint8_t r=x-v; status=(status&~0x83)|(r&0x80)|(x>=v?1:0)|(x==v?2:0); cycles=4; break; }

        case 0xC0: { uint8_t v=fetch8(); uint8_t r=y-v; status=(status&~0x83)|(r&0x80)|(y>=v?1:0)|(y==v?2:0); break; }
        case 0xC4: { uint8_t v=read(addr_zp()); uint8_t r=y-v; status=(status&~0x83)|(r&0x80)|(y>=v?1:0)|(y==v?2:0); cycles=3; break; }
        case 0xCC: { uint8_t v=read(addr_abs()); uint8_t r=y-v; status=(status&~0x83)|(r&0x80)|(y>=v?1:0)|(y==v?2:0); cycles=4; break; }

        // --- BIT ---
        case 0x24: { uint8_t v=read(addr_zp()); status=(status&~0xC2)|(v&0xC0)|((a&v)==0?2:0); cycles=3; break; }
        case 0x2C: { uint8_t v=read(addr_abs()); status=(status&~0xC2)|(v&0xC0)|((a&v)==0?2:0); cycles=4; break; }

        // --- INC/DEC ---
        case 0xE8: x++; setZN(x); break;
        case 0xCA: x--; setZN(x); break;
        case 0xC8: y++; setZN(y); break;
        case 0x88: y--; setZN(y); break;
        case 0xE6: { uint16_t adr=addr_zp(); uint8_t v=read(adr)+1; write(adr,v); setZN(v); cycles=5; break; }
        case 0xF6: { uint16_t adr=addr_zpx(); uint8_t v=read(adr)+1; write(adr,v); setZN(v); cycles=6; break; }
        case 0xEE: { uint16_t adr=addr_abs(); uint8_t v=read(adr)+1; write(adr,v); setZN(v); cycles=6; break; }
        case 0xFE: { uint16_t adr=addr_absx(); uint8_t v=read(adr)+1; write(adr,v); setZN(v); cycles=7; break; }
        case 0xC6: { uint16_t adr=addr_zp(); uint8_t v=read(adr)-1; write(adr,v); setZN(v); cycles=5; break; }
        case 0xD6: { uint16_t adr=addr_zpx(); uint8_t v=read(adr)-1; write(adr,v); setZN(v); cycles=6; break; }
        case 0xCE: { uint16_t adr=addr_abs(); uint8_t v=read(adr)-1; write(adr,v); setZN(v); cycles=6; break; }
        case 0xDE: { uint16_t adr=addr_absx(); uint8_t v=read(adr)-1; write(adr,v); setZN(v); cycles=7; break; }

        // --- SHIFTS/ROTATES ---
        case 0x0A: { uint8_t c=(a&0x80)>>7; a<<=1; setZN(a); status=(status&~0x01)|c; break; }
        case 0x4A: { uint8_t c=a&1; a>>=1; setZN(a); status=(status&~0x01)|c; break; }
        case 0x2A: { uint8_t c=(a&0x80)>>7; uint8_t oc=status&1; a=(a<<1)|oc; setZN(a); status=(status&~0x01)|c; break; }
        case 0x6A: { uint8_t c=a&1; uint8_t oc=(status&1)<<7; a=(a>>1)|oc; setZN(a); status=(status&~0x01)|c; break; }

        case 0x06: { uint16_t adr=addr_zp(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=5; break; }
        case 0x16: { uint16_t adr=addr_zpx(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=6; break; }
        case 0x0E: { uint16_t adr=addr_abs(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=6; break; }
        case 0x1E: { uint16_t adr=addr_absx(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=7; break; }

        case 0x46: { uint16_t adr=addr_zp(); uint8_t v=read(adr); uint8_t c=v&1; v>>=1; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=5; break; }
        case 0x56: { uint16_t adr=addr_zpx(); uint8_t v=read(adr); uint8_t c=v&1; v>>=1; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=6; break; }
        case 0x4E: { uint16_t adr=addr_abs(); uint8_t v=read(adr); uint8_t c=v&1; v>>=1; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=6; break; }
        case 0x5E: { uint16_t adr=addr_absx(); uint8_t v=read(adr); uint8_t c=v&1; v>>=1; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=7; break; }

        case 0x26: { uint16_t adr=addr_zp(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; uint8_t oc=status&1; v=(v<<1)|oc; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=5; break; }
        case 0x36: { uint16_t adr=addr_zpx(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; uint8_t oc=status&1; v=(v<<1)|oc; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=6; break; }
        case 0x2E: { uint16_t adr=addr_abs(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; uint8_t oc=status&1; v=(v<<1)|oc; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=6; break; }
        case 0x3E: { uint16_t adr=addr_absx(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; uint8_t oc=status&1; v=(v<<1)|oc; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=7; break; }

        case 0x66: { uint16_t adr=addr_zp(); uint8_t v=read(adr); uint8_t c=v&1; uint8_t oc=(status&1)<<7; v=(v>>1)|oc; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=5; break; }
        case 0x76: { uint16_t adr=addr_zpx(); uint8_t v=read(adr); uint8_t c=v&1; uint8_t oc=(status&1)<<7; v=(v>>1)|oc; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=6; break; }
        case 0x6E: { uint16_t adr=addr_abs(); uint8_t v=read(adr); uint8_t c=v&1; uint8_t oc=(status&1)<<7; v=(v>>1)|oc; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=6; break; }
        case 0x7E: { uint16_t adr=addr_absx(); uint8_t v=read(adr); uint8_t c=v&1; uint8_t oc=(status&1)<<7; v=(v>>1)|oc; write(adr,v); setZN(v); status=(status&~0x01)|c; cycles=7; break; }

        // --- BRANCHES ---
        case 0x10: { int8_t r=(int8_t)fetch8(); if(!(status&0x80)) {pc+=r; cycles++;} break; }
        case 0x30: { int8_t r=(int8_t)fetch8(); if(status&0x80) {pc+=r; cycles++;} break; }
        case 0x50: { int8_t r=(int8_t)fetch8(); if(!(status&0x40)) {pc+=r; cycles++;} break; }
        case 0x70: { int8_t r=(int8_t)fetch8(); if(status&0x40) {pc+=r; cycles++;} break; }
        case 0x90: { int8_t r=(int8_t)fetch8(); if(!(status&0x01)) {pc+=r; cycles++;} break; }
        case 0xB0: { int8_t r=(int8_t)fetch8(); if(status&0x01) {pc+=r; cycles++;} break; }
        case 0xD0: { int8_t r=(int8_t)fetch8(); if(!(status&0x02)) {pc+=r; cycles++;} break; }
        case 0xF0: { int8_t r=(int8_t)fetch8(); if(status&0x02) {pc+=r; cycles++;} break; }

        // --- JUMPS ---
        case 0x4C: pc=fetch16(); cycles=3; break;
        case 0x6C: { uint16_t ptr=fetch16(); pc=read(ptr)|(read((ptr&0xFF00)|((ptr+1)&0xFF))<<8); cycles=5; break; }
        case 0x20: { uint16_t addr=fetch16(); uint16_t r=pc-1; push(r>>8); push(r&0xFF); pc=addr; cycles=6; break; }
        case 0x60: pc=(pop()|(pop()<<8))+1; cycles=6; break;
        case 0x40: status=(pop()&~0x30)|(status&0x30); pc=pop()|(pop()<<8); cycles=6; break;

        // --- STATUS ---
        case 0x18: status&=~0x01; break;
        case 0x38: status|=0x01; break;
        case 0x58: status&=~0x04; break;
        case 0x78: status|=0x04; break;
        case 0xB8: status&=~0x40; break;
        case 0xD8: status&=~0x08; break;
        case 0xF8: status|=0x08; break;

        // --- STACK/TRANSFERS ---
        case 0x08: push(status|0x30); cycles=3; break;
        case 0x28: status=(pop()&~0x30)|(status&0x30); cycles=4; break;
        case 0x48: push(a); cycles=3; break;
        case 0x68: a=pop(); setZN(a); cycles=4; break;
        case 0xAA: x=a; setZN(x); break;
        case 0x8A: a=x; setZN(a); break;
        case 0xA8: y=a; setZN(y); break;
        case 0x98: a=y; setZN(a); break;
        case 0xBA: x=sp; setZN(x); break;
        case 0x9A: sp=x; break;
        case 0xEA: break;
        case 0x00: break;

        // --- UNOFFICIAL (SACRED) ---
        case 0xA7: { a=read(addr_zp()); x=a; setZN(a); cycles=3; break; } // LAX ZP
        case 0xB7: { a=read(addr_zpy()); x=a; setZN(a); cycles=4; break; } // LAX ZP,Y
        case 0xAF: { a=read(addr_abs()); x=a; setZN(a); cycles=4; break; } // LAX Abs
        case 0xBF: { a=read(addr_absy()); x=a; setZN(a); cycles=4; break; } // LAX Abs,Y
        case 0xA3: { a=read(addr_indx()); x=a; setZN(a); cycles=6; break; } // LAX Ind,X
        case 0xB3: { a=read(addr_indy()); x=a; setZN(a); cycles=5; break; } // LAX Ind,Y

        case 0x07: { uint16_t adr=addr_zp(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); a|=v; setZN(a); status=(status&~0x01)|c; cycles=5; break; } // SLO ZP
        case 0x17: { uint16_t adr=addr_zpx(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); a|=v; setZN(a); status=(status&~0x01)|c; cycles=6; break; } // SLO ZP,X
        case 0x0F: { uint16_t adr=addr_abs(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); a|=v; setZN(a); status=(status&~0x01)|c; cycles=6; break; } // SLO Abs
        case 0x1F: { uint16_t adr=addr_absx(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); a|=v; setZN(a); status=(status&~0x01)|c; cycles=7; break; } // SLO Abs,X
        case 0x1B: { uint16_t adr=addr_absy(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); a|=v; setZN(a); status=(status&~0x01)|c; cycles=7; break; } // SLO Abs,Y
        case 0x03: { uint16_t adr=addr_indx(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); a|=v; setZN(a); status=(status&~0x01)|c; cycles=8; break; } // SLO Ind,X
        case 0x13: { uint16_t adr=addr_indy(); uint8_t v=read(adr); uint8_t c=(v&0x80)>>7; v<<=1; write(adr,v); a|=v; setZN(a); status=(status&~0x01)|c; cycles=8; break; } // SLO Ind,Y

        case 0xC7: { uint16_t adr=addr_zp(); uint8_t v=read(adr)-1; write(adr,v); uint8_t r=a-v; status=(status&~0x83)|(r&0x80)|(a>=v?1:0)|(a==v?2:0); cycles=5; break; } // DCP ZP
        case 0xD7: { uint16_t adr=addr_zpx(); uint8_t v=read(adr)-1; write(adr,v); uint8_t r=a-v; status=(status&~0x83)|(r&0x80)|(a>=v?1:0)|(a==v?2:0); cycles=6; break; } // DCP ZP,X

        case 0x5F: { uint16_t adr=addr_absy(); uint8_t v=read(adr); uint8_t c=v&1; v>>=1; write(adr,v); a^=v; setZN(a); status=(status&~0x01)|c; cycles=7; break; } // SRE Abs,Y

        default:
            LOGD("Unknown Opcode 0x%02X at PC: 0x%04X - Skipping as NOP", opcode, curPC);
            pc++; 
            cycles = 2;
            break;
    }
    if (apu) apu->step(cycles);
    return cycles;
}

void CPU::triggerNMI() {
    LOGD("NMI! PC: 0x%04X", pc);
    push(pc >> 8);
    push(pc & 0xFF);
    // Push status with Bit 5 (Unused) set and Bit 4 (B-Flag) clear
    push((status | 0x20) & ~0x10);
    pc = read16(0xFFFA);
    status |= 0x04;
    cyclesToStall = 7;
}