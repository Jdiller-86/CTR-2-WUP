#!/usr/bin/env python3
"""Bundle verified original guided-test builds and source, excluding private data."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from host import __version__
from host.build import _validate_rpx

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write_zip(path,entries):
    with zipfile.ZipFile(path,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for source,name in sorted(entries,key=lambda pair:pair[1]):
            z.write(source,name)
    with zipfile.ZipFile(path) as z:
        if z.testzip() is not None:raise RuntimeError('Damaged archive: '+str(path))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--interactive-report',type=Path,required=True)
    parser.add_argument('--filesystem-report',type=Path,required=True)
    parser.add_argument('--include-guarded',action='store_true')
    parser.add_argument('--romfs-report',type=Path,help='Include the verified original read-only asset diagnostic')
    parser.add_argument('--output',type=Path,required=True)
    a=parser.parse_args();out=a.output.resolve()
    native=a.native.resolve();report=json.loads((native/'build-report.json').read_text())
    if not report['ok'] or report['runtime_version']!=__version__ or report['profile']!='native-diagnostics':
        raise RuntimeError('Require a verified current native diagnostic')
    for entry in report['artifacts']:
        if digest(native/entry['name'])!=entry['sha256']:raise RuntimeError('Native artifact mismatch')
    inputs=[(native/'ctr2wup-prototype.rpx','rpx/ctr2wup-prototype.rpx'),
            (native/'ctr2wup-prototype.wuhb','sd-card/wiiu/apps/ctr2wup-prototype.wuhb')]
    reports=[(native/'build-report.json','build-reports/native.json')]
    for name,path in [('interactive',a.interactive_report),('filesystem',a.filesystem_report)]:
        path=path.resolve();r=json.loads(path.read_text())
        if not r['ok'] or r['version']!=__version__:raise RuntimeError('Require verified current '+name)
        app=next(x for x in r['applications'] if x['application']==name)
        folder=path.parent/name/'wiiu'
        for entry in app['wiiu_artifacts']:
            f=folder/entry['name']
            if digest(f)!=entry['sha256']:raise RuntimeError('Artifact does not match its builder receipt')
            if f.suffix=='.rpx':inputs.append((f,'rpx/'+f.name))
            elif f.suffix=='.wuhb':inputs.append((f,'sd-card/wiiu/apps/'+f.name))
        reports.append((path,'build-reports/'+name+'.json'))
        if name=='interactive' and a.include_guarded:
            if not app.get('guarded',{}).get('exact_frames_match') or app['guarded']['guest']['state']!='exited':raise RuntimeError('Require verified guarded AOT comparison')
            for entry in app['guarded_wiiu_artifacts']:
                f=path.parent/'interactive/wiiu-guarded'/entry['name']
                if digest(f)!=entry['sha256']:raise RuntimeError('Guarded artifact mismatch')
                inputs.append((f,('rpx/' if f.suffix=='.rpx' else 'sd-card/wiiu/apps/')+f.name))
    if len(inputs)!=(8 if a.include_guarded else 6):raise RuntimeError('Require every requested RPX/WUHB pair')
    if a.romfs_report:
        path=a.romfs_report.resolve();r=json.loads(path.read_text())
        if not r['ok'] or r['version']!=__version__:raise RuntimeError('Require verified current RomFS')
        app=next(x for x in r['applications'] if x['application']=='romfs')
        if app['guest']['state']!='exited' or app['guest']['fallback'] or 'CTR2WUP compiled RomFS PASS' not in app['guest']['messages']:raise RuntimeError('Require the original RomFS guest assertions')
        folder=path.parent/'romfs/wiiu'
        for entry in app['wiiu_artifacts']:
            f=folder/entry['name']
            if digest(f)!=entry['sha256']:raise RuntimeError('RomFS artifact mismatch')
            inputs.append((f,('rpx/' if f.suffix=='.rpx' else 'sd-card/wiiu/apps/')+f.name))
        reports.append((path,'build-reports/romfs.json'))
        if len(inputs)!=(10 if a.include_guarded else 8):raise RuntimeError('Require the RomFS RPX/WUHB pair')
    for f,_ in inputs:
        if f.suffix=='.rpx':_validate_rpx(f)
        elif f.read_bytes()[:4]!=b'WUHB':raise RuntimeError('Invalid WUHB')
    source_folders=('docs','host','translator','runtime','renderer','packaging','profiles','tests','tools')
    source_files=('.gitignore','AGENTS.md','LICENSE','Makefile','README.md','STATUS.md','THIRD_PARTY_NOTICES','pyproject.toml')
    forbidden={'.3ds','.cxi','.cia','.3dsx','.sav','.keys','.key','.cert','.app','.h3','.tik','.tmd','.pyc','.log','.rpx','.wuhb','.elf','.o','.map'}
    sources=[(ROOT/n,'CTR2WUP/'+n) for n in source_files]
    for folder in source_folders:
        for f in sorted((ROOT/folder).rglob('*')):
            if not f.is_file() or any(p in f.parts for p in ('__pycache__','obj','bin','build')):continue
            if f.is_symlink() or f.suffix.lower() in forbidden:raise RuntimeError('Refusing generated/private source input: '+str(f))
            sources.append((f,'CTR2WUP/'+f.relative_to(ROOT).as_posix()))
    out.mkdir(parents=True,exist_ok=False)
    source=out/('CTR2WUP-source-'+__version__+'.zip');write_zip(source,sources)
    entries=inputs+reports+[(source,'source-and-notices/'+source.name),
        (ROOT/'docs/guided-testing.md','START-HERE.md'),
        (ROOT/'docs/console-retest.md','console-retest.md'),(ROOT/'LICENSE','LICENSE'),
        (ROOT/'THIRD_PARTY_NOTICES','THIRD_PARTY_NOTICES')]
    kit=out/('CTR2WUP-console-testing-kit-'+__version__+'.zip');write_zip(kit,entries)
    with zipfile.ZipFile(kit) as z:
        for f,name in entries:
            if hashlib.sha256(z.read(name)).hexdigest()!=digest(f):raise RuntimeError('Wrong bundled bytes')
    source_digest=hashlib.sha256()
    for f,name in sorted(sources,key=lambda pair:pair[1]):
        source_digest.update(name.encode());source_digest.update(bytes.fromhex(digest(f)))
    manifest={'ok':True,'version':__version__,'app_count':len(inputs)//2,'source_file_count':len(sources),
        'source_digest':source_digest.hexdigest(),'console_verified':False,'installable_wup':False,
        'files':[{'name':p.name,'bytes':p.stat().st_size,'sha256':digest(p)} for p in (source,kit)],
        'apps':[{'path':name,'sha256':digest(f)} for f,name in inputs]}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (out/'SHA256SUMS.txt').write_text(''.join(x['sha256']+'  '+x['name']+'\n' for x in manifest['files']))
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':main()
