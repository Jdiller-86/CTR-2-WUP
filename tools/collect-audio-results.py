#!/usr/bin/env python3
"""Optionally collect source-matched Audio Streams TXT files downloaded by FTP.

Point --sd-root at a local directory containing ctr2wup/. No FTP connection,
console command, save, ROM, key or unrelated file is read. Zipping those two
named TXT files yourself is equally valid; this tool only adds a checked receipt.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import zipfile

VERSION = '0.27.0'
APPLICATION = 'native-audio-streams'
MAX_FILE = 4 * 1024 * 1024
MAX_TOTAL = 24 * 1024 * 1024
MAX_FILES = 64
RESULT_BYTES = 8192
RESULT_PATTERN = re.compile(r'^result-native-audio-streams-0\.26\.0-(?P<session>[0-9a-f]{16})-(?P<attempt>[0-9]{2})\.txt$')
TRACE_PATTERN = re.compile(r'^native-audio-streams-(?P<session>[0-9a-f]{16})-(?P<attempt>[0-9]{2})\.txt$')


def real_path(path: Path) -> Path:
    """Reject symlinks in the selected path, including its parent directories."""
    path = Path(os.path.abspath(path))
    if any(part.is_symlink() for part in (path, *path.parents)):
        raise ValueError('Symlink paths are not accepted; select the real local download directory')
    return path


def bounded_read(path: Path) -> bytes:
    """Read one regular file through the same descriptor whose identity we check."""
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode):
        raise ValueError('not a regular file; excluded (keep it separately)')
    if before.st_size > MAX_FILE:
        raise ValueError('exceeds 4 MiB; excluded (keep it separately)')
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
    descriptor = os.open(path, flags)
    with os.fdopen(descriptor, 'rb') as stream:
        opened = os.fstat(stream.fileno())
        if not stat.S_ISREG(opened.st_mode) or (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino):
            raise RuntimeError('A log was replaced while being opened; finish FTP downloads before collecting')
        data = stream.read(MAX_FILE + 1)
        closed = os.fstat(stream.fileno())
    after = path.lstat()
    fingerprint = lambda row: (row.st_dev, row.st_ino, row.st_size, row.st_mtime_ns, row.st_ctime_ns)
    # Windows path stat and descriptor stat can expose different ctime semantics.
    # Compare timestamps within each API, and bind both observations by the same
    # device/inode/size. A changed path or open file still fails closed.
    identity = lambda row: (row.st_dev, row.st_ino, row.st_size)
    if (fingerprint(before) != fingerprint(after) or fingerprint(opened) != fingerprint(closed)
            or identity(before) != identity(opened) or identity(closed) != identity(after)
            or len(data) != before.st_size):
        raise RuntimeError('A log changed while being read; finish FTP downloads before collecting')
    return data


def unique_line(text: str, expression: str) -> bool:
    return len(re.findall(expression, text, re.MULTILINE)) == 1


def identity_matches(name: str, data: bytes, commit: str) -> bool:
    match = RESULT_PATTERN.fullmatch(name) or TRACE_PATTERN.fullmatch(name)
    if not match:
        return False
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError:
        return False
    if ('\x00' in text or not unique_line(text, r'^Application: [^\r\n]*')
            or not unique_line(text, r'^Application: '+APPLICATION+r' \| Build: '+re.escape(VERSION)+r'\r?$')):
        return False
    if name.startswith('result-'):
        return (len(data) == RESULT_BYTES and text.startswith('CTR2WUP result report\n')
                and unique_line(text, r'^Session ticks: '+match['session']+r'\r?$')
                and unique_line(text, r'^Session ticks: [^\r\n]*')
                and unique_line(text, r'^File: '+re.escape(name)+r'\r?$')
                and unique_line(text, r'^File: [^\r\n]*')
                and unique_line(text, r'^Source: [^\r\n]*')
                and unique_line(text, r'^Source: '+commit+r' dirty=0 \| Evidence schema: 2\r?$'))
    return (text.startswith('CTR2WUP Wii U test report\n')
            and unique_line(text, r'^BUILD source_commit=[^\r\n]*')
            and unique_line(text, r'^BUILD source_commit='+commit+r' source_dirty=0 evidence_schema=2 result_bytes=8192\r?$'))


def result_summary(name: str, data: bytes) -> dict:
    text = data.decode('utf-8')
    kind = 'result' if RESULT_PATTERN.fullmatch(name) else 'trace'
    first = lambda expression: next(iter(re.findall(expression, text, re.MULTILINE)), None)
    summary = {'file': name, 'kind': kind, 'bytes': len(data), 'identity_matches': True,
               'audio_records': re.findall(r'^(?:AUDIO |STREAM )[^\r\n]*', text, re.MULTILINE),
               'native_summary': re.findall(r'^(?:Native audio context:|Stream native:|Stream counters:|Portable render:|Audio sessions:)[^\r\n]*', text, re.MULTILINE),
               'named_results': re.findall(r'^\d+\. [^\r\n]*', text, re.MULTILINE),
               'console_acceptance': False}
    if kind == 'result':
        summary.update(stage=first(r'^Stage: ([^\r\n]+)'), guide=first(r'^Guide: ([^\r\n]+)'),
                       host_exit=first(r'^Host exit result: (-?\d+)'))
        summary['guide_complete'] = bool(summary['guide'] and summary['guide'].startswith('COMPLETE'))
    else:
        summary['cleanup_seen'] = 'END OF REPORT' in text
        summary['main_return_seen'] = re.search(r'EXIT [^\r\n]*main_return', text) is not None
    return summary


def pair_warnings(entries: dict[str, bytes]) -> list[str]:
    warnings, paired = [], set()
    for name, data in sorted(entries.items()):
        match = RESULT_PATTERN.fullmatch(name)
        if not match:
            continue
        default = APPLICATION+'-'+match['session']+'-'+match['attempt']+'.txt'
        hints = re.findall(r'^Full trace: [^\r\n]*? \| file=([^ |\r\n]+)', data.decode('utf-8'), re.MULTILINE)
        expected = default
        if hints:
            candidate = TRACE_PATTERN.fullmatch(hints[0])
            if len(hints) != 1 or not candidate or candidate['session'] != match['session']:
                warnings.append(name+': trace filename/session hint differs; pair not accepted')
                continue
            expected = hints[0]
        if expected not in entries:
            warnings.append(name+': full trace missing; keep this result and download '+expected+' if available')
        elif expected in paired:
            warnings.append(name+': trace is already paired to another result; pair is ambiguous')
        else:
            paired.add(expected)
    for name in sorted(entries):
        if TRACE_PATTERN.fullmatch(name) and name not in paired:
            warnings.append(name+': matching result missing; interrupted trace preserved')
    return warnings


def collect(sd_root: Path, output: Path, kit: dict, observations: dict | None = None) -> dict:
    commit = kit.get('source_commit', '')
    if (kit.get('kind') != 'ctr2wup-audio-round' or kit.get('version') != VERSION
            or not isinstance(commit, str) or not re.fullmatch('[0-9a-f]{40}', commit)
            or kit.get('source_dirty') is not False):
        raise ValueError('Use the clean matching Audio Streams manifest.json supplied with this collector')
    folder = real_path(sd_root) / 'ctr2wup'
    real_path(folder)
    if not folder.is_dir():
        raise ValueError('Select the local SD/FTP download root containing its ctr2wup directory')
    output = real_path(output)
    if output.exists():
        raise FileExistsError('Preserve the existing return ZIP; choose a new output filename')
    paths, ignored = [], 0
    for path in folder.iterdir():
        if RESULT_PATTERN.fullmatch(path.name) or TRACE_PATTERN.fullmatch(path.name):
            paths.append(path)
        else:
            ignored += 1
    if len(paths) > MAX_FILES:
        raise ValueError('More than 64 Audio Streams files; preserve earlier runs and return smaller batches')
    warnings = [f'{ignored} unrelated entries ignored without reading their contents'] if ignored else []
    entries, summaries, total = {}, [], 0
    for path in sorted(paths):
        try:
            data = bounded_read(path)
        except ValueError as error:
            warnings.append(path.name+': '+str(error))
            continue
        if not identity_matches(path.name, data, commit):
            warnings.append(path.name+': filename/header/build/source identity differs or result is not 8192 bytes; excluded (keep it separately)')
            continue
        total += len(data)
        if total > MAX_TOTAL:
            raise ValueError('Audio Streams files exceed 24 MiB; preserve the originals and collect smaller batches')
        entries[path.name] = data
        summaries.append(result_summary(path.name, data))
    if not entries:
        detail = '; '.join(warnings)
        raise ValueError('No bounded source-matched Audio Streams files found. Preserve on-screen/storage errors and original files.'+(' '+detail if detail else ''))
    warnings.extend(pair_warnings(entries))
    observations = observations or {}
    if (not isinstance(observations, dict) or len(observations) > 8
            or any(not isinstance(key, str) or not isinstance(value, str) or len(key) > 64 or len(value) > 512
                   for key, value in observations.items())):
        raise ValueError('Observations must be at most eight short text fields')
    records = [{'path': 'logs/'+name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
               for name, data in sorted(entries.items())]
    report = {'schema_version': 1, 'kind': 'ctr2wup-audio-return', 'version': VERSION,
              'source_commit': commit, 'source_dirty': False, 'collector_version': VERSION,
              'collected_at': datetime.now(timezone.utc).isoformat(), 'files': records,
              'summaries': summaries, 'observations': observations, 'warnings': warnings,
              'guest_dsp_accepted': False, 'full_milestone_acceptance': False}
    payload = {'logs/'+name: data for name, data in entries.items()}
    payload['collection.json'] = (json.dumps(report, indent=2)+'\n').encode('utf-8')
    # Exclusive creation protects previous returns, including dangling symlinks.
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(output) as archive:
        if (archive.testzip() is not None or set(archive.namelist()) != set(payload)
                or any(archive.read(name) != data for name, data in payload.items())):
            raise ValueError('Return ZIP readback failed; preserve the original local TXT files')
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sd-root', type=Path, required=True, help='Local folder containing ctr2wup/ after FTP download')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).resolve().with_name('manifest.json'))
    args = parser.parse_args()
    kit = json.loads(args.manifest.read_text(encoding='utf-8'))
    output = args.output or Path.cwd()/('CTR2WUP-audio-results-'+VERSION+'-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')+'.zip')
    report = collect(args.sd_root, output, kit)
    print(f'Created and verified {output}: {len(report["files"])} source-matched TXT files. Original files preserved.')
    for warning in report['warnings']:
        print('Warning: '+warning)
    print('Upload this ZIP with your listening and launcher-return observations. Manual FTP TXT ZIPs are also accepted.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        raise SystemExit('Collection failed: '+str(error))
