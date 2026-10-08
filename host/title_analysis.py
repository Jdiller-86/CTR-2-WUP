"""Private title intake: exact revision plus declared capabilities and gaps."""
import json
from pathlib import Path
import shutil
import tempfile
from .containers import inspect
from .atomic import publish_directory
from .errors import HostError


def analyze_title(path,output,*,lifter=False):
    inspection=inspect(path)
    partition=inspection['partitions'][0]
    output=Path(output).absolute()
    if output.exists():raise HostError('output_exists','Private analysis output already exists',stage='prepare')
    output.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix='.ctr2wup-intake-',dir=output.parent))
    try:
        external=None
        if lifter:
            from translator.upstream import analyze
            external=analyze(path)
            if external['executable_revision']!=inspection['executable_revision']:
                raise HostError('input_changed','Executable changed during intake',stage='analyze')
            (stage/'lifter-analysis.txt').write_text(external['analysis_text'],encoding='utf-8')
            external={k:v for k,v in external.items() if k!='analysis_text'}
        report={'schema_version':1,'ok':True,'stage':'title-intake','selected_candidate':'Super Mario 3D Land',
                'profile_enabled':False,'compatibility':'unsupported','console_verified':False,
                'identity':inspection['identity'],'executable_revision':inspection['executable_revision'],
                'input_sha256':inspection['input_sha256'],'segments':partition['segments'],
                'stack_size':partition['stack_size'],'bss_size':partition['bss_size'],
                'declared_services':partition.get('service_access',[]),
                'service_scope':'extended-header allowlist only; runtime IPC use/commands are not yet traced',
                'sections':partition['sections'],'external_lifter':external,
                'gaps':['Confirm selected title, region and exact revision against this dump',
                        'Inspect RomFS/CRO inventory and generated-code coverage/memory cost',
                        'Implement required kernel/IPC/HID/GSP/filesystem/APT/CFG/DSP behavior',
                        'Validate PICA rendering, GPU readback/coherency, audio and save/reload on console',
                        'Validate local packaging inputs, installer acceptance and Menu launch'],
                'inspection':inspection}
        (stage/'requirements-report.json').write_text(json.dumps(report,indent=2)+'\n')
        publish_directory(stage,output);return {**report,'output':str(output)}
    finally:
        if stage.exists():shutil.rmtree(stage)
