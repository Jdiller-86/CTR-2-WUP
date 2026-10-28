#!/usr/bin/env python3
"""Independently check original ARM DSP output and exact rational 48k playback."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))


def reference():
    pcm=[]
    for i in range(48000):
        word=8192-(i%16384)//2
        pcm.extend(((6000 if not word&32 else -6000) if i<16000 or i>=32000 else 0,
                    (6000 if not word&16 else -6000) if i>=16000 else 0))
    output=[]
    for i in range((48000*24576000+16756990)//16756991):
        at,fraction=divmod(i*16756991,24576000);nxt=min(at+1,47999)
        for lane in range(2):
            value=pcm[at*2+lane]*(24576000-fraction)+pcm[nxt*2+lane]*fraction
            output.append((value+12288000)//24576000)
    return struct.pack('<'+'h'*len(output),*output)


def validate(output,cc='gcc',sanitize=False):
    output=output.absolute();output.mkdir(parents=True,exist_ok=False)
    sources=(ROOT/'runtime/core/sources.list').read_text().splitlines()
    sources=[str(ROOT/'runtime/core'/s) for s in sources if s and not s.startswith('#')]
    sources += [str(ROOT/p) for p in ('runtime/core/ctr_dsp_fixture.c','tests/fixtures/dsp/dsp.generated.c',
                                    'tests/fixtures/dsp/dsp-image.generated.c','tests/guest-dsp/test_dsp.c')]
    expected=reference();modes=['normal','sanitized'] if sanitize else ['normal']
    results=[]
    for mode in modes:
        binary=output/('probe-'+mode+('.exe' if sys.platform=='win32' else ''))
        command=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-I'+str(ROOT/'runtime/core')]
        if mode=='sanitized': command+=['-fno-omit-frame-pointer','-fsanitize=address,undefined']
        subprocess.run(command+sources+['-o',str(binary)],check=True,cwd=ROOT)
        pcm=output/('pcm-'+mode+'.bin')
        run=subprocess.run([str(binary),'--pcm'],check=True,capture_output=True)
        row=json.loads(run.stderr)
        pcm.write_bytes(run.stdout)
        if row.get('ok') is not True or run.stdout!=expected: raise ValueError('Guest DSP reference mismatch')
        results.append(row)
    with wave.open(str(output/'reference.wav'),'wb') as stream:
        stream.setparams((2,2,48000,len(expected)//4,'NONE','not compressed'));stream.writeframes(expected)
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True))
    report={'schema':1,'kind':'ctr2wup-guest-dsp-reference','ok':True,'source_commit':commit,'source_dirty':dirty,
            'modes':modes,'independent_reference':True,'original_inputs_only':True,'arm_interpreter_aot_exact':True,
            'dsp_blocks':300,'dsp_samples':48000,'output_frames':len(expected)//4,'output_rate':48000,
            'pcm_sha256':hashlib.sha256(expected).hexdigest(),'arm_instructions':results[0]['arm_instructions'],
            'console_verified':False,'finite_prerender':True}
    (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cc',default='gcc');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    print(json.dumps(validate(a.output,a.cc,a.sanitize)))
