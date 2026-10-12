#!/usr/bin/env python3
"""Compare original texture vectors across C/Python and optional external Zakuro.

External code remains test-only in its pinned checkout; it is never bundled or
linked into CTR2WUP. HILO follows Azahar's RG8 convention, differing from Zakuro.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import shlex
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from renderer.texture import decode_texture,required_bytes
from host import __version__
ROOT=Path(__file__).resolve().parents[1]
PIN='76f17bf76cd9ea238a94f37d389fe701ab05a673'

def vector(format,width,height,seed):
    rng=random.Random(seed);size=required_bytes(format,width,height)
    if format<12:return rng.randbytes(size)
    chunks=[]
    for i in range(size//(8 if format==12 else 16)):
        high=rng.getrandbits(32)
        if i%2:
            high=(high&255)|2
            for shift in (8,16,24):
                base=rng.randrange(4,28);delta=rng.randrange(-4,4)
                high|=(base<<3|(delta&7))<<shift
        else:high&=~2
        chunks.append((rng.randbytes(8) if format==13 else b'')+struct.pack('<II',rng.getrandbits(32),high))
    return b''.join(chunks)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',default='cc')
    p.add_argument('--cc-prefix',default='');p.add_argument('--run-prefix',default='')
    p.add_argument('--static',action='store_true');p.add_argument('--zakuro',type=Path)
    p.add_argument('--rustc',default='rustc');a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    def run(command,**kw):
        return subprocess.run(command,check=True,capture_output=True,**kw).stdout
    binary=out/'decoder'
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'runtime/core')]
    if a.static:flags+=['-static','-mcpu=750']
    run(shlex.split(a.cc_prefix)+[a.cc,*flags,str(ROOT/'runtime/core/ctr_texture.c'),str(ROOT/'tests/textures/decode.c'),'-o',str(binary)])
    external=None;reference={}
    if a.zakuro:
        source=a.zakuro.resolve()
        pin=run(['git','-C',str(source),'rev-parse','HEAD']).decode().strip()
        if pin!=PIN or run(['git','-C',str(source),'status','--porcelain']):raise RuntimeError('Require clean audited Zakuro pin')
        wrapper=out/'reference.rs';external=out/'zakuro-reference'
        wrapper.write_text('#[path='+json.dumps(str(source/'crates/zakuro-gpu/src/format.rs'))+'] mod format;\n'
                           '#[path='+json.dumps(str(source/'crates/zakuro-gpu/src/texture.rs'))+'] mod texture;\n'
                           'use std::io::{Read,Write};\nfn main(){ let a:Vec<String>=std::env::args().collect();'
                           'let f:u32=a[1].parse().unwrap();let w:u32=a[2].parse().unwrap();let h:u32=a[3].parse().unwrap();'
                           'let mut data=Vec::new();std::io::stdin().read_to_end(&mut data).unwrap();'
                           'let mut out=Vec::new();for y in 0..h {for x in 0..w {out.extend_from_slice(&texture::sample_texel(&data,texture::TextureFormat::from_raw(f),w,x,y));}}'
                           'std::io::stdout().write_all(&out).unwrap();}\n')
        run([a.rustc,'--edition=2021','-A','dead_code','-O',str(wrapper),'-o',str(external)])
        reference={'pin':pin,'formats_compared':[n for n in range(14) if n!=6],
                   'hilo_exception':'CTR2WUP uses RG8 [byte1,byte0,0,255] as Azahar; Zakuro uses [0,byte1,byte0,255]',
                   'runtime_linked':False,'external_source_bundled':False}
    rows=[]
    for format in range(14):
        for width,height,seed in ((8,8,1000+format),(16,32,2000+format),(64,16,3000+format)):
            data=vector(format,width,height,seed);expected=decode_texture(data,format,width,height)
            result=run(shlex.split(a.run_prefix)+[str(binary),str(format),str(width),str(height)],input=data)
            if result!=expected:raise RuntimeError('C/Python mismatch at '+str((format,width,height)))
            compared=external is not None and format!=6
            if compared and run([str(external),str(format),str(width),str(height)],input=data)!=expected:
                raise RuntimeError('External reference mismatch at '+str((format,width,height)))
            rows.append({'format':format,'width':width,'height':height,'seed':seed,
                         'source_sha256':hashlib.sha256(data).hexdigest(),'rgba_sha256':hashlib.sha256(result).hexdigest(),
                         'python_match':True,'zakuro_match':True if compared else None})
    report={'ok':True,'version':__version__,'vectors':rows,'reference':reference,'console_verified':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'ok':True,'vectors':len(rows),'external_comparisons':sum(r['zakuro_match'] is True for r in rows)}))
if __name__=='__main__':main()
