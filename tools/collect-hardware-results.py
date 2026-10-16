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

VERSION = '0.27.0'
SUPPORTED_VERSIONS = {'0.24.1', '0.25.0', VERSION}
SOFTWARE_APPS = ('native-software-scene', 'compiled-pica-draws')
GX2_APP = 'compiled-pica-gx2'
TEXTURED_APP = 'compiled-pica-textured'
GPU_APPS = (GX2_APP, TEXTURED_APP)
APPS = SOFTWARE_APPS + GPU_APPS
MAX_FILE = 4 * 1024 * 1024
MAX_TOTAL = 24 * 1024 * 1024
SESSION_PATTERN = r'[0-9a-f]{16}-[0-9]{2}\.txt'
def patterns(version):
    if version not in SUPPORTED_VERSIONS:raise ValueError('Unsupported hardware-round version')
    escaped = re.escape(version)
    app_pattern = r'(?:'+'|'.join(APPS if version == VERSION else SOFTWARE_APPS)+r')'
    result = re.compile(r'^result-(?P<app>'+app_pattern+')-'+escaped+r'-(?P<session>[0-9a-f]{16})-(?P<attempt>[0-9]{2})\.txt$')
    # Native trace filenames omit the version; their headers must match the kit.
    logs = re.compile(r'^(?:result-'+app_pattern+'-'+escaped+'-|'+app_pattern+r'-(?:'+escaped+'-)?'+r')'+SESSION_PATTERN+'$')
    return result, logs


RESULT_PATTERN, PATTERN = patterns(VERSION)


def trace_identity_matches(name, data, commit, version=VERSION):
    app = next((app for app in APPS if name.startswith(app+'-')), None)
    text = data.decode('utf-8', errors='replace')
    return (app is not None and (app not in GPU_APPS or version == VERSION) and text.startswith('CTR2WUP Wii U test report\n')
        and re.search(r'^Application: '+re.escape(app)+r' \| Build: '+re.escape(version)+r'\r?$', text, re.MULTILINE) is not None
        and re.search(r'^BUILD source_commit='+re.escape(commit)+r' source_dirty=0(?: |\r?$)', text, re.MULTILINE) is not None)


def missing_trace_warnings(entries, version=VERSION):
    result_pattern, pattern = patterns(version)
    entries = {name.removeprefix('logs/'): data for name, data in entries.items()}
    warnings = []
    for name in sorted(entries):
        match = result_pattern.fullmatch(name)
        if not match:continue
        versioned = name.removeprefix('result-')
        actual = versioned.replace('-'+version+'-', '-', 1)
        candidates = {actual, versioned}
        # Result and trace writers resolve filename collisions independently.
        hint = re.search(r'^Full trace: [^\r\n]*? \| file=([^ |\r\n]+)', entries[name].decode('utf-8', errors='replace'), re.MULTILINE)
        if hint and pattern.fullmatch(hint[1]) and any(hint[1].startswith(prefix) for prefix in (
            match['app']+'-'+match['session']+'-', match['app']+'-'+version+'-'+match['session']+'-')):
            actual = hint[1];candidates = {actual}
        if not candidates.intersection(entries):
            warnings.append(actual+': full trace missing; keep the result and download this trace if available')
    return warnings


def gx2_evidence(text, textured=False):
    """Keep GPU execution counters distinct from the CPU parity reference."""
    lines = re.findall(r'GX2 backend: ([^\r\n;]+); CPU preflight and full GPU readback parity', text)
    backend = {}
    if lines:
        pairs = re.findall(r'([a-z_]+)=([a-z0-9_-]+)', lines[-1])
        fields = dict(pairs)
        counters = ('begins', 'draws', 'submissions', 'readbacks', 'shader_ops', 'waits',
                    'wait_failures', 'allocations', 'releases', 'quarantine', 'backend_stage')
        texture_counters = ('texture_uploads', 'texture_bytes')
        required = {'result', 'shader', *counters}
        # Older source-bound GX2 reports predate texture counters. A partial
        # pair cannot establish either the old or the new native contract.
        numeric = counters + tuple(k for k in texture_counters if k in fields)
        if (len(pairs) == len(fields) and set(fields) in (required, required | set(texture_counters))
                and all(fields[k].isdigit() for k in numeric)):
            backend = {k: int(v) if k in numeric else v for k, v in fields.items()}
    references = re.findall(r'Guest GX2 draws \(CPU parity reference\): draws=(\d+) triangles=(\d+) '
                          r'reference_fragments=(\d+) reference_shader_steps=(\d+) '
                          r'scene_error=(\d+) vertex_error=(\d+) shader_error=(\d+); '
                          r'translated GPU shaders \+ exact readback guard', text)
    reference = references[-1] if references else None
    errors = tuple(map(int, reference[4:])) if reference else None
    reference_counters = dict(zip(('draws', 'triangles', 'reference_fragments', 'reference_shader_steps',
                                  'scene_error', 'vertex_error', 'shader_error'),
                                 map(int, reference))) if reference else None
    failures = bool(backend and (backend['result'] != 'ok' or backend['shader'] != 'ok'
                    or backend['wait_failures'] or backend['quarantine'] or backend['backend_stage'] == 4))
    failures = failures or bool(errors and any(errors))
    complete = bool(backend and reference and not failures and backend['backend_stage'] == 0
        and backend['draws'] >= 4 and backend['submissions'] >= backend['draws']
        and backend['readbacks'] > 0 and backend['begins'] >= backend['readbacks']
        and backend['waits'] >= backend['readbacks'] and backend['shader_ops'] > 0
        and backend['allocations'] == backend['releases'] and backend['allocations'] > 0
        and (not textured or (backend.get('texture_uploads', 0) > 0 and backend.get('texture_bytes', 0) > 0))
        and reference_counters['draws'] >= 4 and reference_counters['triangles'] >= 4
        and reference_counters['reference_fragments'] > 0 and reference_counters['reference_shader_steps'] > 0)
    stage = backend.get('backend_stage', -1)
    stages = ('none', 'begin', 'draw', 'end', 'parity')
    return {'gx2_backend': backend or None, 'software_reference': reference_counters,
            'gpu_backend_stage': stages[stage] if 0 <= stage < len(stages) else 'unknown',
            'gpu_readback_evidence': 'failed' if failures else 'passed' if complete else 'incomplete',
            'gpu_failure': failures}

def result_summary(name, data, commit, version=VERSION):
    text = data.decode('utf-8', errors='replace')
    summary = {'file': name, 'kind': 'result' if name.startswith('result-') else 'trace'}
    app = next((app for app in APPS if name.startswith(app+'-') or name.startswith('result-'+app+'-')), None)
    summary['application'] = app
    summary['render_backend'] = 'gx2' if app in GPU_APPS else 'software'
    summary['textured'] = app == TEXTURED_APP
    if app in GPU_APPS:summary.update(gx2_evidence(text, app == TEXTURED_APP))
    if summary['kind'] == 'trace':
        summary['source_identity_seen'] = trace_identity_matches(name, data, commit, version)
        summary['draw_submissions'] = re.findall(r'DRAWN FRAME SUBMITTED[^\r\n]*', text)
        summary['phase_records'] = len(re.findall(r'PHASE name=', text))
        summary['publication_records'] = re.findall(r'PUBLISH display=[^\r\n]*', text)
        return summary
    summary['bytes'] = len(data)
    summary['source_identity_matches'] = f'Source: {commit} dirty=0' in text
    for key, pattern in (('stage', r'^Stage: ([^\r\n]+)'), ('guide', r'^Guide: ([^\r\n]+)'),
                         ('host_exit', r'^Host exit result: (-?\d+)'),
                         ('guest', r'^Guest: ([^\r\n]+)'), ('draw', r'^Draw submitted: ([^\r\n]+)')):
        found = re.search(pattern, text, re.MULTILINE)
        summary[key] = found.group(1) if found else None
    summary['scale_cache'] = re.findall(r'^Scale cache [^\r\n]+', text, re.MULTILINE)
    summary['named_results'] = re.findall(r'^\d+\. [^\r\n]+', text, re.MULTILINE)
    summary['complete_evidence'] = (len(data) == 8192 and summary['source_identity_matches']
        and summary['guide'] is not None and summary['guide'].startswith('COMPLETE')
        and summary['host_exit'] == '0')
    if app in GPU_APPS:
        identity = (text.startswith('CTR2WUP result report\n')
            and re.search(r'^Application: '+re.escape(app)+r' \| Build: '+re.escape(version)+r'\r?$', text, re.MULTILINE) is not None
            and re.search(r'^File: '+re.escape(name)+r'\r?$', text, re.MULTILINE) is not None
            and re.search(r'^Source: '+re.escape(commit)+r' dirty=0 \| Evidence schema: 2\r?$', text, re.MULTILINE) is not None)
        summary['source_identity_matches'] = identity
        guide = re.fullmatch(r'COMPLETE \| passed=2 failed=0 skipped=0(?: current_step=\d+)?', summary['guide'] or '')
        expected_results = (('1. Textured guest pictures: PASS | ', '2. Texture readback and guest exit: PASS | ')
                            if app == TEXTURED_APP else
                            ('1. GX2 guest triangle pictures: PASS | ', '2. GPU readback and guest exit: PASS | '))
        named = summary['named_results']
        guest = re.search(r'^Guest: state=exited [^\r\n]* fallback=0 fault=ok(?: |\r?$)', text, re.MULTILINE) is not None
        draw = re.match(r'1 serial=[1-9]\d* ', summary['draw'] or '') is not None
        summary['complete_evidence'] = bool(summary['complete_evidence'] and identity and guide and guest and draw
            and len(named) == 2 and all(row.startswith(prefix) for row, prefix in zip(named, expected_results))
            and summary['gpu_readback_evidence'] == 'passed')
    # A report is evidence, never a substitute for the user's display/launcher observations.
    return summary


def collect(sd_root, output, kit, observations=None):
    version = kit.get('version')
    if kit.get('kind') != 'ctr2wup-hardware-round' or version not in SUPPORTED_VERSIONS or not re.fullmatch('[0-9a-f]{40}', kit.get('source_commit', '')):
        raise ValueError('Use the matching hardware-round manifest.json beside this collector')
    _, pattern = patterns(version)
    folder = sd_root / 'ctr2wup'
    if folder.is_symlink() or not folder.is_dir():
        raise ValueError('Select the SD root or local FTP download root containing its real ctr2wup directory')
    folders = [folder]
    results = folder / 'results'
    if results.is_symlink() or (results.exists() and not results.is_dir()):
        raise ValueError('Select the real downloaded results directory, not a link or regular file')
    if results.is_dir():
        folders.append(results)
    matches = sorted(p for location in folders for p in location.iterdir() if pattern.fullmatch(p.name))
    if len({p.name for p in matches}) != len(matches):
        raise ValueError('Duplicate log filename across legacy/results directories; preserve both originals')
    if not matches:
        raise ValueError(f'No {version} round logs found. Preserve any on-screen storage error and report that no files were created.')
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
            raise ValueError('A log changed while being read; close the app and finish FTP downloads or safely reinsert the SD card')
        if not path.name.startswith('result-') and not trace_identity_matches(path.name, data, kit['source_commit'], version):
            warnings.append(path.name+': trace build/source identity differs or is incomplete; excluded (keep it separately)');continue
        total += len(data)
        if total > MAX_TOTAL:
            raise ValueError('Round logs exceed 24 MiB; return smaller batches while preserving the originals')
        name = 'logs/'+path.name;entries[name] = data
        records.append({'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        summaries.append(result_summary(path.name, data, kit['source_commit'], version))
    if not entries:
        raise ValueError('No regular bounded round logs could be collected; preserve the originals')
    warnings.extend(missing_trace_warnings(entries, version))
    report = {'schema_version': 1, 'kind': 'ctr2wup-hardware-return', 'version': version, 'collector_version': VERSION,
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
    selected = str(args.sd_root or input('SD root or local FTP download root (parent of ctr2wup): ').strip().strip('"'))
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
    output = args.output or root/('CTR2WUP-results-'+kit['version']+'-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')+'.zip')
    report = collect(Path(selected), output, kit, observations)
    print(f'Created and verified {output.name}: {len(report["files"])} files. Upload this ZIP in the chat.')
    if report['warnings']:print('\n'.join(report['warnings']))
    print('Original SD files were preserved. Incomplete/failure evidence is included for diagnosis.')


if __name__ == '__main__':
    try:main()
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        raise SystemExit('Collection failed: '+str(error))
