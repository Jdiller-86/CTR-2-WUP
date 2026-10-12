"""Private validated main RomFS blob/manifest for the injection pipeline."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

from . import __version__
from .atomic import publish_directory
from .containers import Reader, inspect, u32
from .errors import HostError
from .romfs import Region, fail, inspect_level3, verify_ivfc


def verify_main_romfs(reader, main):
    section = main['sections'].get('romfs')
    if section is None: fail("missing_romfs", "This executable view contains no RomFS assets")
    header = reader.read(main['offset'], 512, 'NCCH header')
    anchored = u32(header, 0x1B8) * (512 << header[0x18E])
    region = Region(reader, section['offset'], section['size'])
    ivfc = verify_ivfc(region, anchored_bytes=anchored)
    level3 = Region(reader, section['offset'] + ivfc['level3_offset'], ivfc['level3_size'])
    metadata = inspect_level3(level3)
    return ivfc, metadata, level3


def prepare_assets(path, output):
    path, output = Path(path).absolute(), Path(output).absolute()
    if path.is_symlink() or not path.is_file(): fail("unsafe_input", "Asset preparation requires a regular decrypted input")
    if output.exists() or output.is_symlink(): fail("output_exists", "Asset output already exists and was preserved")
    checked = inspect(path); main = checked['partitions'][0]
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.ctr2wup-assets-', dir=output.parent))
    try:
        with path.open('rb') as handle:
            reader = Reader(handle); ivfc, metadata, level3 = verify_main_romfs(reader, main)
            if shutil.disk_usage(stage).free < level3.size + 1024*1024:
                fail("insufficient_space", "Not enough space for the private RomFS blob")
            digest = hashlib.sha256()
            with (stage/'romfs.bin').open('xb') as target:
                for offset in range(0, level3.size, 1024*1024):
                    data = level3.read(offset, min(1024*1024, level3.size-offset))
                    target.write(data); digest.update(data)
            if digest.hexdigest() != ivfc['level3_sha256']:
                fail("input_changed", "RomFS data changed after integrity verification")
            if reader.digest(0, reader.size) != checked['input_sha256']:
                fail("input_changed", "Input changed during asset preparation")
            after = os.fstat(handle.fileno())
            if (reader.initial_stat.st_size, reader.initial_stat.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                fail("input_changed", "Input metadata changed during asset preparation")
        report = {'ok': True, 'stage': 'prepare-assets', 'schema_version': 1, 'version': __version__,
                  'source_sha256': checked['input_sha256'], 'identity': checked['identity'],
                  'executable_revision': checked['executable_revision'], 'ivfc': ivfc,
                  'romfs_file': {'name': 'romfs.bin', 'bytes': level3.size, 'sha256': digest.hexdigest()},
                  'metadata': metadata, 'signature_authenticity_verified': False,
                  'commercial_profile_enabled': False, 'console_verified': False}
        (stage/'asset-manifest.json').write_text(json.dumps(report, indent=2, ensure_ascii=True)+'\n')
        publish_directory(stage, output)
        return {**report, 'output': str(output)}
    except HostError as exc:
        if exc.stage == 'inspect': exc.stage = 'assets'
        raise
    finally:
        if stage.exists(): shutil.rmtree(stage)
