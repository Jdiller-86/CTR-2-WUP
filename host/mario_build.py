"""Basic private ROM -> experimental native Mario test WUP pipeline."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import time

from .atomic import publish_directory
from .build import _run_build,_source_state,_validate_rpx
from .doctor import doctor,repository_root,tool_environment
from .errors import HostError
from .loader import load_image
from .containers import inspect

MARIO_ID='0004000000054000'
MARIO_PRODUCT='CTR-P-AREE'

def mario_profile(inspection):
    main=inspection['partitions'][0]
    if (inspection['identity']['program_id']!=MARIO_ID or main['product_code']!=MARIO_PRODUCT
            or main.get('remaster_version')!=0):
        raise HostError('unsupported_profile','The test builder currently accepts the Super Mario 3D Land USA base application',stage='profile',
            details={'identity':inspection['identity'],'executable_revision':inspection['executable_revision'],
                     'compatibility':'unsupported','required_gates':['selected Mario 3D Land USA base revision']})
    if main.get('title',{}).get('region_mask',2)!=2:
        raise HostError('unsupported_profile','This test profile requires the USA application',stage='profile')
    if 'romfs' not in main['sections']:
        raise HostError('missing_romfs','Use the complete owned ROM, including RomFS; an executable-only intake export cannot build the game',stage='profile')
    return {'id':'mario3dland-usa-test-v1','name':'Super Mario 3D Land (Test)',
            'program_id':MARIO_ID,'compatibility':'experimental','gameplay_verified':False}

def title_identity(program_id):
    return '00050000'+hashlib.sha256(('ctr2wup-test:'+program_id).encode('ascii')).hexdigest()[:8]

def generated_image(image,profile,title_id,romfs_hash=None):
    """Native metadata only; installed content holds the actual guest bytes."""
    segments=image['segments'];identity=image['inspection']['executable_revision']
    code_hash=hashlib.sha256(image['code']).digest()
    def digest(name,value):return 'const uint8_t '+name+'[32]={'+','.join('0x%02x'%b for b in value)+'};\n'
    header='#ifndef CTR_GAME_IMAGE_H\n#define CTR_GAME_IMAGE_H\n#include "ctr_guest.h"\n'
    constants={'CTR_GAME_NAME':profile['name'],'CTR_GAME_PROGRAM_ID':profile['program_id'],
        'CTR_GAME_REVISION':identity,'CTR_GAME_DATA_ROOT':'fs:/vol/external01/ctr2wup/titles/'+title_id,
        'CTR_GAME_SD_FOLDER':'sd:/ctr2wup/titles/'+title_id+'/'}
    for name,value in constants.items():header+='#define '+name+' '+json.dumps(value,ensure_ascii=True)+'\n'
    header+=f'#define CTR_GAME_HAS_ROMFS {int(romfs_hash is not None)}\n'
    header+='extern const ctr_guest_image ctr_game_image;\nextern const uint8_t ctr_game_code_sha256[32],ctr_game_romfs_sha256[32];\n#endif\n'
    source='#include "game_image.h"\n'+digest('ctr_game_code_sha256',code_hash)
    source+=digest('ctr_game_romfs_sha256',bytes.fromhex(romfs_hash) if romfs_hash else bytes(32))
    descriptors=','.join('{'+','.join(str(segments[name][key])+'u' for key in ('address','pages','size'))+'}' for name in ('text','rodata','data'))
    source+=f'const ctr_guest_image ctr_game_image={{NULL,{len(image["code"])}u,{descriptors},{image["bss_size"]}u,{image["stack_size"]}u,"{identity}"}};\n'
    return header,source

def _private_output(repo,output):
    relative=output.resolve().relative_to(repo.resolve()) if output.resolve().is_relative_to(repo.resolve()) else None
    if relative is not None and (not relative.parts or relative.parts[0] not in ('private','output','artifacts')):
        raise HostError('private_output_required','Keep game packages in private/, output/, artifacts/, or outside the source checkout',stage='arguments')

def build_mario(path,output,*,keys_config=None,title_id=None,title_version=1,jobs=3,progress=None):
    return build_test_title(path,output,keys_config=keys_config,title_id=title_id,title_version=title_version,
        jobs=jobs,progress=progress,select_profile=mario_profile)

def build_test_title(path,output,*,keys_config=None,title_id=None,title_version=1,jobs=3,progress=None,select_profile):
    """Shared build implementation; original integration checks supply their profile."""
    from .rom_prepare import prepare_rom
    from .assets import prepare_assets
    from .title_stage import stage_title
    from .package_homebrew import pack_homebrew
    from translator.upstream import generate_into
    path=Path(path).expanduser().absolute();output=Path(output).expanduser().absolute();repo=repository_root()
    if not (repo/'runtime/wiiu/Makefile').is_file():
        raise HostError('missing_build_sources','Run from a source checkout or set CTR2WUP_PROJECT_ROOT to it',stage='prepare')
    if not path.is_file() or path.is_symlink():raise HostError('unsafe_input','Select a regular owned ROM file',stage='prepare')
    if output.exists() or output.is_symlink():raise HostError('output_exists','Existing output was preserved; choose a new destination',stage='prepare')
    if type(title_version) is not int or not 1<=title_version<=65535 or type(jobs) is not int or not 1<=jobs<=8:
        raise HostError('invalid_build_settings','Use title version 1..65535 and 1..8 build jobs',stage='arguments')
    _private_output(repo,output)
    output.parent.mkdir(parents=True,exist_ok=True)
    private=repo/'private';private.mkdir(exist_ok=True)
    if min(shutil.disk_usage(private).free,shutil.disk_usage(output.parent).free)<path.stat().st_size*3+256*1024*1024:
        raise HostError('insufficient_disk','Build needs three input-file sizes plus 256 MiB of free working space',stage='prepare')
    work=Path(tempfile.mkdtemp(prefix='mario-build-',dir=private))
    stage=Path(tempfile.mkdtemp(prefix='.ctr2wup-title-',dir=output.parent))
    steps=[];published=False;started=time.perf_counter();source_commit,source_dirty=_source_state(repo)
    def step(name,action):
        if progress:progress(name)
        tick=time.perf_counter();value=action()
        steps.append({'name':name,'state':'passed','seconds':round(time.perf_counter()-tick,3)})
        return value
    try:
        prepared=step('Prepare ROM',lambda:prepare_rom(path,work/'input',keys_config=keys_config))
        game=work/'input/game.cxi';profile=step('Select title',lambda:select_profile(inspect(game)))
        image=load_image(game)
        prerequisites=doctor()
        if not prerequisites['ok']:raise HostError('missing_toolchain',prerequisites['next_action'],stage='prepare',details={'missing':prerequisites['missing_required']})
        try:from cryptography.hazmat.primitives.ciphers import Cipher  # noqa: F401
        except ImportError as exc:raise HostError('missing_crypto','Install ctr2wup[packaging,decryption] before building',stage='prepare') from exc
        title_id=title_id or title_identity(profile['program_id'])
        from .package_validator import _title_id,ValidatorError
        try:title_id=_title_id(title_id)
        except ValidatorError as exc:raise HostError(exc.code,str(exc),stage='profile') from exc
        assets=None
        if 'romfs' in image['inspection']['partitions'][0]['sections']:
            assets=step('Prepare assets',lambda:prepare_assets(game,work/'assets'))
        generation=step('Translate ARM code',lambda:generate_into(game,work/'aot'))
        if generation.get('modules'):
            raise HostError('unsupported_module_layout','This intake requires an explicit packaged module layout before native execution',stage='translate')
        header,metadata=generated_image(image,profile,title_id,assets['romfs_file']['sha256'] if assets else None)
        (work/'game_image.h').write_text(header,encoding='utf-8');(work/'game_image.generated.c').write_text(metadata,encoding='utf-8')
        relative=os.path.relpath(work,repo/'runtime/wiiu').replace(os.sep,'/')
        command=[shutil.which('make',path=tool_environment().get('PATH')),'-C',str(repo/'runtime/wiiu'),
            'GAME_TEST=1','EXTERNAL_AOT=1','GAME_DIR='+relative,'UPSTREAM_DIR='+relative+'/aot',
            'OUTPUT='+relative+'/native','TARGET=ctr2wup-mario-test','CALL_LIMIT=16','-j'+str(jobs)]
        step('Compile Wii U title',lambda:_run_build(command,tool_environment(),work/'compile.log',repo))
        rpx=work/'native/ctr2wup-mario-test.rpx';_validate_rpx(rpx)
        step('Stage title',lambda:stage_title(rpx,work/'title',title_id=title_id,name=profile['name'],version=title_version))
        content=work/'title/content';(content/'guest-code.bin').write_bytes(image['code'])
        if assets:shutil.copyfile(work/'assets/romfs.bin',content/'romfs.bin')
        (content/'test-profile.json').write_text(json.dumps({'profile':profile,'executable_revision':image['inspection']['executable_revision'],
            'code_sha256':hashlib.sha256(image['code']).hexdigest(),'romfs_sha256':assets['romfs_file']['sha256'] if assets else None,
            'title_id':title_id,'title_version':title_version},indent=2)+'\n')
        packed=step('Pack and verify WUP',lambda:pack_homebrew(work/'title',stage/'wup',require_menu=True,require_startup=True,content_id_base=title_version<<16))
        if (source_commit,source_dirty)!=_source_state(repo):raise HostError('source_changed','Project source identity changed during the build',stage='compile')
        report={'schema_version':1,'ok':True,'stage':'rom-to-wup','profile':profile['id'],'compatibility':'experimental',
            'test_package_ready':True,'gameplay_verified':False,'console_verified':False,
            'title_id':title_id,'title_version':title_version,'name':profile['name'],'input_identity':image['inspection']['identity'],
            'executable_revision':image['inspection']['executable_revision'],'source_sha256':prepared['source_sha256'],
            'code_sha256':hashlib.sha256(image['code']).hexdigest(),
            'romfs_sha256':assets['romfs_file']['sha256'] if assets else None,
            'translator':{key:generation[key] for key in ('source_commit','generator_sha256','abi','generated_instructions')},
            'source_commit':source_commit,'source_dirty':source_dirty,'backend':'external-AOT-with-explicit-interpreter-recovery',
            'renderer':'shared-software-PICA','assets_in_wup':bool(assets),'stages':steps,
            'seconds':round(time.perf_counter()-started,3),'package':packed,
            'runtime_log_directory':'sd:/ctr2wup/titles/'+title_id+'/results/',
            'save_path':'sd:/ctr2wup/titles/'+title_id+'/save.snapshot',
            'remaining':['Run the generated title with the owned ROM in a local/console session','Resolve observed CPU/service/GPU/audio gaps and verify boot/gameplay']}
        report['package'].pop('output',None)
        (stage/'build-report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n')
        guide=(f'{profile["name"]}\n\nInstall the wup/ folder with WUP Installer GX2 on Aroma with signature patches. '
            f'Test title ID: {title_id}; version {title_version}. Check that this ID is not used by an unrelated title.\n\n'
            'Executable and RomFS assets are included in the WUP. Keep an SD card inserted for private logs and saves. '
            'TV shows the 3DS top screen; GamePad shows the bottom screen and touch. A/B/X/Y, L/R, D-pad, Circle Pad, '
            'Minus (Select), and Plus (Start) map to the guest. HOME or L+R+Minus requests exit.\n\n'
            f'If execution stops, retain {report["runtime_log_directory"]} and build-report.json for the local development session. '
            'Keep this package and its logs private. This is an experimental test build; boot/gameplay and performance are unverified. '
            'Installed-title exit/relaunch remains unresolved (project issue #15); this build uses the existing lifecycle helpers.\n')
        (stage/'START-HERE.txt').write_text(guide)
        shutil.copyfile(work/'compile.log',stage/'compile.log')
        publish_directory(stage,output)
        published=True
        return {**report,'output':str(output),'wup':str(output/'wup')}
    except HostError as exc:
        # Compiler output can contain guest-derived code; retain it privately.
        exc.details.update({'private_work_directory':str(work)})
        raise
    finally:
        if stage.exists():shutil.rmtree(stage)
        # Successful builds retain only deliverables; failed work stays private
        # so the real failing stage can be diagnosed in a later local session.
        if published and work.exists():shutil.rmtree(work)
