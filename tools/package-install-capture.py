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
    if (not isinstance(document,dict) or set(document)!={'schema_version','title_id','package_source','package_artifact_sha256','files'}
            or type(document['schema_version']) is not int or document['schema_version']!=1
            or document['title_id']!='0005000043543257'
            or not isinstance(document['package_source'],str) or not re.fullmatch('[0-9a-f]{40}',document['package_source'])
            or not isinstance(document['package_artifact_sha256'],str)
            or not re.fullmatch('[0-9a-f]{64}',document['package_artifact_sha256'])):
        raise ValueError('Require an explicit original M1 title, source and package digest')
    files=document['files']
    if not isinstance(files,list) or len(files)!=7:
        raise ValueError('Require exactly seven pinned installer files')
    seen=set()
    for row in files:
        if (not isinstance(row,dict) or set(row)!={'name','size','sha256'}
                or not isinstance(row['name'],str) or row['name'] not in NAMES or row['name'] in seen
                or type(row['size']) is not int or not 0<row['size']<=16*1024*1024
                or not isinstance(row['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',row['sha256'])):
            raise ValueError('Require unique bounded installer filenames, sizes and SHA-256 digests')
        seen.add(row['name'])
    return document,data


def header(document):
    result=['/* Generated fixed-payload identity, not a user-editable SD manifest. */',
            '#define CTR_CAPTURE_TITLE "'+document['title_id']+'"',
            '#define CTR_CAPTURE_TITLE_ID UINT64_C(0x'+document['title_id']+')',
            '#define CTR_CAPTURE_PACKAGE_SOURCE "'+document['package_source']+'"',
            '#define CTR_CAPTURE_PACKAGE_SHA256 "'+document['package_artifact_sha256']+'"',
            'static const CtrInstallCaptureFile ctr_capture_files[7]={']
    for row in document['files']:
        digest=','.join('0x'+row['sha256'][i:i+2] for i in range(0,64,2))
        result.append('    {"'+row['name']+'",'+str(row['size'])+', {'+digest+'}},')
    return '\n'.join(result+['};',''])


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
        build=temporary/'build';build.mkdir()
        (build/'install_capture_package.h').write_text(header(document),encoding='ascii')
        subprocess.run(['make','-C',str(ROOT/'runtime/wiiu/install-capture'),'-j','3',
                        'OUTPUT='+str(build),'CONFIG='+str(build),'DEVKITPRO='+str(sdk),
                        'DEVKITPPC='+str(sdk/'devkitPPC')],check=True)
        if identity()!=commit:
            raise ValueError('Source changed during the capture build')
        app=build/'ctr2wup-install-capture.wuhb'
        audio.release.validate_binary(app)
        audio.release.validate_binary(build/'ctr2wup-install-capture.rpx')
        entries={
            'START-HERE.md':(ROOT/'tools/templates/install-capture.md').read_bytes(),
            'sd-card/wiiu/apps/ctr2wup-install-capture.wuhb':audio.release.regular(app).read_bytes(),
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
