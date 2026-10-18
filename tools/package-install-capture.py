#!/usr/bin/env python3
"""Build the fixed-package MCP capture app; never replace or modify the WUP."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from host.atomic import publish_directory
from host.errors import HostError

SPEC=importlib.util.spec_from_file_location('capture_provenance',ROOT/'tools/package-audio-round.py')
audio=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audio)
NAMES={f'{i:08x}.app' for i in range(4)}|{'title.tmd','title.tik','title.cert'}


def identity():
    commit=audio.release.git(ROOT,'rev-parse','HEAD')
    if not re.fullmatch('[0-9a-f]{40}',commit) or audio.release.git(ROOT,'status','--porcelain'):
        raise ValueError('Capture delivery requires clean committed source')
    return commit


def configuration(path):
    data=audio.release.regular(Path(path),16384).read_bytes()
    document=json.loads(data)
    validate_configuration(document)
    return document,data


def validate_configuration(document):
    schema=document.get('schema_version') if isinstance(document,dict) else None
    digest='package_manifest_sha256' if schema==2 else 'package_artifact_sha256'
    if (not isinstance(document,dict) or set(document)!={'schema_version','title_id','package_source',digest,'files'}
            or type(schema) is not int or schema not in (1,2)
            or document['title_id']!='0005000043543257'
            or not isinstance(document['package_source'],str) or not re.fullmatch('[0-9a-f]{40}',document['package_source'])
            or not isinstance(document[digest],str)
            or not re.fullmatch('[0-9a-f]{64}',document[digest])):
        raise ValueError('Require an explicit original M1 title, source and package digest')
    files=document['files']
    if not isinstance(files,list) or (len(files)!=7 if schema==1 else not 8<=len(files)<=64):
        raise ValueError('Require a bounded complete pinned installer inventory')
    apps=[r.get('name') for r in files if isinstance(r,dict) and isinstance(r.get('name'),str) and re.fullmatch('[0-9a-f]{8}\\.app',r['name'])]
    base=min(int(n[:8],16) for n in apps) if apps else 0
    if schema==2 and base & 0xffff:
        raise ValueError('Require a revision-aligned content identity base')
    names=NAMES if schema==1 else {f'{base+i:08x}.app' for i in range(len(files)-3)}|{'title.tmd','title.tik','title.cert'}
    seen=set()
    for row in files:
        if (not isinstance(row,dict) or set(row)!={'name','size','sha256'}
                or not isinstance(row['name'],str) or row['name'] not in names or row['name'] in seen
                or type(row['size']) is not int or not 0<row['size']<=16*1024*1024
                or not isinstance(row['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',row['sha256'])):
            raise ValueError('Require unique bounded installer filenames, sizes and SHA-256 digests')
        seen.add(row['name'])
    if seen!=names:
        raise ValueError('Require every consecutive content stream and all title metadata')


def header(document,installed_files=None,title_version=None):
    validate_configuration(document)
    if (installed_files is None)!=(title_version is None):
        raise ValueError('Installed file pins and title revision must be supplied together')
    if title_version is not None and (document['schema_version']!=2 or type(title_version) is not int or not 30<=title_version<=65535):
        raise ValueError('Require a bounded original installed title revision')
    digest='package_manifest_sha256' if document['schema_version']==2 else 'package_artifact_sha256'
    record='manifest_sha256' if document['schema_version']==2 else 'artifact_sha256'
    result=['/* Generated fixed-payload identity, not a user-editable SD manifest. */',
            '#define CTR_CAPTURE_TITLE "'+document['title_id']+'"',
            '#define CTR_CAPTURE_TITLE_ID UINT64_C(0x'+document['title_id']+')',
            '#define CTR_CAPTURE_PACKAGE_SOURCE "'+document['package_source']+'"',
            '#define CTR_CAPTURE_PACKAGE_SHA256 "'+document[digest]+'"',
            '#define CTR_CAPTURE_PACKAGE_RECORD "'+record+'='+document[digest]+'"',
            '#define CTR_CAPTURE_FILE_COUNT '+str(len(document['files'])),
            '#define CTR_CAPTURE_M1_COMPLETION '+str(int(document['schema_version']==2)),
            '#define CTR_CAPTURE_DIRECTORY "'+('CTR2WUP-M1-r'+str(title_version) if title_version is not None else 'CTR2WUP-M1')+'"',
            'static const CtrInstallCaptureFile ctr_capture_files[CTR_CAPTURE_FILE_COUNT]={']
    for row in document['files']:
        digest=','.join('0x'+row['sha256'][i:i+2] for i in range(0,64,2))
        result.append('    {"'+row['name']+'",'+str(row['size'])+', {'+digest+'}},')
    result.append('};')
    if document['schema_version']==2:
        previous=json.loads(audio.release.regular(ROOT/'packaging/install-predecessor.json',16384).read_bytes())
        names={'code/app.xml','code/cos.xml','code/ctr2wup.rpx','meta/meta.xml',
               'meta/iconTex.tga','meta/bootLogoTex.tga','meta/bootDrcTex.tga','meta/bootTvTex.tga',
               'meta/bootMovie.h264'}
        if (previous.get('schema_version')!=1 or previous.get('title_id')!=document['title_id']
                or previous.get('source_commit')!='fe4acafeb19609d55a63484b45f2b9d0521b22e9'
                or previous.get('title_version')!=31 or not isinstance(previous.get('files'),list)
                or len(previous['files'])!=9 or {r.get('name') for r in previous['files']}!=names):
            raise ValueError('Require the complete reviewed installed predecessor')
        result += ['#define CTR_CAPTURE_REPAIR_FILE_COUNT 9',
                   '#define CTR_CAPTURE_REPAIR_VERSION 31',
                   '#define CTR_CAPTURE_REPAIR_SOURCE "'+previous['source_commit']+'"',
                   'static const CtrInstallCaptureFile ctr_capture_previous_files[CTR_CAPTURE_REPAIR_FILE_COUNT]={']
        for row in previous['files']:
            if (type(row.get('size')) is not int or not 0<row['size']<=3*1024*1024
                    or not isinstance(row.get('sha256'),str) or not re.fullmatch('[0-9a-f]{64}',row['sha256'])):
                raise ValueError('Invalid installed predecessor file pin')
            digest=','.join('0x'+row['sha256'][i:i+2] for i in range(0,64,2))
            result.append('    {"'+row['name']+'",'+str(row['size'])+', {'+digest+'}},')
        result.append('};')
    else:
        result.append('#define CTR_CAPTURE_REPAIR_FILE_COUNT 0')
    if installed_files is not None:
        names={'code/app.xml','code/cos.xml','code/ctr2wup.rpx','meta/meta.xml',
               'meta/iconTex.tga','meta/bootLogoTex.tga','meta/bootDrcTex.tga','meta/bootTvTex.tga',
               'meta/bootMovie.h264'}
        if (document['schema_version']!=2 or type(title_version) is not int or not 30<=title_version<=65535
                or not isinstance(installed_files,list)
                or len(installed_files)!=9 or any(not isinstance(r,dict) for r in installed_files)
                or {r.get('path') for r in installed_files}!=names):
            raise ValueError('Require all independently verified original installed files')
        app_ids=[int(r['name'][:8],16) for r in document['files'] if r['name'].endswith('.app')]
        if min(app_ids) not in (0,title_version<<16):
            raise ValueError('Content identity base differs from the installed title revision')
        result += ['#define CTR_CAPTURE_CURRENT_FILE_COUNT 9',
                   '#define CTR_CAPTURE_CURRENT_VERSION '+str(title_version),
                   'static const CtrInstallCaptureFile ctr_capture_current_files[CTR_CAPTURE_CURRENT_FILE_COUNT]={']
        for row in installed_files:
            if (type(row.get('bytes')) is not int or not 0<row['bytes']<=3*1024*1024
                    or not isinstance(row.get('sha256'),str) or not re.fullmatch('[0-9a-f]{64}',row['sha256'])):
                raise ValueError('Invalid installed current file pin')
            digest=','.join('0x'+row['sha256'][i:i+2] for i in range(0,64,2))
            result.append('    {"'+row['path']+'",'+str(row['bytes'])+', {'+digest+'}},')
        result.append('};')
    else:
        result.append('#define CTR_CAPTURE_CURRENT_FILE_COUNT 0')
    return '\n'.join(result+[''])


def build_app(document,build,sdk,*,installed_files=None,title_version=None):
    """Compile the reviewed frontend with immutable in-binary payload pins."""
    receipt=audio.release.regular(sdk/'ctr2wup-image-digest.txt',4096).read_bytes()
    if receipt.decode().strip()!=audio.release.SDK_IMAGE:
        raise ValueError('Require the pinned Wii U SDK receipt')
    build.mkdir()
    (build/'install_capture_package.h').write_text(header(document,installed_files,title_version),encoding='ascii')
    subprocess.run(['make','-C',str(ROOT/'runtime/wiiu/install-capture'),'-j','3',
                    'OUTPUT='+str(build),'CONFIG='+str(build),'DEVKITPRO='+str(sdk),
                    'DEVKITPPC='+str(sdk/'devkitPPC')],check=True)
    app=build/'ctr2wup-install-capture.wuhb'
    audio.release.validate_binary(app)
    audio.release.validate_binary(build/'ctr2wup-install-capture.rpx')
    return audio.release.regular(app).read_bytes()


def package(output,sdk):
    commit=identity()
    document,config=configuration(ROOT/'packaging/install-capture.json')
    sdk=Path(sdk).absolute();output=Path(output).absolute()
    receipt=audio.release.regular(sdk/'ctr2wup-image-digest.txt',4096).read_bytes()
    if receipt.decode().strip()!=audio.release.SDK_IMAGE:
        raise ValueError('Require the pinned Wii U SDK receipt')
    if output.exists() or output.is_symlink():
        raise ValueError('Existing capture output was preserved')
    if any(path.is_symlink() for path in output.parents):
        raise ValueError('Capture output cannot traverse symlinks')
    output.parent.mkdir(parents=True,exist_ok=True)
    temporary=Path(tempfile.mkdtemp(prefix='.install-capture-',dir=output.parent))
    try:
        build=temporary/'build'
        app=build_app(document,build,sdk)
        if identity()!=commit:
            raise ValueError('Source changed during the capture build')
        entries={
            'START-HERE.md':(ROOT/'tools/templates/install-capture.md').read_bytes(),
            'sd-card/wiiu/apps/ctr2wup-install-capture.wuhb':app,
            'support/package-identity.json':config,
            'support/LICENSE':(ROOT/'LICENSE').read_bytes(),
            'support/THIRD_PARTY_NOTICES':(ROOT/'THIRD_PARTY_NOTICES').read_bytes(),
            'support/CTR2WUP-source-'+audio.VERSION+'.zip':audio.checked_source(commit),
            'support/sdk-image-digest.txt':receipt,
        }
        entries.update(('support/'+name,data) for name,data in audio.sdk_entries(sdk/'licenses').items())
        records=[{'path':name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()}
                 for name,data in sorted(entries.items())]
        manifest={'schema_version':1,'kind':'ctr2wup-install-capture','protocol':'mcp-capture-v2',
                  'source_commit':commit,'source_dirty':False,'package_source':document['package_source'],
                  'title_id':document['title_id'],'package_unchanged':True,'original_inputs_only':True,
                  'requires_signature_patches':True,'console_verified':False,'full_milestone_acceptance':False,
                  'log_directory':'ctr2wup/install-check/install-capture','files':records}
        entries['support/manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
        entries['support/SHA256SUMS']=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n'
                                              for name,data in sorted(entries.items())).encode()
        for name,data in entries.items():
            path=temporary/'stage'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        # Build intermediates are not part of the FTP handoff.
        shutil.rmtree(build)
        if identity()!=commit:
            raise ValueError('Source changed before publication')
        publish_directory(temporary,output)
        return manifest
    finally:
        if temporary.exists():shutil.rmtree(temporary)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--sdk',type=Path,default=Path(os.environ.get('DEVKITPRO','/workspace/ctr2wup-sdk')))
    args=parser.parse_args()
    try:
        result=package(args.output,args.sdk)
    except (ValueError,OSError,HostError,subprocess.CalledProcessError) as error:
        parser.exit(1,str(error)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))


if __name__=='__main__':main()
