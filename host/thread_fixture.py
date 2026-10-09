"""Original three-thread ARM guest using standard SVCs and per-thread TLS."""
import hashlib
import struct
from .guest_fixture import Program
TEXT_ADDRESS,DATA_ADDRESS=0x00100000,0x08000000
PROGRAM_ID=0x000400000FF00F0B

def segments():
    main=Program();worker=Program();busy=Program()
    def svc(p,n):p.emit(0xef000000|n)
    def mov(p,d,s):p.emit(0xe1a00000|d<<12|s)
    def store(p,s,base,off):p.emit(0xe5800000|base<<16|s<<12|off)
    def wait(p,reg):
        mov(p,0,reg);p.constant(2,0xffffffff);p.constant(3,0xffffffff);svc(p,0x24)
    def entry(p,offset):
        address=TEXT_ADDRESS+len(p.words)*4;delta=TEXT_ADDRESS+offset-address-8
        p.emit(0xe59f1000|delta)
    # Main owns a mutex and starts a worker which must block before its release.
    main.constant(4,DATA_ADDRESS);main.emit(0xee1daf70);main.constant(0,0x1111);store(main,0,10,0)
    main.constant(1,1);svc(main,0x13);mov(main,5,1);store(main,5,4,0)
    main.constant(1,0);svc(main,0x17);mov(main,6,1);store(main,6,4,4)
    main.constant(0,0x30);entry(main,0x6f0);mov(main,2,4);main.constant(3,0x0fffe000);main.constant(4,0xfffffffe)
    svc(main,8);mov(main,7,1);main.constant(4,DATA_ADDRESS);store(main,7,4,8)
    # The second worker has no synchronization until exit; it forces time slicing.
    main.constant(0,0x30);entry(main,0x6f4);mov(main,2,4);main.constant(3,0x0fffd000);main.constant(4,0xffffffff)
    svc(main,8);mov(main,8,1);main.constant(4,DATA_ADDRESS);store(main,8,4,12)
    main.constant(0,0);main.constant(1,0);svc(main,0x0a)
    mov(main,0,5);svc(main,0x14);store(main,0,4,16)
    wait(main,6);store(main,0,4,20)
    wait(main,7);store(main,0,4,24)
    wait(main,8);store(main,0,4,28)
    main.emit(0xe59a0000);store(main,0,4,32);store(main,10,4,36)
    for r in (5,6,7,8):mov(main,0,r);svc(main,0x23)
    svc(main,3)
    # Child begins with r0=argument; another TLS slot and stack are selected.
    mov(worker,4,0);worker.emit(0xee1daf70);worker.constant(0,0x2222);store(worker,0,10,0)
    store(worker,10,4,40);worker.emit(0xe5945000);worker.emit(0xe5946004)
    # Releasing the main thread's mutex must fail instead of unlocking it.
    mov(worker,0,5);svc(worker,0x14);store(worker,0,4,44)
    wait(worker,5);store(worker,0,4,48)
    worker.constant(0,1000000);worker.constant(1,0);svc(worker,0x0a)
    mov(worker,0,5);svc(worker,0x14);store(worker,0,4,52)
    mov(worker,0,6);svc(worker,0x18)
    worker.constant(1,0xffff8000);svc(worker,0x37);store(worker,1,4,56);svc(worker,9)
    mov(busy,4,0);busy.emit(0xee1daf70);busy.constant(0,0x3333);store(busy,0,10,0);store(busy,10,4,60)
    busy.constant(0,100);busy.label('loop');busy.emit(0xe2500001);busy.branch('loop',1)
    store(busy,0,4,64);svc(busy,9)
    return ((0,main.finish(),False,'ctr_thread_main'),(0x300,worker.finish(),False,'ctr_thread_worker'),(0x500,busy.finish(),False,'ctr_thread_busy'))

def image_code():
    code=bytearray(8192)
    for at,data,_,_ in segments():code[at:at+len(data)]=data
    struct.pack_into('<2I',code,0x6f0,TEXT_ADDRESS+0x300,TEXT_ADDRESS+0x500)
    return bytes(code)

def fixture_bytes():
    from .stack_fixture import fixture_bytes as template
    out=bytearray(template());code=image_code()
    for at in (0x108,0x118):struct.pack_into('<Q',out,at,PROGRAM_ID)
    out[0x150:0x160]=b'CTR-P-THREAD'.ljust(16,b'\0');out[0x200:0x208]=b'CTRTHRD\0'
    struct.pack_into('<I',out,0x218,0x6f8);out[0xc00:]=code;out[0xbe0:0xc00]=hashlib.sha256(code).digest()
    out[0x160:0x180]=hashlib.sha256(out[0x200:0x600]).digest();out[0x1c0:0x1e0]=hashlib.sha256(out[0xa00:0xc00]).digest()
    return bytes(out)

def generated_sources():
    from translator.aot import translate
    generated=[];blocks=[]
    for at,data,_,symbol in segments():
        generated.append(translate(data,TEXT_ADDRESS+at,symbol,False))
        for pc in range(TEXT_ADDRESS+at,TEXT_ADDRESS+at+len(data),4):blocks.append(f'    {{0x{pc:08x}u,false,{symbol}_at_{pc:08x}}},')
    generated.append('const ctr_block ctr_thread_fixture_blocks[]={\n'+'\n'.join(blocks)+'\n};\n')
    generated.append('const size_t ctr_thread_fixture_block_count=sizeof(ctr_thread_fixture_blocks)/sizeof(ctr_thread_fixture_blocks[0]);\n')
    code=image_code();blob=',\n'.join('    '+','.join(f'0x{b:02x}' for b in code[i:i+32]) for i in range(0,len(code),32))
    metadata='#include "ctr_thread_fixture.h"\n'+f'static const uint8_t thread_bytes[]={{\n{blob}\n}};\n'
    metadata+=f'const ctr_guest_image ctr_thread_image={{thread_bytes,sizeof(thread_bytes),{{0x{TEXT_ADDRESS:08x}u,1u,0x6f8u}},{{0,0,0}},{{0x{DATA_ADDRESS:08x}u,1u,4096u}},0,16384u,"{hashlib.sha256(code).hexdigest()}"}};\n'
    return {'thread.generated.c':'\n'.join(generated),'thread-image.generated.c':metadata}
