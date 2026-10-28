#!/usr/bin/env python3
"""Source-bound minimal original guest-audio and retained-system-log handoff."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import zipfile
ROOT=Path(__file__).resolve().parents[1]

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);out=importlib.util.module_from_spec(spec);spec.loader.exec_module(out);return out
round_tools=module('runtime_round_audio','tools/package-audio-round.py')
reference_tools=module('runtime_round_reference','tools/validate-guest-dsp.py')
release=round_tools.release
DUMPER_COMMIT='3bb31ca67a6b015cf79f6ef9397f4d039ae60192'
DUMPER_ZIP_SHA='2bffffb43d35d89d3353586516ef7de523c3ab3e628a9a623927ae511c77c066'
DUMPER_SOURCE_SHA='2ee4051057ca51eee2af8c6f120a8b6929f10d9efaebf21c5014e57a007406aa'


def package(app,reference,sdk_receipt,sdk_licenses,dumper,dumper_source,output):
    commit=round_tools.identity()
    if any(p.is_symlink() for p in (output,*output.parents)): raise ValueError('Symlink package output')
    native,native_bytes=round_tools.json_bytes(app/'build-report.json')
    if (native.get('ok') is not True or native.get('source_commit')!=commit or native.get('source_dirty') is not False
        or native.get('target')!='guest-audio' or native.get('profile')!='original-guest-dsp-pcm-v1'
        or native.get('runtime_version')!=round_tools.VERSION or native.get('input_identity') is not None): raise ValueError('Require the clean source-matched Guest Audio native build')
    names=('ctr2wup-guest-audio.rpx','ctr2wup-guest-audio.wuhb')
    release.check_records(native.get('artifacts'),names,app)
    entries={}
    binaries={}
    for name in names:
        release.validate_binary(app/name);binaries[name]=release.regular(app/name,4*1024*1024).read_bytes()
    rpx=binaries[names[0]];wuhb=binaries[names[1]]
    if wuhb.find(rpx)!=512 or wuhb.rfind(rpx)!=512: raise ValueError('WUHB must contain the exact current RPX once at its native offset')
    entries['sd-card/wiiu/apps/'+names[1]]=wuhb
    ref,ref_bytes=round_tools.json_bytes(reference)
    expected=reference_tools.reference()
    if (ref.get('ok') is not True or ref.get('source_commit')!=commit or ref.get('source_dirty') is not False
        or ref.get('kind')!='ctr2wup-guest-dsp-reference' or ref.get('modes')!=['normal','sanitized']
        or any(ref.get(k) is not True for k in ('independent_reference','original_inputs_only','arm_interpreter_aot_exact'))
        or ref.get('dsp_blocks')!=300 or ref.get('dsp_samples')!=48000 or ref.get('output_frames')!=70398
        or ref.get('arm_instructions')!=114701 or ref.get('pcm_sha256')!=hashlib.sha256(expected).hexdigest()): raise ValueError('Require complete clean independent guest-DSP evidence')
    wav=release.regular(reference.parent/'reference.wav',1024*1024).read_bytes()
    import wave
    with wave.open(io.BytesIO(wav)) as f:
        if f.getparams()[:4]!=(2,2,48000,70398) or f.readframes(70399)!=expected: raise ValueError('Reference WAVE differs from independent exact PCM')
    sdk=release.regular(sdk_receipt,512).read_bytes()
    if sdk.decode().strip()!=release.SDK_IMAGE: raise ValueError('Wrong SDK image')
    original=release.regular(dumper,3*1024*1024).read_bytes()
    source=release.regular(dumper_source,2*1024*1024).read_bytes()
    if hashlib.sha256(original).hexdigest()!=DUMPER_ZIP_SHA or hashlib.sha256(source).hexdigest()!=DUMPER_SOURCE_SHA: raise ValueError('Require the exact official dumper and its pinned complete source')
    with zipfile.ZipFile(io.BytesIO(original)) as z:
        if z.namelist()!=['WiiUCrashLogDumper.wuhb'] or z.testzip(): raise ValueError('Unexpected official dumper archive')
        entries['sd-card/wiiu/apps/WiiUCrashLogDumper.wuhb']=z.read('WiiUCrashLogDumper.wuhb')
    with zipfile.ZipFile(io.BytesIO(source)) as z:
        entries['licenses/WiiUCrashLogDumper-LICENSE']=z.read('WiiUCrashLogDumper-'+DUMPER_COMMIT+'/LICENSE')
    entries.update(round_tools.sdk_entries(sdk_licenses))
    entries.update({'reference.wav':wav,'support/native-build.json':native_bytes,'support/reference.json':ref_bytes,
                    'support/sdk-image-digest.txt':sdk,'support/WiiUCrashLogDumper-source.zip':source,
                    'support/CTR2WUP-source.zip':round_tools.checked_source(commit),
                    'support/review-runtime-round.py':(ROOT/'tools/review-runtime-round.py').read_bytes(),
                    'LICENSE':(ROOT/'LICENSE').read_bytes(),'THIRD_PARTY_NOTICES':(ROOT/'THIRD_PARTY_NOTICES').read_bytes(),
                    'START-HERE.md':release.render_guide(ROOT/'tools/templates/runtime-round.md',round_tools.VERSION,commit)})
    entries['sd-card/ctr2wup/dsp-check/kit.json']=(json.dumps({'kind':'ctr2wup-runtime-round','source_commit':commit})+'\n').encode()
    manifest={'schema':1,'kind':'ctr2wup-runtime-round','source_commit':commit,'source_dirty':False,
              'reference':ref,'console_verified':False,'installed_exit_repair':False,'finite_prerender':True,
              'system_logs_private':True,'dumper_source_commit':DUMPER_COMMIT,
              'files':[{'path':n,'bytes':len(d),'sha256':hashlib.sha256(d).hexdigest()} for n,d in sorted(entries.items())]}
    entries['manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    entries['SHA256SUMS.txt']=''.join(hashlib.sha256(d).hexdigest()+'  '+n+'\n' for n,d in sorted(entries.items())).encode()
    if round_tools.identity()!=commit or sum(map(len,entries.values()))>24*1024*1024: raise ValueError('Source changed or handoff too large')
    output.mkdir(parents=True,exist_ok=False);stage=output/'stage';stage.mkdir()
    for name,data in entries.items():
        path=stage/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    target=output/'CTR2WUP-runtime-check.zip'
    with zipfile.ZipFile(target,'x',compression=zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(entries.items()): z.writestr(name,data)
    with zipfile.ZipFile(target) as z:
        if z.testzip() or z.namelist()!=sorted(entries) or any(z.read(n)!=d for n,d in entries.items()): raise ValueError('ZIP CRC/readback mismatch')
    report={'ok':True,'source_commit':commit,'archive':target.name,'bytes':target.stat().st_size,
            'sha256':release.sha256(target),'console_verified':False,'files':len(entries)}
    (output/'package-report.json').write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('app','reference','sdk-receipt','sdk-licenses','dumper','dumper-source','output'): p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();print(json.dumps(package(a.app,a.reference,a.sdk_receipt,a.sdk_licenses,a.dumper,a.dumper_source,a.output)))
