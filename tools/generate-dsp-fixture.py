#!/usr/bin/env python3
"""Generate the original ARM DSP/NDSP PCM probe and its bounded AOT blocks."""
import hashlib
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host.guest_fixture import Program
from translator.aot import translate


def code():
    p=Program()
    def mov(dst,src,cond=14): p.emit(cond<<28|0x01a00000|dst<<12|src)
    def word(i,v): p.constant(0,v);p.emit(0xe5860000|i*4)
    def request(words):
        for i,v in enumerate(words): word(i,v)
        mov(0,5);p.emit(0xef000032)
    p.constant(7,0x08000000);p.emit(0xee1d6f70);p.emit(0xe2866080)
    p.constant(1,0x08000000);p.emit(0xef00002d);mov(4,1)
    for i,v in enumerate([0x00010002,0x20,0]): word(i,v)
    mov(0,4);p.emit(0xef000032)
    for i,v in enumerate([0x00050100,0x3a707364,0x5053443a,8,0]): word(i,v)
    mov(0,4);p.emit(0xef000032);p.emit(0xe596500c)
    request([0x001100c2,16,255,255,0x10a,0x08000010])
    p.constant(1,1);p.emit(0xef000017);mov(8,1)
    for i,v in enumerate([0x00150082,2,2,0]): word(i,v)
    p.emit(0xe5868010);mov(0,5);p.emit(0xef000032)
    request([0x00160000]);p.emit(0xe596900c)
    request([0x00170040,0x2000]);request([0x000d0082,2,4,0x10402,0x08000020])
    request([0x00070040,0x4000])
    # Owned linear memory; genuine guest stores generate two square waves.
    for reg,value in enumerate([0x10003,0,0,0x20000,3]): p.constant(reg,value)
    p.emit(0xef000001);p.constant(1,0x14000000);p.constant(2,0x14010000);p.constant(0,8192)
    p.constant(4,0x17701770);p.constant(12,0xe890e890)
    p.label('pcm');p.emit(0xe3100020);mov(3,4,0);mov(3,12,1);p.emit(0xe5813000);p.emit(0xe2811004)
    p.emit(0xe3100010);mov(3,4,0);mov(3,12,1);p.emit(0xe5823000);p.emit(0xe2822004)
    p.emit(0xe2500001);p.branch('pcm',1)
    p.constant(10,0);p.constant(11,4);p.constant(12,0x1ff40000)
    p.label('frame')
    # Commands are written by ARM, in alternating NDSP banks.
    def store(offset,value): p.constant(0,value);p.emit(0xe58c0000|offset)
    store(0,0);p.emit(0xe58cb000)
    # Both sources loop; gain changes give left, right, then stereo phases.
    for channel in range(2):
        base=0x100+channel*0xc0
        store(base,0x0e000000)
        p.emit(0xe35a0000);p.branch('existing'+str(channel),1)
        store(base,0x7e270010);store(base+0x34,0x3f800000)
        store(base+0xa0,0x00010001)
        store(base+0xac,0x00002000 if channel==0 else 0x00002001)
        store(base+0xb0,0x40000000);store(base+0xb4,5)
        store(base+0xbc,((channel+1)<<16)|2)
        p.label('existing'+str(channel))
        p.constant(1,0)
        p.constant(2,0x3f800000)
        p.emit(0xe35a0064) # 100 blocks = left phrase
        if channel==0:
            mov(1,2,3);p.emit(0xe35a00c8);mov(1,2,2)
        else: mov(1,2,2)
        p.emit(0xe58c1000|base+4+channel*4)
        store(base+4+(1-channel)*4,0)
    # Signal -> sticky IRQ wait -> clear. Host advances only the scheduled time.
    mov(0,8);p.emit(0xef000019)
    mov(0,9);p.emit(0xef000018)
    mov(0,8);p.constant(2,0xffffffff);p.constant(3,0xffffffff);p.emit(0xef000024)
    p.emit(0xe28aa001);p.emit(0xe28bb001)
    p.constant(0,0x20000);p.emit(0xe02cc000) # eor bank address,bank size
    p.emit(0xe35a0f4b) # cmp r10,#300
    p.branch('frame',1)
    request([0x000d0082,2,4,0x10402,0x08000024])
    p.emit(0xef000003)
    return p.finish()


def generated():
    text=code(); image=bytearray(8192);image[:len(text)]=text
    image[4096:4101]=b'srv:\0';image[4112:4128]=b'CTR2WUP-PCM-HLE\x01';image[4132]=1
    if len(text)>4096: raise ValueError('DSP text exceeds one page')
    digest=hashlib.sha256(image).hexdigest()
    rows=',\n'.join('    '+','.join(f'0x{b:02x}' for b in image[i:i+32]) for i in range(0,len(image),32))
    metadata='#include "ctr_dsp_fixture.h"\nstatic const uint8_t bytes[]={\n'+rows+'\n};\n'
    metadata+=f'const ctr_guest_image ctr_dsp_image={{bytes,sizeof(bytes),{{0x00100000,1,{len(text)}}},{{0,0,0}},{{0x08000000,1,4096}},0,16384,"{digest}"}};\n'
    return {'dsp.generated.c':translate(text,0x00100000,'ctr_dsp_fixture',False),'dsp-image.generated.c':metadata}

if __name__=='__main__':
    folder=ROOT/'tests/fixtures/dsp';folder.mkdir(exist_ok=True)
    for name,data in generated().items(): (folder/name).write_text(data,encoding='utf-8')
