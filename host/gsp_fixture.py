"""Original ARM program: standard HID input -> RGB565 CPU frames -> GSP IPC."""
import hashlib
import struct
from .guest_fixture import Program,immediate
from .hid_fixture import hid_startup
TEXT_ADDRESS=0x00100000
DATA_ADDRESS=0x08000000
TOP_ADDRESS=DATA_ADDRESS+4096
BOTTOM_ADDRESS=TOP_ADDRESS+400*240*2
BSS_SIZE=(400+320)*240*2
PROGRAM_ID=0x000400000FF00F08

def gsp_code():
    p=Program();word,request=hid_startup(p)
    word(0,0x50100);word(1,0x3a707367);word(2,0x7570473a);word(3,8);word(4,1);request(4)
    p.emit(0xE59AC00C)
    word(0,0x160042);word(1,0);word(2,0);word(3,0xffff8001);request(12)
    word(0,0xb0040);word(1,0);request(12)
    p.label('frame')
    p.emit(0xE1A00008);p.constant(2,0xffffffff);p.constant(3,0xffffffff);p.emit(0xEF000024)
    p.emit(0xE5970100);p.emit(0xE3500000);p.branch('exit',1)
    p.emit(0xE5993010);p.emit(0xE0893203);p.emit(0xE5930028);p.emit(0xE5870000)
    p.emit(0xE3100001);p.branch('blue',0);p.constant(0,0x07e0);p.branch('top')
    p.label('blue');p.constant(0,0x001f)
    p.label('top');p.constant(1,TOP_ADDRESS);p.constant(2,400*240)
    p.label('top_pixel');p.emit(0xE1C100B0);p.emit(0xE2811002);p.emit(0xE2522001);p.branch('top_pixel',1)
    # Corner markers independently reveal rotation, stride and channel order.
    for x,y,color in ((0,0,0xffff),(399,0,0xf800),(0,239,0x07e0),(399,239,0x001f)):
        p.constant(1,TOP_ADDRESS+x*480+(239-y)*2);p.constant(0,color);p.emit(0xE1C100B0)
    p.emit(0xE1D130B0);p.emit(0xE5873014) # unsigned LDRH of final blue marker
    p.emit(0xE59930B8);p.emit(0xE0893183);p.emit(0xE59300C8);p.emit(0xE5870008)
    p.emit(0xE59300CC);p.emit(0xE587000C);p.emit(0xE3500000);p.branch('teal',0)
    p.constant(0,0xf81f);p.branch('bottom')
    p.label('teal');p.constant(0,0x07ff)
    p.label('bottom');p.constant(1,BOTTOM_ADDRESS);p.constant(2,320*240)
    p.label('bottom_pixel');p.emit(0xE1C100B0);p.emit(0xE2811002);p.emit(0xE2522001);p.branch('bottom_pixel',1)
    p.emit(0xE597000C);p.emit(0xE3500000);p.branch('submit',0)
    p.emit(0xE5970008);p.constant(2,0xffff);p.emit(0xE0002002) # x
    p.emit(0xE1A00820);p.emit(0xE26000EF) # y=239-y
    p.constant(1,BOTTOM_ADDRESS);p.emit(0xE0811482);p.emit(0xE0411282);p.emit(0xE0811080)
    p.constant(0,0xffff);p.emit(0xE1C100B0)
    p.label('submit')
    for screen,base in ((0,TOP_ADDRESS),(1,BOTTOM_ADDRESS)):
        for i,v in enumerate((0x50200,screen,0,base,base,480,(0x142 if screen==0 else 0x102),0,0)):word(i,v)
        request(12)
    p.emit(0xE5970010);p.emit(0xE2800001);p.emit(0xE5870010);p.branch('frame')
    p.label('exit');word(0,0x170000);request(12);p.emit(0xE1A0000C);p.emit(0xEF000023)
    p.emit(0xE1A00006);p.emit(0xE1A01009);p.emit(0xEF000020);p.constant(11,0)
    p.label('close');p.emit(0xE087010B);p.emit(0xE5900200);p.emit(0xEF000023)
    p.emit(0xE28BB001);p.emit(0xE35B0006);p.branch('close',1)
    p.emit(0xE1A00005);p.emit(0xEF000023);p.emit(0xE1A00004);p.emit(0xEF000023);p.emit(0xEF000003)
    return p.finish()

def image_code():
    code=bytearray(8192);text=gsp_code();assert len(text)<=4096
    code[:len(text)]=text;code[4096+0x300:4096+0x305]=b'srv:\0';return bytes(code)

def fixture_bytes():
    from .hid_fixture import fixture_bytes as template
    out=bytearray(template());code=image_code()
    for at in (0x108,0x118):struct.pack_into('<Q',out,at,PROGRAM_ID)
    out[0x150:0x160]=b'CTR-P-GSPTEST'.ljust(16,b'\0');out[0x200:0x208]=b'CTRGSP\0\0'
    struct.pack_into('<I',out,0x218,len(gsp_code()));struct.pack_into('<I',out,0x23c,BSS_SIZE)
    out[0x458:0x460]=b'gsp::Gpu';out[0xc00:]=code;out[0xbe0:0xc00]=hashlib.sha256(code).digest()
    out[0x160:0x180]=hashlib.sha256(out[0x200:0x600]).digest();out[0x1c0:0x1e0]=hashlib.sha256(out[0xa00:0xc00]).digest()
    return bytes(out)

def generated_sources():
    from translator.aot import translate
    code=image_code();blob=',\n'.join('    '+','.join(f'0x{b:02x}' for b in code[i:i+32]) for i in range(0,len(code),32))
    meta='#include "ctr_gsp_fixture.h"\n'+f'static const uint8_t gsp_image_bytes[]={{\n{blob}\n}};\n'
    meta+='const ctr_guest_image ctr_gsp_image={gsp_image_bytes,sizeof(gsp_image_bytes),\n'
    meta+=f'    {{0x{TEXT_ADDRESS:08x}u,1u,{len(gsp_code())}u}},{{0,0,0}},{{0x{DATA_ADDRESS:08x}u,1u,4096u}},\n'
    meta+=f'    {BSS_SIZE}u,16384u,"{hashlib.sha256(code).hexdigest()}"}};\n'
    return {'gsp.generated.c':translate(gsp_code(),TEXT_ADDRESS,'ctr_gsp_fixture',False),'gsp-image.generated.c':meta}
