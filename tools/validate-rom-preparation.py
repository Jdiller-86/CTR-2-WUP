#!/usr/bin/env python3
"""Exercise original encrypted input through intake, actual AOT and Wii U builds."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host import __version__
from host.guest_fixture import fixture_bytes
from tests.encrypted_fixtures import encrypted_cia, encrypted_ncch, write_key_config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--wiiu', action='store_true', help='Also build the prepared original fixture as RPX/WUHB')
    args = parser.parse_args(); output = args.output.absolute(); output.mkdir(parents=True, exist_ok=False)
    checks = []
    def run(name, command):
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (output / (name + '.log')).write_text(result.stdout + result.stderr)
        checks.append({'name': name, 'exit_code': result.returncode})
        if result.returncode: raise RuntimeError(name + ' failed; see its preserved log')
        return json.loads(result.stdout)
    (ROOT / 'private').mkdir(exist_ok=True)
    # No real console key is used. Even synthetic transient key files are kept
    # private and deleted; they never enter reports, source or console bundles.
    with tempfile.TemporaryDirectory(prefix='original-rom-crypto-', dir=ROOT / 'private') as temp:
        config = write_key_config(Path(temp) / 'synthetic keys')
        encoded, expected = encrypted_ncch(method=1, romfs=False, base_plain=fixture_bytes())
        source = output / 'original-encrypted.cia'; source.write_bytes(encrypted_cia(encoded))
        original_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        prepared = run('prepare', [sys.executable, '-m', 'host', 'prepare-rom', str(source),
                       '--keys-config', str(config), '--output', str(output / 'prepared')])
        game = output / 'prepared/game.cxi'
        if game.read_bytes() != expected: raise RuntimeError('Prepared original bytes differ')
        intake = run('intake', [sys.executable, '-m', 'host', 'prepare-intake', str(game),
                     '--output', str(output / 'intake')])
        if not intake['executable_available'] or intake['executable_revision'] != prepared['executable_revision']:
            raise RuntimeError('Small executable intake differs from prepared input')
        guest = run('external-aot', [sys.executable, '-m', 'host', 'run-fixture', str(game),
                    '--backend', 'upstream', '--output', str(output / 'execution')])
        if args.wiiu:
            build = run('wiiu', [sys.executable, '-m', 'host', 'build', str(game),
                        '--target', 'guest-fixture', '--backend', 'upstream', '--output', str(output / 'wiiu')])
        else: build = None
        if hashlib.sha256(source.read_bytes()).hexdigest() != original_hash:
            raise RuntimeError('Original encrypted source changed')
    report = {'ok': True, 'version': __version__, 'original_fixtures_only': True,
              'checks': checks, 'preparation': prepared, 'intake_revision_match': True,
              'execution': guest, 'wiiu_build': build, 'source_unchanged': True,
              'transient_synthetic_keys_removed': True, 'real_console_keys_used': False,
              'console_verified': False, 'commercial_profile_enabled': False,
              'installable_wup_verified': False, 'native_windows_verified': False}
    (output / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'ok': True, 'checks': checks, 'report': str(output / 'report.json')}))


if __name__ == '__main__': main()
