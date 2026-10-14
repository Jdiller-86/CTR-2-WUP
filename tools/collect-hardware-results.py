#!/usr/bin/env python3
"""Collect only this original diagnostic round. Never reads saves, ROMs or keys."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import stat
import zipfile

VERSION = '0.24.1'
APPS = ('native-software-scene', 'compiled-pica-draws')
MAX_FILE = 4 * 1024 * 1024
MAX_TOTAL = 24 * 1024 * 1024
PATTERN = re.compile(r'^(?:result-)?('+'|'.join(APPS)+r')-'+re.escape(VERSION)+r'-[0-9a-f]{16}-[0-9]{2}\.txt$')


def result_summary(name, data, commit):
    text = data.decode('utf-8', errors='replace')
    summary = {'file': name, 'kind': 'result' if name.startswith('result-') else 'trace'}
    if summary['kind'] == 'trace':
        summary['source_identity_seen'] = f'source_commit={commit} source_dirty=0' in text
        summary['draw_submissions'] = re.findall(r'DRAWN FRAME SUBMITTED[^\r\n]*', text)
        summary['phase_records'] = len(re.findall(r'PHASE name=', text))
        return summary
    summary['bytes'] = len(data)
    summary['source_identity_matches'] = f'Source: {commit} dirty=0' in text
    for key, pattern in (('stage', r'^Stage: ([^\r\n]+)'), ('guide', r'^Guide: ([^\r\n]+)'),
                         ('host_exit', r'^Host exit result: (-?\d+)'),
                         ('guest', r'^Guest: ([^\r\n]+)'), ('draw', r'^Draw submitted: ([^\r\n]+)')):
        found = re.search(pattern, text, re.MULTILINE)
        summary[key] = found.group(1) if found else None
    summary['named_results'] = re.findall(r'^\d+\. [^\r\n]+', text, re.MULTILINE)
    summary['complete_evidence'] = (len(data) == 8192 and summary['source_identity_matches']
        and summary['guide'] is not None and summary['guide'].startswith('COMPLETE')
        and summary['host_exit'] == '0')
    # A report is evidence, never a substitute for the user's display/launcher observations.
    return summary


def collect(sd_root, output, kit, observations=None):
    if kit.get('kind') != 'ctr2wup-hardware-round' or kit.get('version') != VERSION or not re.fullmatch('[0-9a-f]{40}', kit.get('source_commit', '')):
        raise ValueError('Use the matching hardware-round manifest.json beside this collector')
    folder = sd_root / 'ctr2wup'
    if folder.is_symlink() or not folder.is_dir():
        raise ValueError('Select the SD root containing its real ctr2wup directory')
    matches = sorted(p for p in folder.iterdir() if PATTERN.fullmatch(p.name))
    if not matches:
        raise ValueError('No 0.24.1 round logs found. Preserve any on-screen storage error and report that no files were created.')
    if len(matches) > 64:
        raise ValueError('More than 64 round files found; preserve/archive earlier runs before collecting again')
    entries, records, summaries, warnings = {}, [], [], []
    total = 0
    for path in matches:
        before = path.lstat()
        if not stat.S_ISREG(before.st_mode) or path.is_symlink():
            warnings.append(path.name+': not a regular file; excluded');continue
        if before.st_size > MAX_FILE:
            warnings.append(path.name+': exceeds 4 MiB; excluded (keep it separately)');continue
        with path.open('rb') as stream:
            data = stream.read(MAX_FILE+1)
        after = path.lstat()
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) or len(data) != before.st_size:
            raise ValueError('A log changed while being read; close the app and safely reinsert the SD card')
        total += len(data)
        if total > MAX_TOTAL:
            raise ValueError('Round logs exceed 24 MiB; return smaller batches while preserving the originals')
        name = 'logs/'+path.name;entries[name] = data
        records.append({'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        summaries.append(result_summary(path.name, data, kit['source_commit']))
    if not entries:
        raise ValueError('No regular bounded round logs could be collected; preserve the originals')
    report = {'schema_version': 1, 'kind': 'ctr2wup-hardware-return', 'version': VERSION,
              'source_commit': kit['source_commit'], 'collected_at': datetime.now(timezone.utc).isoformat(),
              'observations': observations or {}, 'warnings': warnings, 'files': records,
              'summaries': summaries, 'full_milestone_acceptance': False}
    entries['collection.json'] = (json.dumps(report, indent=2)+'\n').encode()
    # Exclusive output preserves prior returns. No source file is changed or deleted.
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(entries.items()):archive.writestr(name, data)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() or any(archive.read(n) != data for n, data in entries.items()):
            raise ValueError('Return ZIP verification failed; keep the original SD files')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sd-root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--noninteractive', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    kit = json.loads((root/'manifest.json').read_text(encoding='utf-8'))
    selected = str(args.sd_root or input('SD drive/root (for example E:\\): ').strip().strip('"'))
    if re.fullmatch('[A-Za-z]:', selected):selected += '/'
    observations = {}
    if not args.noninteractive:
        print('A blank answer is recorded as not reported. Do not repeat a hung test just to fill this in.')
        for key, prompt in (
            ('environment', 'Firmware / Aroma / launcher versions, if known: '),
            ('arithmetic_minus', 'Arithmetic app: returned to launcher after Minus on both launches? '),
            ('draw_minus', 'Guest Draws: returned to launcher after Minus? '),
            ('home', 'Optional HOME check: not run / returned / suspended / hung / other: '),
            ('problems', 'Any visual, audio, freeze or storage problem (or none)? ')):
            observations[key] = input(prompt).strip()[:512] or 'not reported'
    output = args.output or root/('CTR2WUP-results-'+VERSION+'-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')+'.zip')
    report = collect(Path(selected), output, kit, observations)
    print(f'Created and verified {output.name}: {len(report["files"])} files. Upload this ZIP in the chat.')
    if report['warnings']:print('\n'.join(report['warnings']))
    print('Original SD files were preserved. Incomplete/failure evidence is included for diagnosis.')


if __name__ == '__main__':
    try:main()
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        raise SystemExit('Collection failed: '+str(error))
