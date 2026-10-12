#!/usr/bin/env python3
"""Build an original Level-3 RomFS view and an unsigned canonical IVFC wrapper."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

NONE=0xffffffff

def align(n,a):
    return (n+a-1)//a*a

def bucket(parent,name):
    value=parent^123456789
    encoded=name.encode('utf-16le')
    for (unit,) in struct.iter_unpack('<H',encoded):
        value=((value>>5)|(value<<27))&0xffffffff
        value^=unit
    return value%3

def image():
    dirs=[{'offset':0,'name':'','parent':0,'child_dir':24,'child_file':0},
          {'offset':24,'name':'levels','parent':0,'child_dir':NONE,'child_file':52}]
    files=[{'offset':0,'name':'readme.txt','parent':0,'sibling':NONE,'bytes':b'CTR2WUP assets\n'},
           {'offset':52,'name':'config.bin','parent':24,'sibling':104,'bytes':bytes(range(16))},
           {'offset':104,'name':'設定.txt','parent':24,'sibling':NONE,'bytes':b'read-only fixture\n'}]
    directory=bytearray(60);file_table=bytearray(148);payload=bytearray()
    dir_hash=[NONE]*3;file_hash=[NONE]*3
    for d in dirs:
        name=d['name'].encode('utf-16le');h=bucket(d['parent'],d['name']);old=dir_hash[h];dir_hash[h]=d['offset']
        entry=struct.pack('<6I',d['parent'],NONE,d['child_dir'],d['child_file'],old,len(name))+name
        directory[d['offset']:d['offset']+len(entry)]=entry
    for f in files:
        payload.extend(bytes(align(len(payload),16)-len(payload)));offset=len(payload);payload.extend(f['bytes'])
        name=f['name'].encode('utf-16le');h=bucket(f['parent'],f['name']);old=file_hash[h];file_hash[h]=f['offset']
        entry=struct.pack('<2I2Q2I',f['parent'],f['sibling'],offset,len(f['bytes']),old,len(name))+name
        file_table[f['offset']:f['offset']+len(entry)]=entry
    header=struct.pack('<10I',40,40,12,52,len(directory),112,12,124,len(file_table),272)
    return header+struct.pack('<3I',*dir_hash)+directory+struct.pack('<3I',*file_hash)+file_table+payload

def ivfc_image(level3, exponent=12):
    """Original standard three-level IVFC fixture; no signed authenticity."""
    block=1<<exponent
    def hashes(data):
        return b''.join(hashlib.sha256(data[i:i+block].ljust(block,b'\0')).digest()
                        for i in range(0,len(data),block))
    level2=hashes(level3);level1=hashes(level2);masters=hashes(level1)
    header=bytearray(0x60);header[:8]=b'IVFC\0\0\x01\0'
    struct.pack_into('<I',header,8,len(masters));logical=0
    for i,data in enumerate((level1,level2,level3)):
        struct.pack_into('<QQII',header,12+24*i,logical,len(data),exponent,0)
        logical+=align(len(data),block)
    struct.pack_into('<I',header,0x54,0x5c)
    output=header+masters;output.extend(bytes(align(len(output),block)-len(output)))
    for data in (level3,level1,level2):output.extend(data.ljust(align(len(data),block),b'\0'))
    return bytes(output)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);data=image()
    (a.output/'romfs-level3.bin').write_bytes(data)
    (a.output/'romfs-image.h').write_text('#include <stdint.h>\nstatic const uint8_t romfs_bytes[]={'+','.join(map(str,data))+'};\n'+
        'static const uint8_t romfs_sha256[]={'+','.join(map(str,hashlib.sha256(data).digest()))+'};\n')
    wrapped=ivfc_image(data);(a.output/'romfs-ivfc.bin').write_bytes(wrapped)
    report={'original_fixture':True,'service_view':'decrypted Level-3 only','bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'IVFC_authenticated':False,'commercial_assets':False}
    (a.output/'romfs-fixture.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
