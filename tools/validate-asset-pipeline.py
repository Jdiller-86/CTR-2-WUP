#!/usr/bin/env python3
"""Original encrypted CIA -> verified RomFS -> compiled guest file access.

Reuses an already checked validate-libctru --application romfs output directory.
No game, real key store, game patch or network lookup is needed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host import __version__
from host.assets import prepare_assets
from host.rom_prepare import prepare_rom
from tests.encrypted_fixtures import encrypted_cia,encrypted_ncch,write_key_config

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--application-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();source=a.application_dir.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    encoded,plain=encrypted_ncch(base_plain=(source/'original.cxi').read_bytes())
    with tempfile.TemporaryDirectory(prefix='ctr2wup-original-asset-keys-') as temp:
        keys=write_key_config(temp);input=out/'original-encrypted.cia';input.write_bytes(encrypted_cia(encoded))
        original_hash=hashlib.sha256(input.read_bytes()).hexdigest()
        preparation=prepare_rom(input,out/'prepared',keys_config=keys)
        if (out/'prepared/game.cxi').read_bytes()!=plain or not preparation['full_ivfc_verified']:
            raise RuntimeError('Recovered original input/IVFC differs')
        assets=prepare_assets(out/'prepared/game.cxi',out/'assets')
        blob=out/'assets/romfs.bin'
        if blob.read_bytes()!=(source/'romfs-level3.bin').read_bytes():raise RuntimeError('Prepared asset blob differs')
        execution=subprocess.run([str(source/'runner'),'--romfs-file',str(blob)],capture_output=True,text=True,check=True)
        (out/'guest.log').write_text(execution.stdout+execution.stderr)
        guest=json.loads(execution.stdout.splitlines()[-1])
        if guest['state']!='exited' or guest['fallback'] or 'CTR2WUP compiled RomFS PASS' not in guest['messages']:
            raise RuntimeError('Original guest did not pass its own asset assertions')
        if hashlib.sha256(input.read_bytes()).hexdigest()!=original_hash:raise RuntimeError('Input was changed')
    report={'ok':True,'version':__version__,'input_sha256':original_hash,'source_preserved':True,
            'synthetic_keys_transient':True,'prepared_code_revision':preparation['executable_revision'],
            'full_ivfc_verified':True,'asset_blob':assets['romfs_file'],'guest':guest,
            'commercial_profile_enabled':False,'console_verified':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
