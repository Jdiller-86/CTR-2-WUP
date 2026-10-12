#!/usr/bin/env python3
"""Original unscaled image vectors: C/Python and optional test-only Zakuro colors.

External format code is compiled only in its clean audited reference checkout;
its full display-transfer engine is not linked or copied into the project.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import shlex
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from renderer.transfer import convert
from host import __version__
ROOT=Path(__file__).resolve().parents[1]
PIN='76f17bf76cd9ea238a94f37d389fe701ab05a673'

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cc',default='cc');p.add_argument('--cc-prefix',default='');p.add_argument('--run-prefix',default='')
    p.add_argument('--static',action='store_true');p.add_argument('--zakuro',type=Path);p.add_argument('--rustc',default='rustc');a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    def run(command,**kw):return subprocess.run(command,check=True,capture_output=True,**kw).stdout
    binary=out/'transfer';flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'runtime/core')]
    if a.static:flags+=['-static','-mcpu=750']
    run(shlex.split(a.cc_prefix)+[a.cc,*flags,str(ROOT/'runtime/core/ctr_transfer.c'),str(ROOT/'tests/gx/transfer.c'),'-o',str(binary)])
    external=None
    if a.zakuro:
        source=a.zakuro.resolve();pin=run(['git','-C',str(source),'rev-parse','HEAD']).decode().strip()
        if pin!=PIN or run(['git','-C',str(source),'status','--porcelain']):raise RuntimeError('Require clean audited Zakuro pin')
        wrapper=out/'reference.rs';external=out/'color-reference'
        wrapper.write_text('#[path='+json.dumps(str(source/'crates/zakuro-gpu/src/format.rs'))+'] mod format;\n'
            'use std::io::{Read,Write};\nfn main(){let a:Vec<String>=std::env::args().collect();let dim:u32=a[1].parse().unwrap();'
            'let flags:u32=a[2].parse().unwrap();let (w,h)=(dim&65535,dim>>16);let mut data=Vec::new();std::io::stdin().read_to_end(&mut data).unwrap();'
            'let inf=format::ColorFormat::from_raw(flags>>8);let outf=format::ColorFormat::from_raw(flags>>12);let ib=inf.bytes_per_pixel();let ob=outf.bytes_per_pixel();'
            'let mut out=vec![0u8;(w*h) as usize*ob];for y in 0..h {for x in 0..w {'
            'let src=if flags&2!=0 {(y*w+x) as usize*ib} else {format::morton_offset(x,y,w,ib as u32) as usize};'
            'let dy=if flags&1!=0 {h-1-y} else {y};let tiled=(flags&2!=0)!=(flags&32!=0);'
            'let dst=if tiled {format::morton_offset(x,dy,w,ob as u32) as usize} else {(dy*w+x) as usize*ob};'
            'outf.encode(inf.decode(&data[src..src+ib]),&mut out[dst..dst+ob]);}} std::io::stdout().write_all(&out).unwrap();}\n')
        run([a.rustc,'--edition=2021','-A','dead_code','-O',str(wrapper),'-o',str(external)])
    rows=[]
    for shape in ((8,8),(16,8)):
        w,h=shape;dim=w|(h<<16)
        for inf in range(5):
            data=random.Random(w*1000+inf).randbytes(w*h*(4 if inf==0 else 3 if inf==1 else 2))
            for outf in range(5):
                for mode in (0,2,32,34):
                    for flip in (0,1):
                        flags=(inf<<8)|(outf<<12)|mode|flip;args=[str(dim),str(flags)]
                        expected=convert(data,dim,dim,flags)
                        actual=run(shlex.split(a.run_prefix)+[str(binary),*args],input=data)
                        if actual!=expected:raise RuntimeError('C/Python mismatch '+str((shape,inf,outf,mode,flip)))
                        if external and run([str(external),*args],input=data)!=expected:raise RuntimeError('External color/layout mismatch')
                        rows.append({'width':w,'height':h,'flags':flags,'source_sha256':hashlib.sha256(data).hexdigest(),
                                     'output_sha256':hashlib.sha256(actual).hexdigest(),'external_color_layout_match':True if external else None})
    report={'ok':True,'version':__version__,'vectors':rows,'external_color_layout_pin':PIN if external else None,
            'external_full_transfer_engine_executed':False,'console_verified':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'ok':True,'vectors':len(rows),'external_color_layout_match':bool(external)}))
if __name__=='__main__':main()
