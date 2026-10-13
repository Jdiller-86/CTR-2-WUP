#!/usr/bin/env python3
"""Build original applications with pinned external libctru and run checked AOT.

Uses normal devkitARM CRT/newlib/libctru startup with standalone APT and
explicit private virtual filesystem volumes and optional original RomFS assets.
Motion and applets are absent.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host.runtime_sources import runtime_sources
from host.loader import load_image
from host import __version__
from host.build import _validate_rpx
from tests.host_fixtures import ncch
from translator.upstream import generate_into
ROOT=Path(__file__).resolve().parents[1]
PIN='9b55eda44cf80b971503991e0c78f9bc8fe50425'

def check_draw_frames(folder):
    """Independent integer image oracle over the captured displayed surfaces."""
    checks=[]
    for screen,width in (('top',400),('bottom',320)):
        data=(folder/('frame-0-'+screen+'.ppm')).read_bytes()
        header=f'P6\n{width} 240\n255\n'.encode()
        if not data.startswith(header) or len(data)!=len(header)+width*240*3:
            raise RuntimeError('Draw capture has the wrong PPM format or dimensions')
        expected=bytearray()
        for y in range(240):
            for x in range(width):
                vx,vy=239-y,x
                inside=30<=vx<210 and width//8<=vy<width*7//8
                red=(2*vx+1)*width+(2*vy+1)*240<480*width
                expected.extend((255,0,0) if inside and red else (0,255,0) if inside else (16,32,49))
        if data[len(header):]!=expected:
            raise RuntimeError('Captured draw pixels differ from the independent analytic image')
        checks.append({'screen':screen,'width':width,'height':240,'pixels':width*240,
                       'exact_match':True,'sha256':hashlib.sha256(data).hexdigest()})
    return checks

def container(elf):
    data=elf.read_bytes()
    if data[:7]!=b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH',data,16)!=(2,40):raise ValueError('Require the original ARM32 little-endian executable')
    entry,phoff=struct.unpack_from('<II',data,24);width,count=struct.unpack_from('<HH',data,42)
    if width!=32 or count!=3 or entry!=0x100000:raise ValueError('Unexpected original devkitARM layout')
    segments=[];code=b''
    for i,expected in enumerate((5,4,6)):
        kind,offset,address,_,filesz,memsz,flags,alignment=struct.unpack_from('<8I',data,phoff+i*width)
        if kind!=1 or flags!=expected or alignment!=4096 or filesz>memsz or offset+filesz>len(data):raise ValueError('Unexpected original segment')
        pages=(filesz+4095)//4096;segments.append((address,pages,filesz,memsz-filesz));code+=data[offset:offset+filesz].ljust(pages*4096,b'\0')
    rom=ncch([('.code',code)],program_id=0x000400000ff00f0c)
    for at,segment in zip((0x210,0x220,0x230),segments):struct.pack_into('<III',rom,at,*segment[:3])
    struct.pack_into('<I',rom,0x23c,segments[-1][3]);rom[0x160:0x180]=hashlib.sha256(rom[0x200:0x600]).digest()
    return rom

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--libctru-source',type=Path,default=ROOT.parent/'ctr2wup-deps/libctru')
    p.add_argument('--devkitarm',type=Path,default=Path(os.environ.get('DEVKITARM',str(Path(os.environ.get('DEVKITPRO','/workspace/ctr2wup-sdk'))/'devkitARM'))))
    p.add_argument('--cc',default='gcc');p.add_argument('--cc-prefix',default='');p.add_argument('--run-prefix',default='');p.add_argument('--static',action='store_true');p.add_argument('--sanitize',action='store_true')
    p.add_argument('--application',choices=('boot','threaded','graphics','input','interactive','filesystem','romfs','transfer','draw'),help='Run only the selected original application')
    p.add_argument('--wiiu',action='store_true',help='Also build original interactive/filesystem applications as RPX/WUHB')
    p.add_argument('--wiiu-guarded',action='store_true',help='Also verify/build an interactive AOT comparison with call depth limited to 2')
    p.add_argument('--romfs-file',action='store_true',help='Build the RomFS console diagnostic using a verified external asset blob')
    p.add_argument('--live-input',action='store_true',help='Replay held/released A with varied host clocks and console-sized budgets')
    p.add_argument('--trace-startup',action='store_true',help='Capture every IPC at single-instruction boundaries for the original boot application')
    a=p.parse_args()
    if a.trace_startup and a.application!='boot':p.error('--trace-startup requires --application boot')
    if a.wiiu_guarded and (not a.wiiu or a.application!='interactive'):p.error('--wiiu-guarded requires --wiiu --application interactive')
    if a.romfs_file and (not a.wiiu or a.application!='romfs'):p.error('--romfs-file requires --wiiu --application romfs')
    if a.live_input and a.application!='interactive':p.error('--live-input requires --application interactive')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);checks=[]
    def run(name,command,**kw):
        r=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',**kw)
        (out/(name+'.log')).write_text(r.stdout)
        checks.append({'name':name,'exit_code':r.returncode})
        if r.returncode:raise RuntimeError(name+' failed; see its preserved log')
        return r.stdout
    source_commit=run('source-commit',['git','-C',str(ROOT),'rev-parse','HEAD']).strip()
    source_dirty=bool(run('source-state',['git','-C',str(ROOT),'status','--porcelain']))
    pin=run('dependency-pin',['git','-C',str(a.libctru_source),'rev-parse','HEAD']).strip()
    dirty=run('dependency-clean',['git','-C',str(a.libctru_source),'status','--porcelain'])
    if pin!=PIN or dirty:raise RuntimeError('Require the clean audited libctru pin')
    arm=a.devkitarm/'bin/arm-none-eabi-gcc';env=dict(os.environ,DEVKITARM=str(a.devkitarm),DEVKITPRO=str(a.devkitarm.parent))
    arm_version=run('arm-compiler',[str(arm),'--version'])
    with tempfile.TemporaryDirectory(prefix='ctr2wup-libctru-') as temp:
        library=Path(temp)/'libctru';shutil.copytree(a.libctru_source/'libctru',library)
        run('libctru-build',['make','-C',str(library),'lib/libctru.a','-j','4'],env=env)
        library_hash=hashlib.sha256((library/'lib/libctru.a').read_bytes()).hexdigest()
        reports=[]
        for name in (a.application,) if a.application else ('boot','threaded','graphics','input','interactive','filesystem','romfs','transfer','draw'):
            folder=out/name;folder.mkdir();elf=folder/'application.elf'
            run(name+'-arm-compile',[str(arm),'-march=armv6k','-mtune=mpcore','-mfloat-abi=hard','-mfpu=vfp','-mtp=soft','-O2','-g','-ffunction-sections','-fdata-sections','-D__3DS__','-I'+str(library/'include'),'-L'+str(library/'lib'),'-specs=3dsx.specs',str(ROOT/'tests/homebrew'/(name+'.c')),'-lctru','-lm','-o',str(elf)])
            rom=folder/'original.cxi';rom.write_bytes(container(elf));image=load_image(rom);generation=generate_into(rom,folder/'aot')
            blob=','.join(str(b) for b in image['code']);segments=','.join('{'+','.join(str(s[k]) for k in ('address','pages','size'))+'}' for s in image['segments'].values())
            (folder/'boot-image.h').write_text('#include "ctr_guest.h"\nstatic const uint8_t boot_bytes[]={'+blob+'};\nstatic const ctr_guest_image boot_image={boot_bytes,sizeof(boot_bytes),'+segments+','+str(image['bss_size'])+',16384,"'+hashlib.sha256(image['code']).hexdigest()+'"};\n')
            if name=='romfs':run('romfs-fixture',[sys.executable,str(ROOT/'tools/generate-romfs-fixture.py'),'--output',str(folder)])
            core=ROOT/'runtime/core';binary=folder/'runner';flags=['-std=c11','-O2','-g','-Wall','-Wextra','-Werror','-ffp-contract=off','-fno-fast-math','-I'+str(core),'-I'+str(ROOT/'runtime/recomp'),'-I'+str(folder),'-I'+str(folder/'aot')]
            if a.static:flags+=['-static','-mcpu=750']
            if a.sanitize:flags+=['-O1','-fno-omit-frame-pointer','-fsanitize=address,undefined']
            if name=='romfs':flags+=['-DCTR_ROMFS_HOMEBREW']
            if name=='draw':flags+=['-DCTR_DRAW_HOMEBREW']
            sources=runtime_sources(ROOT) + [core / n for n in ('ctr_fs_snapshot.c', 'ctr_sha256.c', 'ctr_romfs_file.c')]+[ROOT/'runtime/recomp/ctr_recomp.c',ROOT/'tests/homebrew/runner.c']+[folder/'aot'/n for n in generation['sources']]
            run(name+'-host-compile',shlex.split(a.cc_prefix)+[a.cc,*flags,*map(str,sources),'-lm','-o',str(binary)])
            extra=['--trace-ipc'] if a.trace_startup else [str(folder)] if name in ('graphics','interactive','transfer','draw') else ['--filesystem',str(folder/'save.snapshot')] if name=='filesystem' else []
            output=run(name+'-execute',shlex.split(a.run_prefix)+[str(binary)]+extra)
            result=json.loads(output.splitlines()[-1]);expected='CTR2WUP compiled libctru boot PASS' if name=='boot' else 'CTR2WUP compiled thread/TLS/LightLock PASS' if name=='threaded' else 'CTR2WUP compiled graphics PASS' if name=='graphics' else 'CTR2WUP compiled input PASS' if name=='input' else 'CTR2WUP compiled filesystem SAVE 1 PASS' if name=='filesystem' else 'CTR2WUP compiled RomFS PASS' if name=='romfs' else 'CTR2WUP compiled GX transfers PASS' if name=='transfer' else 'CTR2WUP compiled PICA draws PASS' if name=='draw' else 'CTR2WUP compiled interactive PASS'
            if result['state']!='exited' or result['fallback'] or expected not in result['messages']:raise RuntimeError(name+' did not complete its real guest assertions')
            if name in ('graphics','interactive','transfer') and result['frames']!=[0x00ff00ff,0xff0000ff,0x00ff00ff]:raise RuntimeError('Require all three exact two-screen RGB565 frames')
            if name=='transfer' and result['gx']!={'commands':17,'dma':3,'fills':8,'transfers':3,'copies':1,'cache':1,'pica':1}:raise RuntimeError('Original application did not execute all expected GX operations')
            if name=='transfer' and result['pica_state']!={'lists':1,'writes':11,'finalizers':1,'viewport_width':0x12bb56dd}:raise RuntimeError('Original GPUCMD configuration state did not match masked/grouped writes')
            if name=='draw' and (result['frames']!=[0x102031ff] or result['pica_render']!={'draws':4,'triangles':4,'fragments':97200,'shader_instructions':36}
                                or result['gx']!={'commands':6,'dma':0,'fills':2,'transfers':2,'copies':0,'cache':0,'pica':2}):
                raise RuntimeError('Original program did not execute and present all expected PICA draws')
            if name=='input' and result['input_samples']!=3:raise RuntimeError('Require all three standard HID publications')
            if name=='interactive' and result['input_samples']!=4:raise RuntimeError('Require three input-driven frames and the guest B-button exit')
            if name=='filesystem':
                reloaded=json.loads(run(name+'-reload',shlex.split(a.run_prefix)+[str(binary),*extra]).splitlines()[-1])
                if reloaded['state']!='exited' or reloaded['fallback'] or 'CTR2WUP compiled filesystem RELOAD PASS' not in reloaded['messages'] or reloaded['save_commits']!=1:raise RuntimeError('Fresh process failed standard savedata reload')
            frames=[{'name':f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in sorted(folder.glob('frame-*.ppm'))]
            ipc=[]
            for line in output.splitlines():
                if line.startswith('ipc '):
                    fields=dict(item.split('=',1) for item in line.split()[1:]);ipc.append({k:int(v,16) if k in ('header','result','pc') else int(v) if k=='request' else v for k,v in fields.items()})
            if a.trace_startup and sum(bool(event['result']) for event in ipc)!=result['ipc_failures']:raise RuntimeError('Incomplete startup failure trace')
            reports.append({'application':name,'guest':result,'frames':frames,'startup_ipc':ipc,'generation':generation,'allocated_bytes':image['allocated_bytes'],'executable_sha256':hashlib.sha256(image['code']).hexdigest()})
            if name=='draw':reports[-1]['independent_frame_checks']=check_draw_frames(folder)
            if name=='romfs':
                blob=folder/'romfs-level3.bin'
                file_result=json.loads(run('romfs-file-execute',shlex.split(a.run_prefix)+[str(binary),'--romfs-file',str(blob)]).splitlines()[-1])
                if file_result!=result:raise RuntimeError('File-backed RomFS differs from the original memory-backed guest')
                reports[-1]['file_backed_guest']=file_result
                corrupt=folder/'corrupt-romfs.bin';damaged=bytearray(blob.read_bytes());damaged[-1]^=1;corrupt.write_bytes(damaged)
                rejected=subprocess.run(shlex.split(a.run_prefix)+[str(binary),'--romfs-file',str(corrupt)],capture_output=True,text=True)
                (out/'romfs-file-corrupt-reject.log').write_text(rejected.stdout+rejected.stderr)
                checks.append({'name':'romfs-file-corrupt-reject','exit_code':rejected.returncode,'expected':9})
                if rejected.returncode!=9 or corrupt.read_bytes()!=damaged:raise RuntimeError('Corrupt asset blob was not rejected/preserved')
            if a.wiiu_guarded:
                guarded=folder/'runner-guarded';frames_folder=folder/'guarded-frames';frames_folder.mkdir()
                run('interactive-guarded-host-compile',shlex.split(a.cc_prefix)+[a.cc,*flags,'-DCTR_RECOMP_CALL_LIMIT=2',*map(str,sources),'-lm','-o',str(guarded)])
                guarded_result=json.loads(run('interactive-guarded-execute',shlex.split(a.run_prefix)+[str(guarded),str(frames_folder)]).splitlines()[-1])
                if guarded_result['state']!='exited' or guarded_result['fallback'] or expected not in guarded_result['messages'] or guarded_result['frames']!=result['frames'] or guarded_result['input_samples']!=4 or guarded_result['aot_max_depth']>2 or not guarded_result['aot_unwinds']:
                    raise RuntimeError('Guarded AOT failed original guest/frame/input/depth checks')
                if guarded_result['instructions']!=result['instructions']:raise RuntimeError('Guarded AOT changed guest instruction execution')
                for frame in frames:
                    if hashlib.sha256((frames_folder/frame['name']).read_bytes()).hexdigest()!=frame['sha256']:raise RuntimeError('Guarded AOT pixels differ')
                reports[-1]['guarded']={'call_limit':2,'guest':guarded_result,'exact_frames_match':True}
            if a.live_input:
                live_sources=[s for s in sources if s!=ROOT/'tests/homebrew/runner.c']+[ROOT/'tests/homebrew/live_replay.c']
                live_results=[]
                for limit in (64,2) if a.wiiu_guarded else (64,):
                    live=folder/('runner-live-'+str(limit))
                    run('live-'+str(limit)+'-compile',shlex.split(a.cc_prefix)+[a.cc,*flags,'-DCTR_RECOMP_CALL_LIMIT='+str(limit),*map(str,live_sources),'-lm','-o',str(live)])
                    for delta,budget,delay in ((1000000,10000,0),(16666667,50000,2),(285000000,50000,0),(1000000000,50000,5)):
                        row=json.loads(run('live-'+str(limit)+'-'+str(delta),shlex.split(a.run_prefix)+[str(live),str(delta),str(budget),str(delay)]).splitlines()[-1])
                        if not row['ok'] or row['transitions']!=4 or row['partial_samples'] or row['depth']>limit:raise RuntimeError('Live input timing replay failed')
                        live_results.append(dict(row,call_limit=limit))
                reports[-1]['live_input_replays']=live_results
            if name=='filesystem':
                reports[-1]['reload']=reloaded
                snapshot=(folder/'save.snapshot').read_bytes()
                if snapshot[:8]!=b'CTRFS001' or struct.unpack_from('<4I',snapshot,8)!=(4,2,len(snapshot),(zlib.crc32(snapshot[24:])^zlib.crc32(snapshot[:20]))):raise RuntimeError('Wrong snapshot framing/checksum')
                cursor=24;saved={}
                for _ in range(2):
                    length,size=struct.unpack_from('<2I',snapshot,cursor);cursor+=8
                    path=snapshot[cursor:cursor+length].decode('utf-8');cursor+=length
                    saved[path]=snapshot[cursor:cursor+size];cursor+=size
                if cursor!=len(snapshot) or saved!={'/progress.dat':b'CTR2SAVE\x02\0\0\0','/記録.dat':b'CTR2SAVE\x02\0\0\0'}:raise RuntimeError('Wrong independently preserved save files')
                corrupt=folder/'corrupt.snapshot';bad=bytearray(snapshot);bad[-1]^=1;corrupt.write_bytes(bad)
                rejected=subprocess.run(shlex.split(a.run_prefix)+[str(binary),'--filesystem',str(corrupt)],capture_output=True,text=True)
                checks.append({'name':'filesystem-corrupt-reject','exit_code':rejected.returncode,'expected':5})
                if rejected.returncode!=5 or corrupt.read_bytes()!=bad:raise RuntimeError('Corrupt save was not preserved/rejected')
                reports[-1]['snapshot_sha256']=hashlib.sha256(snapshot).hexdigest()
            if a.wiiu and name in ('interactive','filesystem','romfs','transfer','draw'):
                wiiu=folder/'wiiu';wiiu.mkdir();recipe=ROOT/'runtime/wiiu'
                # GNU Make tokenizes source/prerequisite lists. Keep its relative
                # names free of user-path whitespace, as the main build engine
                # does, and publish only validated artifacts to the chosen path.
                (ROOT/'artifacts').mkdir(exist_ok=True)
                with tempfile.TemporaryDirectory(prefix='compiled-console-',dir=ROOT/'artifacts') as temporary:
                    work=Path(temporary);shutil.copytree(folder/'aot',work/'aot');shutil.copy2(folder/'boot-image.h',work/'boot-image.h')
                    if name=='romfs':shutil.copy2(folder/'romfs-image.h',work/'romfs-image.h')
                    prefix=os.path.relpath(work,recipe)
                    target='ctr2wup-libctru-'+name+('-file' if a.romfs_file else '')
                    run(name+'-wiiu-build',['make','-C',str(recipe),'OUTPUT='+prefix+'/wiiu','TARGET='+target,'FILESYSTEM_HOMEBREW='+('1' if name=='filesystem' else '0'),'ROMFS_HOMEBREW='+('1' if name=='romfs' else '0'),'ROMFS_FILE='+str(int(a.romfs_file)),'GX_HOMEBREW='+('1' if name=='transfer' else '0'),'PICA_HOMEBREW='+('1' if name=='draw' else '0'),'GUEST_DEFAULT=2','EXTERNAL_AOT=1','COMPILED_HOMEBREW=1','COMPILED_IMAGE_DIR='+prefix,'UPSTREAM_DIR='+prefix+'/aot'],env=env)
                    built=work/'wiiu';rpx=built/(target+'.rpx');shortcut=rpx.with_suffix('.wuhb')
                    _validate_rpx(rpx)
                    if shortcut.read_bytes()[:4]!=b'WUHB':raise RuntimeError('Invalid compiled WUHB')
                    shutil.copy2(rpx,wiiu/rpx.name);shutil.copy2(shortcut,wiiu/shortcut.name)
                reports[-1]['wiiu_artifacts']=[{'name':f.name,'size':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in sorted(wiiu.glob('*.rp[x]*'))+sorted(wiiu.glob('*.wuhb'))]
                if a.wiiu_guarded:
                    guarded_folder=folder/'wiiu-guarded';guarded_folder.mkdir()
                    with tempfile.TemporaryDirectory(prefix='guarded-console-',dir=ROOT/'artifacts') as temporary:
                        work=Path(temporary);shutil.copytree(folder/'aot',work/'aot');shutil.copy2(folder/'boot-image.h',work/'boot-image.h')
                        prefix=os.path.relpath(work,recipe)
                        run('interactive-guarded-wiiu-build',['make','-C',str(recipe),'OUTPUT='+prefix+'/wiiu','TARGET=ctr2wup-libctru-guarded','GUEST_DEFAULT=2','EXTERNAL_AOT=1','COMPILED_HOMEBREW=1','CALL_LIMIT=2','COMPILED_IMAGE_DIR='+prefix,'UPSTREAM_DIR='+prefix+'/aot'],env=env)
                        rpx=work/'wiiu/ctr2wup-libctru-guarded.rpx';shortcut=rpx.with_suffix('.wuhb')
                        _validate_rpx(rpx)
                        if shortcut.read_bytes()[:4]!=b'WUHB':raise RuntimeError('Invalid guarded WUHB')
                        shutil.copy2(rpx,guarded_folder/rpx.name);shutil.copy2(shortcut,guarded_folder/shortcut.name)
                    reports[-1]['guarded_wiiu_artifacts']=[{'name':f.name,'size':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in sorted(guarded_folder.iterdir())]
        if a.wiiu and not any('wiiu_artifacts' in r for r in reports):raise ValueError('--wiiu requires interactive, filesystem, romfs, transfer or draw')
    if source_commit!=run('source-commit-after',['git','-C',str(ROOT),'rev-parse','HEAD']).strip():
        raise RuntimeError('Source commit changed during validation; retain logs and run again')
    source_dirty=source_dirty or bool(run('source-state-after',['git','-C',str(ROOT),'status','--porcelain']))
    report={'ok':True,'version':__version__,'source_commit':source_commit,'source_dirty':source_dirty,'libctru_pin':pin,'library_sha256':library_hash,'arm_compiler':arm_version.splitlines()[0],'applications':reports,'checks':checks,'console_verified':False,'native_windows_verified':False,'complete_service_startup':False,'standalone_apt_and_fs_startup':True,'unavailable_startup_services':['APT:S','APT:A','ptm:sysm'],'powerpc_execution':'emulated Linux' if a.static else None}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'ok':True,'applications':[{'name':r['application'],'instructions':r['guest']['instructions'],'ipc_failures':r['guest']['ipc_failures']} for r in reports],'report':str(out/'report.json')}));return 0
if __name__=='__main__':raise SystemExit(main())
