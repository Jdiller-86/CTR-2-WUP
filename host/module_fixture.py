"""Original ARM/Thumb CRO loader guest and two communicating modules."""
import struct
from translator.modules import FIELDS,array,generate
from translator.aot import translate
from .guest_fixture import Program

TEXT=0x100000
DATA=0x08000000
ROOT=0x200000
MATH=0x300000
CLIENT=0x400000
HEAP=0x08010000
CLIENT_DATA=0x08011000
CODE=0x180

def export_tree(names):
    """Original bounded binary lookup records, including the sentinel root."""
    if not names:return b''
    nodes=[None]
    def branch(indices):
        here=len(nodes);nodes.append(None)
        if len(indices)==1:
            nodes[here]=(0,0,0,indices[0]);return 0x8000|here
        for bit in range(128*8):
            left=[];right=[]
            for i in indices:
                value=ord(names[i][bit//8]) if bit//8<len(names[i]) else 0
                (right if value&(1<<(bit%8)) else left).append(i)
            if left and right:break
        else:raise ValueError('Original exports must have distinct bounded names')
        nodes[here]=(bit,branch(left),branch(right),0);return here
    nodes[0]=(0,branch(list(range(len(names)))),0,0)
    return b''.join(struct.pack('<4H',*node) for node in nodes)

def cro(name,code,segments,exports=(),indexed=(),internal=(),named=(),imports=(),data=b''):
    """Write documented file records; symbol results are checked by guest math."""
    blob=bytearray(4096);h=dict.fromkeys(FIELDS,0);cursor=CODE
    def section(field,count,payload,stride=1):
        nonlocal cursor
        cursor=(cursor+3)&~3;h[field]=cursor;h[count]=len(payload)//stride
        blob[cursor:cursor+len(payload)]=payload;cursor+=len(payload)
    section('code','code_size',code)
    section('module_name','module_name_size',name.encode()+b'\0');h['name']=h['module_name']
    section('segments','segment_count',b''.join(struct.pack('<3I',*s) for s in segments),12)
    section('exports','export_count',bytes(len(exports)*8),8)
    section('tree','tree_count',export_tree([symbol for symbol,_ in exports]),8)
    section('index_exports','index_export_count',b''.join(struct.pack('<I',tag) for tag in indexed),4)
    strings=b''.join(symbol.encode()+b'\0' for symbol,_ in exports)
    section('export_strings','export_strings_size',strings)
    at=h['export_strings']
    for i,(symbol,tag) in enumerate(exports):
        struct.pack_into('<2I',blob,h['exports']+i*8,at,tag);at+=len(symbol)+1
    section('import_modules','import_module_count',bytes(len(imports)*20),20)
    batches=[]
    def add_batch(rows):
        at=len(batches)
        batches.extend(struct.pack('<IBBBBI',tag,typ,int(i==len(rows)-1),0,0,addend) for i,(tag,typ,addend) in enumerate(rows))
        return at
    named_batches=[add_batch(rows) for _,rows in named]
    indexed_rows=[];anonymous_rows=[];module_rows=[]
    for module,indices,anonymous in imports:
        first_index=len(indexed_rows);first_anon=len(anonymous_rows)
        indexed_rows.extend((index,add_batch(rows)) for index,rows in indices)
        anonymous_rows.extend((tag,add_batch(rows)) for tag,rows in anonymous)
        module_rows.append((module,first_index,len(indices),first_anon,len(anonymous)))
    section('external','external_count',b''.join(batches),12)
    section('imports','import_count',bytes(len(named)*8),8)
    section('index_imports','index_import_count',bytes(len(indexed_rows)*8),8)
    section('anon_imports','anon_import_count',bytes(len(anonymous_rows)*8),8)
    names=[name for name,_ in named]+[row[0] for row in module_rows]
    section('import_strings','import_strings_size',b''.join(n.encode()+b'\0' for n in names))
    at=h['import_strings']
    for i,((name,_),batch) in enumerate(zip(named,named_batches)):
        struct.pack_into('<2I',blob,h['imports']+i*8,at,h['external']+batch*12);at+=len(name)+1
    for table,rows in [('index_imports',indexed_rows),('anon_imports',anonymous_rows)]:
        for i,(value,batch) in enumerate(rows):struct.pack_into('<2I',blob,h[table]+i*8,value,h['external']+batch*12)
    for i,(name,first_index,num_index,first_anon,num_anon) in enumerate(module_rows):
        struct.pack_into('<5I',blob,h['import_modules']+i*20,at,h['index_imports']+first_index*8,num_index,h['anon_imports']+first_anon*8,num_anon);at+=len(name)+1
    section('static_anon','static_anon_count',b'',8)
    section('internal','internal_count',b''.join(struct.pack('<IBBBBI',tag,typ,segment,0,0,addend) for tag,typ,segment,addend in internal),12)
    section('static_relocs','static_reloc_count',b'',12)
    section('data','data_size',data)
    # DATA is the final section; fill its segment offset once layout is known.
    for i,(offset,size,kind) in enumerate(segments):
        if kind==2 and code:struct.pack_into('<I',blob,h['segments']+i*12,h['data'])
        if kind==3:h['bss_size']=size
    if cursor>4096:raise ValueError('Original CRO fixture exceeds one page')
    h['magic']=0x304f5243;h['file_size']=4096
    for i,field in enumerate(FIELDS):struct.pack_into('<I',blob,0x80+i*4,h[field])
    return bytes(blob)

def module_files():
    root=cro('main',b'',[(TEXT,8192,0),(DATA,16384,2)],[('main_helper',0x1800<<4)])
    math_code=struct.pack('<10I',0xe92d4010,0xe59f4014,0xe12fff34,0xe59f1010,0xe5810000,0xe8bd8010,0xe2800007,0xe12fff1e,0,0)+struct.pack('<2H',0x3009,0x4770)
    math=cro('math',math_code,[(CODE,len(math_code),0),(0,0,1),(0,8,2),(0,4,3)],
        [('entry',0),('thumb',41<<4)],indexed=[24<<4],internal=[(36<<4,2,3,0),((4<<4)|2,3,0,24)],
        named=[('main_helper',[(32<<4,2,0),(2,38,0)])],data=bytes(8))
    client_code=struct.pack('<5I',0xe92d4000,0xe59f3004,0xe12fff33,0xe8bd8000,0)
    client=cro('client',client_code,[(CODE,len(client_code),0),(0,4,2)],exports=[('client_entry',0)],
        imports=[('math',[(0,[(16<<4,2,0)])],[(0,[(1,2,0)])])],data=bytes(4))
    return {'main.crs':root,'math.cro':math,'client.cro':client}

def manifest():
    return {'schema_version':1,'modules':[
        {'file':'main.crs','address':ROOT,'static':True},
        {'file':'math.cro','address':MATH,'data_address':HEAP,'bss_address':HEAP+128,
         'ranges':[{'offset':0,'size':32},{'offset':40,'size':4,'thumb':True}]},
        {'file':'client.cro','address':CLIENT,'data_address':CLIENT_DATA,'ranges':[{'offset':0,'size':16}]}]}

def guest_program():
    p=Program();expected=[]
    def constant(r,v):p.constant(r,v)
    def svc(n):p.emit(0xef000000|n)
    def mov(d,s):p.emit(0xe1a00000|d<<12|s)
    def save(offset):p.emit(0xe5880000|offset)
    def request(handle,words,result=0):
        for i,value in enumerate(words):constant(0,value);p.emit(0xe5890000|i*4)
        mov(0,handle);svc(0x32);p.emit(0xe5990004);save(len(expected)*4);expected.append(result)
    def allocation(address,free=False,result=0,save_at=None):
        for r,v in enumerate((1 if free else 3,address,0,4096,0 if free else 3)):constant(r,v)
        svc(1)
        if save_at is not None:save(save_at)
        else:save(len(expected)*4);expected.append(result)
    def load(which,link,result=0):
        request(6,[0x000902c2,DATA+(which+1)*4096,MATH if which==1 else CLIENT,4096,
            HEAP if which==1 else CLIENT_DATA,0,8 if which==1 else 4,HEAP+128 if which==1 else 0,4 if which==1 else 0,int(link),0,0,0,0xffff8001],result)
    def unload(base,source,result=0):request(6,[0x000500c2,base,0,source,0,0xffff8001],result)
    constant(0,0);p.emit(0xeb000000|((0x1800-len(p.words)*4-8)//4))
    constant(8,DATA);p.emit(0xee1d9f70);p.emit(0xe2899080)
    constant(1,DATA+0x100);svc(0x2d);mov(5,1)
    request(5,[0x10002,0x20,0])
    request(5,[0x50100,int.from_bytes(b'ldr:','little'),int.from_bytes(b'ro\0\0','little'),6,0]);p.emit(0xe599600c)
    request(6,[0x100c2,DATA+4096,4096,ROOT,0,0xffff8001])
    allocation(HEAP);allocation(CLIENT_DATA)
    load(2,True,0xd8a12c21)  # Missing provider is an error without publication.
    load(1,True);load(2,False)
    request(6,[0x60042,CLIENT,0,0xffff8001])
    for address,offset in [(MATH+CODE,0x200),(MATH+CODE+24,0x208),(MATH+CODE+41,0x20c),(CLIENT+CODE,0x210)]:
        constant(0,5);constant(3,address);p.emit(0xe12fff33);save(offset)
    constant(1,HEAP+128);p.emit(0xe5910000);save(0x204)
    unload(MATH,DATA+8192,0xd8a12c0d)  # Client holds imports into math.
    allocation(HEAP,True,save_at=0x214)
    request(6,[0x70042,CLIENT,0,0xffff8001]);request(6,[0x60042,CLIENT,0,0xffff8001])
    unload(CLIENT,DATA+12288);unload(MATH,DATA+8192)
    request(6,[0x80042,DATA+4096,0,0xffff8001])
    allocation(HEAP,True);allocation(CLIENT_DATA,True)
    mov(0,6);svc(0x23);mov(0,5);svc(0x23);svc(3)
    return p.finish(),expected

def image_code():
    files=module_files();code,_=guest_program();image=bytearray(8192+16384)
    if len(code)>0x1800:raise ValueError('Original loader guest overlaps helper')
    image[:len(code)]=code;image[0x1800:0x1808]=struct.pack('<2I',0xe0800080,0xe12fff1e)
    image[8192+0x100:8192+0x105]=b'srv:\0'
    for i,blob in enumerate(files.values(),1):image[8192+i*4096:8192+(i+1)*4096]=blob
    return bytes(image)

def fixture_bytes():
    from tests.host_fixtures import ncch
    import hashlib
    out=ncch([('.code',image_code())],program_id=0x000400000ff00f0d)
    struct.pack_into('<3I',out,0x210,TEXT,2,8192)
    struct.pack_into('<3I',out,0x230,DATA,4,16384)
    out[0x160:0x180]=hashlib.sha256(out[0x200:0x600]).digest()
    return bytes(out)

def generated_sources(folder):
    package_source,report=generate(manifest(),folder)
    code,expected=guest_program();image=image_code()
    generated=[translate(code,TEXT,'ctr_module_guest',False),translate(bytes(image[0x1800:0x1808]),TEXT+0x1800,'ctr_module_helper',False)]
    rows=[f'    {{0x{TEXT+i:08x}u,false,ctr_module_guest_at_{TEXT+i:08x}}},' for i in range(0,len(code),4)]
    rows +=[f'    {{0x{TEXT+i:08x}u,false,ctr_module_helper_at_{TEXT+i:08x}}},' for i in (0x1800,0x1804)]
    generated.append('const ctr_block ctr_module_fixture_blocks[]={\n'+'\n'.join(rows)+'\n};\nconst size_t ctr_module_fixture_block_count=sizeof(ctr_module_fixture_blocks)/sizeof(ctr_block);\n')
    metadata='#include "ctr_module.h"\n'+array('module_guest_bytes',image)
    metadata+='const ctr_guest_image ctr_module_fixture_image={module_guest_bytes,sizeof(module_guest_bytes),{0x100000u,2,8192},{0,0,0},{0x08000000u,4,16384},0,16384,"original-module-loader"};\n'
    metadata+='const uint32_t ctr_module_fixture_results[]={'+','.join(hex(v)+'u' for v in expected)+'};\nconst size_t ctr_module_fixture_result_count=sizeof(ctr_module_fixture_results)/sizeof(uint32_t);\n'
    return {'guest.generated.c':'\n'.join(generated),'image.generated.c':metadata,'modules.generated.c':package_source},report
