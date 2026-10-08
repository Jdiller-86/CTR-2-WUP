"""Original ARM HID -> shared GSP framebuffer queue -> VBlank relay probe."""
import hashlib
import struct
from .gsp_fixture import gsp_code,TEXT_ADDRESS,DATA_ADDRESS,BSS_SIZE
PROGRAM_ID=0x000400000FF00F09

def relay_code():
    return gsp_code(shared=True)

def image_code():
    code=bytearray(8192);text=relay_code();assert len(text)<=4096
    code[:len(text)]=text;code[4096+0x300:4096+0x305]=b'srv:\0';return bytes(code)

def fixture_bytes():
    from .gsp_fixture import fixture_bytes as template
    out=bytearray(template());code=image_code()
    for at in (0x108,0x118):struct.pack_into('<Q',out,at,PROGRAM_ID)
    out[0x150:0x160]=b'CTR-P-RELAY'.ljust(16,b'\0');out[0x200:0x208]=b'CTRRELAY'
    struct.pack_into('<I',out,0x218,len(relay_code()))
    out[0xc00:]=code;out[0xbe0:0xc00]=hashlib.sha256(code).digest()
    out[0x160:0x180]=hashlib.sha256(out[0x200:0x600]).digest()
    out[0x1c0:0x1e0]=hashlib.sha256(out[0xa00:0xc00]).digest();return bytes(out)

def generated_sources():
    from translator.aot import translate
    code=image_code();blob=',\n'.join('    '+','.join(f'0x{b:02x}' for b in code[i:i+32]) for i in range(0,len(code),32))
    meta='#include "ctr_relay_fixture.h"\n'+f'static const uint8_t relay_image_bytes[]={{\n{blob}\n}};\n'
    meta+='const ctr_guest_image ctr_relay_image={relay_image_bytes,sizeof(relay_image_bytes),\n'
    meta+=f'    {{0x{TEXT_ADDRESS:08x}u,1u,{len(relay_code())}u}},{{0,0,0}},{{0x{DATA_ADDRESS:08x}u,1u,4096u}},\n'
    meta+=f'    {BSS_SIZE}u,16384u,"{hashlib.sha256(code).hexdigest()}"}};\n'
    return {'relay.generated.c':translate(relay_code(),TEXT_ADDRESS,'ctr_relay_fixture',False),'relay-image.generated.c':meta}
