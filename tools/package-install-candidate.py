#!/usr/bin/env python3
"""Create a source-bound original installation candidate without external keys."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from host import __version__
from host.package_homebrew import pack_homebrew
from host.title_stage import stage_title

TITLE_ID = '0005000043543257'
TITLE_NAME = 'CTR2WUP Install Check'


def candidate(native, output):
    spec = importlib.util.spec_from_file_location('install_release', ROOT/'tools/package-audio-round.py')
    audio = importlib.util.module_from_spec(spec);spec.loader.exec_module(audio)
    commit = audio.identity()
    receipt = audio.release.read_json(native/'build-report.json')
    if (receipt.get('ok') is not True or receipt.get('target') != 'install-check'
            or receipt.get('profile') != 'native-install' or receipt.get('runtime_version') != __version__
            or receipt.get('source_commit') != commit or receipt.get('source_dirty') is not False
            or receipt.get('input_identity') is not None):
        raise ValueError('Require the clean exact-source original install-check build')
    names = ('ctr2wup-install-check.rpx', 'ctr2wup-install-check.wuhb')
    audio.release.check_records(receipt.get('artifacts'), names, native)
    rpx = native/names[0]
    audio.release.validate_binary(rpx)
    if output.exists() or output.is_symlink() or any(p.is_symlink() for p in output.parents):
        raise ValueError('Preserve existing output and avoid symlink destinations')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.install-candidate-', dir=output.parent) as tmp:
        stage = Path(tmp)/'title'
        stage_title(rpx, stage, title_id=TITLE_ID, name=TITLE_NAME, version=27)
        packed = Path(tmp)/'package'
        proof = pack_homebrew(stage, packed)
        (packed/'pack-report.json').unlink()
        files = [dict(path=p.name, size=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                 for p in sorted(packed.iterdir())]
        report = dict(schema_version=1, ok=True, kind='ctr2wup-install-candidate',
                      version=__version__, source_commit=commit, source_dirty=False,
                      original_inputs_only=True, profile='wiiu_plaintext_homebrew',
                      content_layout=proof['content_layout'],
                      title_id=TITLE_ID, title_name=TITLE_NAME, title_version=27,
                      rpx_sha256=hashlib.sha256(rpx.read_bytes()).hexdigest(),
                      install_test_ready=True, requires_signature_patches=True,
                      common_key_required=False, production_certificates_required=False,
                      signature_environment='EnvironmentLoader 01_sigpatches; Aroma alone is insufficient',
                      independent_integrity=proof['validation'], test_signing=proof['test_signing'],
                      console_verified=False, installation='unverified', files=files)
        if audio.identity() != commit:
            raise ValueError('Source changed during installation-candidate creation')
        (packed/'report.json').write_text(json.dumps(report, indent=2)+'\n')
        from host.atomic import publish_directory
        publish_directory(packed, output)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(candidate(args.native, args.output)))


if __name__ == '__main__':
    main()
