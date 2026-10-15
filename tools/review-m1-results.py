#!/usr/bin/env python3
"""Review original M1 console evidence; produce a candidate, never accept a gate.

Reads only allowlisted TXT reports/traces from a completed local FTP copy. It
does not read keys, saves, packages, or contact a console/GitHub. Observations
are explicit tester statements, not measurements inferred from cleanup logs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import stat

MAX_FILE = 4 * 1024 * 1024
MAX_TOTAL = 24 * 1024 * 1024
MAX_RESULTS = 96
NATIVE_STEPS = (
    'Picture check', 'Buttons', 'Left stick', 'Right stick', 'Touch positions',
    'Touch border rejection', 'Touch alignment', 'Sound check',
    'Ten-minute stability', 'Stability observation',
)
EXIT_STAGES = (
    'guest_cleanup_begin', 'guest_cleanup_done', 'audio_cleanup_begin',
    'audio_cleanup_done', 'graphics_cleanup_begin', 'graphics_cleanup_done',
    'report_close_done', 'resources_closed_before_procui_shutdown',
    'callbacks_cleared', 'procui_shutdown_begin', 'procui_shutdown_done',
    'whb_proc_shutdown_begin', 'whb_proc_shutdown_done', 'main_return',
)
ROLES = ('native-full', 'native-repeat', 'native-home', 'installed-first', 'installed-repeat')
LAUNCHER_TITLES = {'0005000013374842', '000500101004a000', '000500101004a100', '000500101004a200'}
HEX40 = re.compile(r'[0-9a-f]{40}')
HEX64 = re.compile(r'[0-9a-f]{64}')


def no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON field')
        result[key] = value
    return result


def read_json(path):
    return json.loads(read_regular(path, 1024 * 1024).decode('utf-8'), object_pairs_hook=no_duplicate_keys)


def read_regular(path, limit=MAX_FILE):
    before = path.lstat()
    if path.is_symlink() or not stat.S_ISREG(before.st_mode) or before.st_size > limit:
        raise ValueError('Evidence must be a bounded regular file')
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    after = path.lstat()
    if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
            != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns)
            or len(data) != before.st_size or len(data) > limit):
        raise ValueError('Evidence changed during review; finish FTP downloads first')
    return data


def one(text, pattern):
    values = re.findall(pattern, text, re.MULTILINE)
    return values[0] if len(values) == 1 else None


def merge_status(checks):
    statuses = {row['status'] for row in checks}
    return next((value for value in ('FAIL', 'BLOCKED', 'INCOMPLETE') if value in statuses), 'PASS')


def check(identifier, status, detail, files=()):
    return {'id': identifier, 'status': status, 'detail': detail, 'files': list(files)}


def manual(identifier, value, detail):
    return check(identifier, 'PASS' if value is True else 'FAIL' if value is False else 'INCOMPLETE', detail)


def validate_manifest(manifest, expected_source):
    if not HEX40.fullmatch(expected_source):
        raise ValueError('Require the independently recorded tested source commit')
    if (manifest.get('kind') != 'ctr2wup-m1-acceptance' or manifest.get('schema_version') != 1
            or manifest.get('source_commit') != expected_source or manifest.get('source_dirty') is not False
            or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', manifest.get('version', ''))
            or not re.fullmatch(r'[A-Za-z0-9._-]{1,80}', manifest.get('suite_id', ''))
            or not re.fullmatch(r'[a-z0-9-]{1,48}', manifest.get('native_app', 'native'))):
        raise ValueError('Use the clean source-matched M1 suite manifest')
    return manifest['version'], manifest.get('native_app', 'native')


def parse_run(folder, path, match, manifest):
    data = read_regular(path)
    text = data.decode('utf-8', errors='strict')
    source, version, app, session = manifest['source_commit'], manifest['version'], match['app'], match['session']
    result = {'result': path.name, 'application': app, 'session': session,
              'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data), 'errors': [],
              'incomplete': [], 'guide': {}, 'storage': [], 'trace': None, 'trace_text': ''}
    if not text.startswith('CTR2WUP result report\n'):
        result['errors'].append('result report banner missing')
    expected = (
        (one(text, r'^Application: ([^\r\n]+)\r?$'), app + ' | Build: ' + version, 'application/build identity'),
        (one(text, r'^Session ticks: ([0-9a-f]{16})\r?$'), session, 'session identity'),
        (one(text, r'^File: ([^\r\n]+)\r?$'), path.name, 'result filename identity'),
        (one(text, r'^Source: ([^\r\n]+)\r?$'), source + ' dirty=0 | Evidence schema: 2', 'clean source identity'),
    )
    for actual, wanted, label in expected:
        if actual != wanted:
            result['errors'].append(label + ' differs or is ambiguous')
    if len(data) != 8192:
        result['errors'].append('result is not exactly 8192 bytes')
    if one(text, r'^Stage: ([^\r\n]+)\r?$') != 'resources_closed_before_system_transition':
        result['incomplete'].append('final result cleanup checkpoint missing')
    exit_result = one(text, r'^Host exit result: (-?\d+)(?:[^\r\n]*)$')
    if exit_result is None or exit_result == '-1':
        result['incomplete'].append('host exit result missing or still running')
    elif exit_result != '0':
        result['errors'].append('nonzero host exit result')
    writes = one(text, r'^Previous result writes verified: (\d+)\r?$')
    if writes is None or int(writes) == 0:
        result['incomplete'].append('no prior closed-file result read-back verified')
    input_errors = one(text, r'^Input: samples=\d+ errors=(\d+)(?:[^\r\n]*)$')
    if input_errors is None:
        result['incomplete'].append('input error counter missing')
    elif input_errors != '0':
        result['errors'].append('input sampling errors observed')
    summary = one(text, r'^Guide: (COMPLETE|RUNNING|CANCELLED|NOT_STARTED) \| passed=(\d+) failed=(\d+) skipped=(\d+)(?:[^\r\n]*)$')
    result['guide_state'] = summary[0] if summary else None
    result['guide_totals'] = tuple(int(value) for value in summary[1:]) if summary else None
    if summary and int(summary[2]):
        result['errors'].append('guide contains a reported failure')
    selftests = one(text, r'^Self-tests finished: (\d+) \| ([^\r\n]+)\r?$')
    if not selftests or selftests[0] != '1':
        result['incomplete'].append('native self-tests did not complete')
    elif not re.fullmatch(r'CPU=0 services=0 heap=0 objects=0 HID=0 GSP=0 relay=0 stack=0 threads=0', selftests[1]):
        result['errors'].append('native self-test failure')
    for number, name, outcome in re.findall(r'^(\d+)\. ([^\r\n:]+): (PASS|FAIL|NOT TESTED) \| [^\r\n]*', text, re.MULTILINE):
        if name in result['guide']:
            result['errors'].append('duplicate named guide outcome')
        result['guide'][name] = {'number': int(number), 'status': outcome}
        if outcome == 'FAIL':
            result['errors'].append('failed guide step: ' + name)
    hint = one(text, r'^Full trace: ([^\r\n]*)$')
    trace_match = re.fullmatch(r'SAVING \| file=(' + re.escape(app) + '-' + session + r'-[0-9]{2}\.txt) errno=0(?:[^\r\n]*)', hint or '')
    if not trace_match:
        if hint and ('FAILED' in hint or re.search(r'errno=[1-9]', hint)):
            result['errors'].append('full trace storage failure reported')
        else:
            result['incomplete'].append('valid paired trace reference missing')
        return result
    trace_name = trace_match[1]
    trace_path = folder / trace_name
    if not trace_path.exists():
        result['incomplete'].append('paired full trace missing: ' + trace_name)
        return result
    trace_data = read_regular(trace_path)
    trace = trace_data.decode('utf-8', errors='strict')
    result['trace'], result['trace_text'] = trace_name, trace
    result['trace_sha256'] = hashlib.sha256(trace_data).hexdigest()
    result['trace_bytes'] = len(trace_data)
    wanted = app + ' | Build: ' + version
    headers = re.findall(r'^Application: ([^\r\n]+)\r?$', trace, re.MULTILINE)
    if headers.count(wanted) != 1 or any(value != wanted and not value.startswith(wanted + ' | Entering cleanup: result=') for value in headers):
        result['errors'].append('trace application/build identity differs or is ambiguous')
    build = re.findall(r'^BUILD source_commit=([^ ]+) source_dirty=(\d+) evidence_schema=(\d+) result_bytes=(\d+)\r?$', trace, re.MULTILINE)
    if build != [(source, '0', '2', '8192')]:
        result['errors'].append('trace clean source identity differs or is ambiguous')
    session_identity = one(trace, r'^M1_SESSION ticks=([0-9a-f]{16}) source_commit=([0-9a-f]{40}) version=([^ ]+) protocol=m1-v1\r?$')
    if session_identity != (session, source, version):
        result['errors'].append('trace body process session differs or is missing/ambiguous')
    previous = -1
    for stage in EXIT_STAGES:
        positions = [m.start() for m in re.finditer(r'^EXIT stage=' + stage + r'(?:;[^\r\n]*)?\r?$', trace, re.MULTILINE)]
        if len(positions) != 1 or positions[0] <= previous:
            result['incomplete'].append('ordered complete lifecycle missing: ' + stage)
            break
        previous = positions[0]
    end_marker = '===== END OF REPORT: application resource cleanup reached; result=0 ====='
    if trace.count(end_marker) != 1:
        result['incomplete'].append('successful END OF REPORT missing')
    elif not trace.find('EXIT stage=graphics_cleanup_done') < trace.find(end_marker) < trace.find('EXIT stage=report_close_done'):
        result['incomplete'].append('END OF REPORT not between resource cleanup and report close')
    if re.search(r'(?:CTR2WUP (?:RESULT|exit checkpoint) FAILED|REPORT COMMIT FAILED)', trace):
        result['errors'].append('report publication error observed')
    if re.search(r'^audio FAIL |^Native audio begin: result=(?:RESET_FAILED|INIT_FAILED|QUIT_FAILED)', trace, re.MULTILINE):
        result['errors'].append('native audio initialization/configuration failed')
    if re.search(r'^Guest fault observed during session: YES\r?$', trace, re.MULTILINE):
        result['errors'].append('native guest diagnostic fault observed')
    if re.search(r'^GUIDE RESULT: [^\r\n]*result=FAIL(?: |$)', trace, re.MULTILINE):
        result['errors'].append('trace records a guide failure')
    result['storage'] = re.findall(r'^M1_STORAGE ([^\r\n]+)\r?$', trace, re.MULTILINE)
    platform = one(trace, r'^Platform title_id=([0-9a-f]{16}) exit_route=(HBL_relaunch|system_menu) SD_owner=WUT_devoptab\r?$')
    result['title_id'] = platform[0] if platform else None
    result['exit_route'] = platform[1] if platform else None
    return result


def run_check(role, row, observations, complete=False):
    if row is None:
        return check(role, 'INCOMPLETE', 'Select the matching returned result filename for this fresh launch.')
    reasons = row['errors'] + row['incomplete']
    if row['errors']:
        status = 'FAIL'
    elif row['incomplete']:
        status = 'INCOMPLETE'
    else:
        status = 'PASS'
    if complete and (row['guide_state'] != 'COMPLETE' or row['guide_totals'] != (10, 0, 0) or len(row['guide']) != 10
            or any(row['guide'].get(name) != {'number': i, 'status': 'PASS'} for i, name in enumerate(NATIVE_STEPS, 1))):
        status = 'FAIL' if row['errors'] else 'INCOMPLETE'
        reasons.append('all ten native guide steps must pass without skips')
    if complete:
        trace_outcomes = re.findall(r'^GUIDE RESULT: step=(\d+) name=([^\r\n]+?) result=(PASS|FAIL|NOT TESTED) elapsed_ms=(\d+)(?: |$)', row['trace_text'], re.MULTILINE)
        expected = [(str(i), name, 'PASS') for i, name in enumerate(NATIVE_STEPS, 1)]
        if [values[:3] for values in trace_outcomes] != expected:
            if status != 'FAIL':status = 'INCOMPLETE'
            reasons.append('trace must corroborate every native guide outcome in order')
    user = observations.get(role, {})
    if not isinstance(user, dict):
        raise ValueError('Each run observation must be an object')
    if user.get('no_unexplained_problem') is False:
        status = 'FAIL';reasons.append('tester reported an unresolved problem')
    elif user.get('no_unexplained_problem') is not True and status == 'PASS':
        status = 'INCOMPLETE';reasons.append('tester problem/no-problem observation missing')
    if user.get('returned_ok') is False:
        status = 'FAIL';reasons.append('tester reported failure to return')
    elif user.get('returned_ok') is not True and status == 'PASS':
        status = 'INCOMPLETE';reasons.append('tester must confirm actual launcher/Menu return')
    route = 'home' if role == 'native-home' else 'minus'
    if user.get('exit_control') != route and status == 'PASS':
        status = 'INCOMPLETE';reasons.append('record the requested ' + route + ' exit control')
    trace = row.get('trace_text', '')
    minus_seen = re.search(r'^EXIT trigger=Minus; cleaning resources before system transition\r?$', trace, re.MULTILINE) is not None
    home_seen = (re.search(r'^EXIT trigger=HBL_home_button\r?$', trace, re.MULTILINE) is not None
                 or re.search(r'^APP ProcUI status=3\r?$', trace, re.MULTILINE) is not None)
    if status == 'PASS' and (route == 'minus' and not minus_seen or route == 'home' and (not home_seen or minus_seen)):
        status = 'INCOMPLETE';reasons.append('trace must corroborate the requested exit route')
    return check(role, status, '; '.join(reasons) if reasons else 'Source/session-matched result, read-back, cleanup and tester return verified.', (row['result'], row['trace']) if row['trace'] else (row['result'],))


def storage_checks(manifest, runs):
    config = manifest.get('recovery', {})
    if config.get('available') is not True or config.get('protocol') != 'scratch-v1':
        return [check('sd-recovery', 'BLOCKED', 'This kit has no source-bound original SD recovery probe; ordinary successful writes cannot prove recovery.')]
    rows = [runs.get(role) for role in ('native-full', 'native-repeat')]
    checks = []
    required = {'protocol': 'scratch-v1', 'sentinel_written': '1', 'sentinel_readback': '1',
                'previous_recovered': '1', 'pending_rejected': '1', 'invalid_path_rejected': '1',
                'final_readback': '1', 'result': 'PASS'}
    for role, row in zip(('native-full', 'native-repeat'), rows):
        if row is None or len(row['storage']) != 1:
            checks.append(check(role + '-sd', 'INCOMPLETE', 'One unambiguous M1_STORAGE probe receipt is required.'));continue
        parts = row['storage'][0].split()
        if any('=' not in part for part in parts):
            checks.append(check(role + '-sd', 'FAIL', 'Malformed storage receipt.'));continue
        values = [part.split('=', 1) for part in parts]
        fields = dict(values)
        if len(fields) != len(values) or any(fields.get(key) != value for key, value in required.items()):
            checks.append(check(role + '-sd', 'FAIL', 'Recovery/error-path probe did not establish every required check.'));continue
        status = 'PASS'
        if fields.get('fresh_reload') not in ('0', '1') or (role == 'native-repeat' and fields['fresh_reload'] != '1'):
            status = 'INCOMPLETE'
        checks.append(check(role + '-sd', status, 'Scratch snapshot bytes, previous recovery, stale pending rejection, deliberate invalid path and valid retry; repeat must reload an earlier process sentinel.', (row['trace'],)))
    return checks


def review(folder, manifest, observations, expected_source):
    version, native_app = validate_manifest(manifest, expected_source)
    if not isinstance(observations, dict) or observations.get('suite_id') != manifest['suite_id']:
        raise ValueError('Observations must identify the exact suite_id')
    if folder.is_symlink() or not folder.is_dir():
        raise ValueError('Choose the completed local FTP ctr2wup directory')
    if not isinstance(observations.get('runs', {}), dict) or not isinstance(observations.get('environment', {}), dict):
        raise ValueError('Runs/environment must be objects')
    pattern = re.compile(r'^result-(?P<app>' + re.escape(native_app) + ')-' + re.escape(version)
                         + r'-(?P<session>[0-9a-f]{16})-(?P<attempt>[0-9]{2})\.txt$')
    paths = sorted(path for path in folder.iterdir() if pattern.fullmatch(path.name))
    if len(paths) > MAX_RESULTS:
        raise ValueError('Too many matching results; preserve originals and review smaller batches')
    parsed, total = {}, 0
    for path in paths:
        row = parse_run(folder, path, pattern.fullmatch(path.name), manifest)
        total += row['bytes'] + row.get('trace_bytes', 0)
        if total > MAX_TOTAL:
            raise ValueError('Evidence exceeds the 24 MiB review budget')
        parsed[path.name] = row
    runs, selection = {}, observations.get('runs', {})
    for role in ROLES:
        obs = selection.get(role, {})
        if not isinstance(obs, dict):
            raise ValueError('Each run observation must be an object')
        name = obs.get('result')
        if name is not None and (not isinstance(name, str) or not pattern.fullmatch(name)):
            raise ValueError('Run selection must use an allowlisted result basename')
        runs[role] = parsed.get(name)
    selected = [row for row in runs.values() if row]
    sessions = [row['session'] for row in selected]
    identity = check('distinct-launches', 'PASS' if len(sessions) == len(set(sessions)) else 'FAIL',
                     'Each required launch must have a distinct native process session; Plus is not a fresh launch.')
    environment = observations.get('environment', {})
    required_environment = ('firmware', 'aroma', 'launcher', 'launcher_version', 'sd_format', 'sd_capacity', 'tv_mode')
    missing = [key for key in required_environment if not isinstance(environment.get(key), str) or not environment[key].strip() or len(environment[key]) > 160]
    env_check = check('environment', 'INCOMPLETE' if missing else 'PASS', 'Record exact environment: ' + ', '.join(missing) if missing else 'Exact firmware/Aroma/launch route/SD/display recorded.')
    full = run_check('native-full', runs['native-full'], selection, complete=True)
    collection = run_check('native-full-publication', runs['native-full'], {'native-full-publication': selection.get('native-full', {})})
    repeat = run_check('native-repeat', runs['native-repeat'], selection)
    home = run_check('native-home', runs['native-home'], selection)
    picture = observations.get('native_observations', {})
    if not isinstance(picture, dict):
        raise ValueError('Native observations must be an object')
    controls = [full]
    for field, label in (('tv_correct', 'TV picture'), ('gamepad_correct', 'GamePad picture'),
                         ('touch_aligned', 'touch marker alignment'), ('tone_tv', 'tone heard on TV'),
                         ('tone_gamepad', 'tone heard on GamePad'), ('launch_music_stopped', 'launch music stopped'),
                         ('stability_no_problems', 'no problem during the timed ten-minute session')):
        controls.append(manual(field, picture.get(field), 'Tester explicitly confirms ' + label + '.'))
    stability = runs['native-full']
    elapsed = [] if stability is None else re.findall(r'^GUIDE RESULT: step=9 name=Ten-minute stability result=PASS elapsed_ms=(\d+)(?: |$)', stability['trace_text'], re.MULTILINE)
    controls.append(check('ten-minute-clock', 'PASS' if len(elapsed) == 1 and int(elapsed[0]) >= 600000 else 'INCOMPLETE', 'Trace must measure at least 600000 ms in the actual stability step.'))
    trace = '' if stability is None else stability['trace_text']
    tone_ready = (re.search(r'^Native audio begin: result=READY owned=1 [^\r\n]*failures=0', trace, re.MULTILINE)
                  and re.search(r'^audio configured=1 TV_result=0 DRC_result=0 rate=48000\r?$', trace, re.MULTILINE)
                  and re.search(r'^tone request=[1-9][0-9]* frequency_hz=440 duration_ms=250\r?$', trace, re.MULTILINE))
    controls.append(check('native-tone-initialized', 'PASS' if tone_ready else 'INCOMPLETE', 'Native AX context and requested original tone must be recorded; audible output remains tester evidence.'))
    lifecycle = [identity, repeat, home] + storage_checks(manifest, runs)
    package = manifest.get('package', {})
    if not isinstance(package, dict):
        raise ValueError('Package manifest must be an object')
    installed = []
    if package.get('available') is not True:
        installed.append(check('original-installed-wup', 'BLOCKED', 'No install-test package exists in this kit. Real local packaging material/provenance, explicit collision-checked title identity and verified original WUP are required. RPX/WUHB cannot satisfy Menu install/launch.'))
    else:
        checks = (package.get('source_commit') == expected_source,
                  isinstance(package.get('title_id'), str) and re.fullmatch(r'00050000[0-9a-f]{8}', package['title_id']) is not None,
                  package.get('title_id') not in LAUNCHER_TITLES | {'0005000000000000'},
                  isinstance(package.get('sha256'), str) and HEX64.fullmatch(package['sha256']) is not None,
                  package.get('integrity_verified') is True, package.get('identity_collision_checked') is True,
                  package.get('provenance_reviewed') is True, package.get('synthetic_credentials') is False)
        installed.append(check('package-identity', 'PASS' if all(checks) else 'BLOCKED', 'Require a reviewed real source-matched original WUP report; structural integrity does not prove installer acceptance.'))
        for role in ('installed-first', 'installed-repeat'):
            installed.append(run_check(role, runs[role], selection))
            obs = selection.get(role, {})
            installed.append(manual(role + '-menu', obs.get('launched_from_menu'), 'Tester launches this installed title from the Wii U Menu.'))
            row = runs[role]
            installed.append(check(role + '-title', 'PASS' if row and row.get('title_id') == package.get('title_id') and row.get('exit_route') == 'system_menu' else 'INCOMPLETE', 'Runtime platform title ID and system-Menu exit route must match the verified WUP identity.'))
        for field in ('installer', 'installer_version', 'installation_target'):
            installed.append(check(field, 'PASS' if isinstance(environment.get(field), str) and environment[field].strip() else 'INCOMPLETE', 'Record exact ' + field + '.'))
        installation = observations.get('installation', {})
        if not isinstance(installation, dict):
            raise ValueError('Installation observations must be an object')
        for field in ('installation_succeeded', 'title_identity_correct', 'installed_both_screens_correct'):
            installed.append(manual(field, installation.get(field), 'Tester explicitly confirms ' + field.replace('_', ' ') + '.'))
    assigned = {row['result'] for row in selected}
    attempts = observations.get('reviewed_attempts', {})
    if not isinstance(attempts, dict):
        raise ValueError('Reviewed attempts must be an object of result basenames and explanations')
    unreviewed = [name for name in parsed if name not in assigned and not (isinstance(attempts.get(name), str) and attempts[name].strip())]
    coverage = check('all-attempts-reviewed', 'INCOMPLETE' if unreviewed else 'PASS',
                     'Unassigned current-source attempts need explicit review, retaining failures: ' + ', '.join(unreviewed) if unreviewed else 'All returned same-build attempts are assigned or explicitly reviewed.')
    groups = [
        {'id': 'm1-results', 'checks': [collection, coverage]},
        {'id': 'm1-lifecycle', 'checks': [full] + lifecycle},
        {'id': 'm1-controls-audio', 'checks': controls},
        {'id': 'm1-package-launch', 'checks': installed},
    ]
    for group in groups:
        group['status'] = merge_status(group['checks'])
    acceptance = {'id': 'm1-platform-acceptance', 'checks': [env_check] + [check(group['id'], group['status'], 'Complete reviewed evidence required.') for group in groups]}
    acceptance['status'] = merge_status(acceptance['checks']);groups.append(acceptance)
    inventory = [{key: value for key, value in row.items() if key != 'trace_text'} for row in parsed.values()]
    return {'schema_version': 1, 'kind': 'ctr2wup-m1-review', 'suite_id': manifest['suite_id'],
            'source_commit': expected_source, 'version': version,
            'manifest_sha256': hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
            'status': acceptance['status'], 'candidate_complete': acceptance['status'] == 'PASS',
            'full_milestone_acceptance': False, 'review_required': True,
            'evidence_scope': 'Original native platform only; no game compatibility, guest DSP acceptance or power-loss guarantee.',
            'environment': environment, 'criteria': groups, 'inventory': inventory}


def checklist(report):
    lines = ['# M1 evidence review', '', f"Suite: `{report['suite_id']}` · source `{report['source_commit']}`", '',
             f"Candidate status: **{report['status']}**. Full milestone acceptance: **not granted**.", '',
             'PASS is reviewable evidence for a criterion. A maintainer must still review the complete return and live Project transition.', '']
    for group in report['criteria']:
        lines.extend([f"## {group['id']}: {group['status']}", ''])
        for row in group['checks']:
            lines.append(f"- **{row['status']}** `{row['id']}` — {row['detail']}")
        lines.append('')
    lines.extend(['Keys, certificates, saves, package contents and raw traces are never included in this generated checklist.', ''])
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path, help='Local FTP copy of ctr2wup, with result TXT and paired full traces')
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--observations', type=Path, required=True)
    parser.add_argument('--expected-source', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--checklist', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or args.checklist.exists():
        parser.error('Preserve existing review outputs; choose new filenames')
    report = review(args.folder, read_json(args.manifest), read_json(args.observations), args.expected_source)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2);stream.write('\n')
    with args.checklist.open('x', encoding='utf-8') as stream:
        stream.write(checklist(report))
    print(json.dumps({key: report[key] for key in ('status', 'candidate_complete', 'full_milestone_acceptance', 'review_required')}))


if __name__ == '__main__':
    main()
