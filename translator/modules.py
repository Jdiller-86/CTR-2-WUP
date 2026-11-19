"""Prepare a bounded package of original/pretranslated CRO0 modules.

Addresses and instruction ranges are explicit. The runtime compares the loaded
file and relocated code with this package before enabling translated dispatch.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
from .aot import translate

FIELDS='magic name next previous file_size bss_size fixed zero unknown_tag load_tag exit_tag unresolved_tag code code_size data data_size module_name module_name_size segments segment_count exports export_count index_exports index_export_count export_strings export_strings_size tree tree_count import_modules import_module_count external external_count imports import_count index_imports index_import_count anon_imports anon_import_count import_strings import_strings_size static_anon static_anon_count internal internal_count static_relocs static_reloc_count'.split()
MAX_FILE=4*1024*1024

def u32(data,at):return struct.unpack_from('<I',data,at)[0]
def put(data,at,value):struct.pack_into('<I',data,at,value&0xffffffff)
def bounded(at,size,limit):return 0<=at<=limit and 0<=size<=limit-at

class Module:
    def __init__(self,entry,folder):
        self.entry=entry;path=folder/entry['file']
        if not path.is_file() or path.is_symlink() or path.stat().st_size>MAX_FILE:raise ValueError('Module must be a bounded regular file')
        self.source=path.read_bytes();self.bytes=bytearray(self.source)
        self.h={name:u32(self.source,0x80+i*4) for i,name in enumerate(FIELDS)} if len(self.source)>=0x138 else {}
        self.address=entry['address'];self.static=entry.get('static',False)
        self.data=entry.get('data_address',0);self.bss=entry.get('bss_address',0)
        if not self.h or self.h['magic']!=0x304f5243 or self.h['file_size']!=len(self.source) or len(self.source)%4096:
            raise ValueError('Expected a page-padded, unfixed CRO0')
        if self.address%4096 or not 0x100000<=self.address<=0x8000000-len(self.source):raise ValueError('Invalid module load address')
        if self.h['fixed'] or self.h['next'] or self.h['previous'] or self.h['static_anon_count'] or self.h['static_reloc_count']:
            raise ValueError('Fixed/registered and static-relocation forms are unsupported')
        for table,count,stride in [('code','code_size',1),('data','data_size',1),('module_name','module_name_size',1),('segments','segment_count',12),('exports','export_count',8),('index_exports','index_export_count',4),('export_strings','export_strings_size',1),('tree','tree_count',8),('import_modules','import_module_count',20),('external','external_count',12),('imports','import_count',8),('index_imports','index_import_count',8),('anon_imports','anon_import_count',8),('import_strings','import_strings_size',1),('internal','internal_count',12)]:
            if not bounded(self.h[table],self.h[count]*stride,len(self.source)):raise ValueError('Module table is out of bounds')
        if not 1<=self.h['segment_count']<=4:raise ValueError('Unsupported segment count')
        self.segments=[]
        for i in range(self.h['segment_count']):
            offset,size,kind=struct.unpack_from('<3I',self.source,self.h['segments']+i*12)
            if kind>3 or size>MAX_FILE:raise ValueError('Unsupported segment')
            if not self.static and kind<2 and not bounded(offset,size,len(self.source)):raise ValueError('Invalid file segment')
            address=offset if self.static else self.address+offset if kind<2 else self.data if kind==2 else self.bss
            if address%4 or not bounded(address,size,0x40000000):raise ValueError('Invalid segment address')
            self.segments.append((offset,size,kind,address))
        self.name=self.string(self.h['module_name'],'module_name','module_name_size')
        self.exports={}
        for i in range(self.h['export_count']):
            at=self.h['exports']+i*8;name=self.string(u32(self.source,at),'export_strings','export_strings_size')
            if name in self.exports:raise ValueError('Duplicate export')
            self.exports[name]=self.tag(u32(self.source,at+4))[0]

    def string(self,at,table,size):
        if not self.h[table]<=at<self.h[table]+self.h[size]:raise ValueError('String is out of bounds')
        end=self.source.find(b'\0',at,min(at+128,self.h[table]+self.h[size]))
        if end<=at or any(c<32 or c>126 for c in self.source[at:end]):raise ValueError('Invalid symbol/module name')
        return self.source[at:end].decode('ascii')

    def tag(self,value,width=1):
        index=value&15;offset=value>>4
        if index>=len(self.segments):raise ValueError('Invalid segment tag')
        file,size,kind,address=self.segments[index]
        if not bounded(offset,width,size):raise ValueError('Segment tag is out of bounds')
        return address+offset,file+offset,kind

    def relocated_code(self,providers):
        if self.static:return b''
        seen=set();used=set()
        def apply(at,symbol):
            tag=u32(self.source,at);address,file,kind=self.tag(tag,4);typ=self.source[at+4]
            if address%4 or address in seen:raise ValueError('Misaligned or duplicate relocation target')
            seen.add(address)
            if typ not in (0,2,3,38):raise ValueError('Unsupported relocation type')
            if kind<2 and typ:
                value=symbol+u32(self.source,at+8)-(address if typ==3 else 0);put(self.bytes,file,value)
        def batch(at,symbol):
            first=self.h['external'];count=self.h['external_count']
            if at<first or (at-first)%12:raise ValueError('Invalid relocation batch')
            for i in range((at-first)//12,count):
                if i in used:raise ValueError('Repeated relocation batch')
                used.add(i);entry=first+i*12;apply(entry,symbol)
                if self.source[entry+5]==1:return
                if self.source[entry+5]:raise ValueError('Invalid batch terminator')
            raise ValueError('Unterminated relocation batch')
        for i in range(self.h['internal_count']):
            at=self.h['internal']+i*12;index=self.source[at+5]
            if index>=len(self.segments):raise ValueError('Invalid internal relocation')
            apply(at,self.segments[index][3])
        for i in range(self.h['import_count']):
            at=self.h['imports']+i*8;name=self.string(u32(self.source,at),'import_strings','import_strings_size')
            matches=[p.exports[name] for p in providers if p is not self and name in p.exports]
            if len(matches)!=1:raise ValueError('Missing or ambiguous named import: '+name)
            batch(u32(self.source,at+4),matches[0])
        for i in range(self.h['import_module_count']):
            at=self.h['import_modules']+i*20
            name=self.string(u32(self.source,at),'import_strings','import_strings_size')
            matches=[p for p in providers if p is not self and p.name==name]
            if len(matches)!=1:raise ValueError('Missing or ambiguous imported module: '+name)
            provider=matches[0]
            for anonymous in (False,True):
                begin,count=struct.unpack_from('<2I',self.source,at+(12 if anonymous else 4))
                table='anon_imports' if anonymous else 'index_imports';num='anon_import_count' if anonymous else 'index_import_count'
                if not count and not begin:continue
                if begin<self.h[table] or (begin-self.h[table])%8 or not bounded(begin-self.h[table],count*8,self.h[num]*8):raise ValueError('Invalid module import slice')
                for j in range(count):
                    entry=begin+j*8;tag=u32(self.source,entry)
                    if not anonymous:
                        if tag>=provider.h['index_export_count']:raise ValueError('Invalid indexed import')
                        tag=u32(provider.source,provider.h['index_exports']+tag*4)
                    batch(u32(self.source,entry+4),provider.tag(tag)[0])
        if len(used)!=self.h['external_count']:raise ValueError('Unowned external relocation')
        return bytes(self.bytes[self.h['code']:self.h['code']+self.h['code_size']])

def array(name,data):
    rows=['    '+','.join(f'0x{b:02x}' for b in data[i:i+24])+',' for i in range(0,len(data),24)]
    return f'static const uint8_t {name}[]={{\n'+ '\n'.join(rows)+'\n};\n'

def generate(manifest,folder):
    if manifest.get('schema_version')!=1 or not 1<=len(manifest.get('modules',[]))<=8:raise ValueError('Invalid module package manifest')
    modules=[Module(entry,folder) for entry in manifest['modules']]
    if sum(m.static for m in modules)!=1 or len({m.name for m in modules})!=len(modules):raise ValueError('Require one CRS and unique module names')
    generated=['#include "ctr_module.h"\n'];descriptors=[];reports=[]
    for index,m in enumerate(modules):
        symbol=f'ctr_packaged_module_{index}';code=m.relocated_code(modules);blocks=[]
        generated.append(array(symbol+'_file',m.source))
        if not m.static:
            generated.append(array(symbol+'_code',code))
            occupied=set()
            for region_index,r in enumerate(m.entry.get('ranges',[{'offset':0,'size':len(code),'thumb':False}])):
                off,size=r['offset'],r['size'];thumb=r.get('thumb',False);width=2 if thumb else 4
                if not size or off%width or size%width or not bounded(off,size,len(code)):raise ValueError('Invalid module instruction range')
                name=symbol+f'_range_{region_index}';base=m.address+m.h['code']+off
                generated.append(translate(code[off:off+size],base,name,thumb))
                for pc in range(base,base+size,width):
                    if pc in occupied:raise ValueError('Overlapping module instruction ranges')
                    occupied.add(pc);blocks.append((pc,thumb,f'{name}_at_{pc:08x}'))
            if not blocks:raise ValueError('Module has no translated instructions')
            generated.append(f'static const ctr_block {symbol}_blocks[]={{\n'+''.join(f'    {{0x{pc:08x}u,{str(thumb).lower()},{name}}},\n' for pc,thumb,name in sorted(blocks))+'};\n')
        identity=hashlib.sha256(m.source).hexdigest()
        descriptors.append(f'    {{{symbol}_file,sizeof({symbol}_file),0x{m.address:08x}u,0x{m.data:08x}u,0x{m.bss:08x}u,{str(m.static).lower()},'+
            ('NULL,0,NULL,0,' if m.static else f'{symbol}_code,sizeof({symbol}_code),{symbol}_blocks,sizeof({symbol}_blocks)/sizeof(ctr_block),')+f'"{identity}"}},')
        reports.append({'name':m.name,'address':m.address,'static':m.static,'sha256':identity,'translated_instructions':len(blocks)})
    generated.append('const ctr_module_image ctr_module_package[]={\n'+'\n'.join(descriptors)+'\n};\nconst size_t ctr_module_package_count=sizeof(ctr_module_package)/sizeof(ctr_module_package[0]);\n')
    return '\n'.join(generated),{'schema_version':1,'ok':True,'modules':reports,'console_verified':False}

def prepare(manifest_path,output):
    from host.atomic import publish_directory
    import tempfile
    import shutil
    manifest_path=Path(manifest_path);output=Path(output).absolute()
    if output.exists():raise ValueError('Output already exists')
    source,report=generate(json.loads(manifest_path.read_text()),manifest_path.parent)
    output.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.ctr2wup-modules-',dir=output.parent))
    try:
        (stage/'modules.generated.c').write_text(source)
        (stage/'modules.generated.h').write_text('#include "ctr_module.h"\nextern const ctr_module_image ctr_module_package[];\nextern const size_t ctr_module_package_count;\n')
        (stage/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        publish_directory(stage,output)
    finally:
        if stage.exists():shutil.rmtree(stage)
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('manifest',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    try:print(json.dumps(prepare(args.manifest,args.output)))
    except (OSError,ValueError,KeyError,struct.error) as error:parser.exit(1,str(error)+'\n')
