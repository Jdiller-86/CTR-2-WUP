"""Original ARM/Thumb nested calls and checked stack/register-transfer probe."""
import hashlib
import struct
from .guest_fixture import Program
TEXT_ADDRESS,DATA_ADDRESS=0x00100000,0x08000000
PROGRAM_ID=0x000400000FF00F0A
EXPECTED=(10,10,3,7,0x4444,0x5555,DATA_ADDRESS,0x7777)

def segments():
    p=Program()
    for register,value in ((4,0x4444),(5,0x5555),(6,DATA_ADDRESS),(7,0x7777)):p.constant(register,value)
    def pointer(register,slot):
        offset=0x2f0+slot*4-len(p.words)*4-8;p.emit(0xe59f0000|register<<12|offset)
    pointer(8,0);pointer(12,0);p.emit(0xe12fff3c)
    pointer(12,1);p.emit(0xe12fff3c)
    for index,register in enumerate((4,5,6,7)):p.emit(0xe5860010|register<<12|index*4)
    p.emit(0x08bb0003) # Failed EQ: no transfer from unmapped r11, no writeback.
    p.emit(0xef000003)
    arm=struct.pack('<8I',0xe92d40f0,0xe3a04003,0xe3a05007,0xe0840005,
                    0xe8860031,0xe286600c,0xe936000e,0xe8bd80f0)
    thumb=struct.pack('<12H',0xb530,0x240d,0x2511,0x3411,0xc630,0x3e08,
                      0xce0c,0x3e08,0x47c0,0xc60f,0x3e10,0xbd30)
    return ((0,p.finish(),False,'ctr_stack_main'),(0x100,arm,False,'ctr_stack_arm'),(0x200,thumb,True,'ctr_stack_thumb'))

def image_code():
    code=bytearray(8192)
    for offset,data,_,_ in segments():code[offset:offset+len(data)]=data
    struct.pack_into('<2I',code,0x2f0,TEXT_ADDRESS+0x100,TEXT_ADDRESS+0x201);return bytes(code)

def fixture_bytes():
    from .gsp_fixture import fixture_bytes as template
    out=bytearray(template());code=image_code()
    for at in (0x108,0x118):struct.pack_into('<Q',out,at,PROGRAM_ID)
    out[0x150:0x160]=b'CTR-P-STACK'.ljust(16,b'\0');out[0x200:0x208]=b'CTRSTACK'
    struct.pack_into('<I',out,0x218,0x2f8);struct.pack_into('<I',out,0x23c,0)
    out[0x450:0x460]=bytes(16);out[0xc00:]=code;out[0xbe0:0xc00]=hashlib.sha256(code).digest()
    out[0x160:0x180]=hashlib.sha256(out[0x200:0x600]).digest()
    out[0x1c0:0x1e0]=hashlib.sha256(out[0xa00:0xc00]).digest();return bytes(out)

def generated_sources():
    from translator.aot import translate
    generated=[];blocks=[]
    for offset,data,thumb,symbol in segments():
        generated.append(translate(data,TEXT_ADDRESS+offset,symbol,thumb))
        width=2 if thumb else 4
        for at in range(TEXT_ADDRESS+offset,TEXT_ADDRESS+offset+len(data),width):
            blocks.append(f'    {{0x{at:08x}u,{"true" if thumb else "false"},{symbol}_at_{at:08x}}},')
    generated.append('const ctr_block ctr_stack_fixture_blocks[]={\n'+'\n'.join(blocks)+'\n};\n')
    generated.append('const size_t ctr_stack_fixture_block_count=sizeof(ctr_stack_fixture_blocks)/sizeof(ctr_stack_fixture_blocks[0]);\n')
    code=image_code();blob=',\n'.join('    '+','.join(f'0x{b:02x}' for b in code[i:i+32]) for i in range(0,len(code),32))
    metadata='#include "ctr_stack_fixture.h"\n'+f'static const uint8_t stack_bytes[]={{\n{blob}\n}};\n'
    metadata+=f'const ctr_guest_image ctr_stack_image={{stack_bytes,sizeof(stack_bytes),{{0x{TEXT_ADDRESS:08x}u,1u,0x2f8u}},{{0,0,0}},{{0x{DATA_ADDRESS:08x}u,1u,4096u}},0,16384u,"{hashlib.sha256(code).hexdigest()}"}};\n'
    return {'stack.generated.c':'\n'.join(generated),'stack-image.generated.c':metadata}
