#!/usr/bin/env python3
"""Maintainer: review-install-results.py FOLDER --source COMMIT [--observations FILE].

FOLDER is the completed FTP copy of the kit's source-specific results folder,
including kit.json. Historical install-v1 folders remain readable, but do not
prove the production snapshot-v2 recovery required by m1-completion-v2.
Observations identify suite_id, installer, firmware/aroma (or environment_unchanged), install_success,
title_id_checked and runs [{session, menu_launch, both_screens, minus_pressed,
returned_to_menu, no_problem}]. Unknown observations stay incomplete; raw notes
are never copied into the candidate JSON. This tool cannot accept a milestone.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat

ROOT = Path(__file__).resolve().parents[1]
TITLE = '0005000043543257'
STAGES = ('guest_cleanup_begin', 'guest_cleanup_done', 'audio_cleanup_begin',
          'audio_cleanup_done', 'graphics_cleanup_begin', 'graphics_cleanup_done',
          'report_close_begin', 'report_close_done', 'resources_closed_before_procui_shutdown',
          'callbacks_cleared', 'request_menu_begin', 'request_menu_returned',
          'procui_poll_begin', 'procui_shutdown_begin', 'procui_shutdown_done',
          'whb_proc_shutdown_begin', 'whb_proc_shutdown_done', 'main_return')
RESULT = re.compile(r'result-native-install-0\.27\.0-([0-9a-f]{16})-[0-9]{2}\.txt')
TRACE = re.compile(r'native-install-([0-9a-f]{16})-[0-9]{2}\.txt')
STORAGE = re.compile(r'M1_STORAGE protocol=scratch-v1 sentinel_written=1 sentinel_readback=1 fresh_reload=([01]) previous_recovered=1 pending_rejected=1 invalid_path_rejected=1 final_readback=1 result=PASS errno=0 error_stage=none writes=([1-9][0-9]*) readbacks=([1-9][0-9]*) recoveries=1; software_recovery_not_power_loss=1')
SNAPSHOT_FIELDS = frozenset(('protocol', 'sentinel_written', 'sentinel_readback',
    'fresh_reload', 'previous_recovered', 'pending_rejected', 'invalid_path_rejected',
    'final_readback', 'result', 'errno', 'error_stage', 'writes', 'readbacks',
    'recoveries', 'phase', 'producer_session', 'recovery_session', 'current_session',
    'production_adapter', 'interruption_prepared', 'software_recovery_not_power_loss'))


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON field')
        result[key] = value
    return result


def load_json(data):
    return json.loads(data, object_pairs_hook=unique_object)


def snapshot_receipt(line, session):
    """Validate the phase receipt before joining evidence from two processes."""
    if not isinstance(line, str) or len(line) > 1024 or not line.startswith('M1_STORAGE '):
        return None
    fields = {}
    for token in line[len('M1_STORAGE '):].split():
        match = re.fullmatch(r'([a-z_]+)=([A-Za-z0-9_-]+);?', token)
        if not match or match[1] in fields:
            return None
        fields[match[1]] = match[2]
    if set(fields) != SNAPSHOT_FIELDS:
        return None
    required = dict(protocol='snapshot-v2', sentinel_written='1', sentinel_readback='1',
        invalid_path_rejected='1', result='PASS', errno='0', error_stage='none',
        production_adapter='1', interruption_prepared='1', software_recovery_not_power_loss='1')
    if any(fields[key] != value for key, value in required.items()):
        return None
    if any(not re.fullmatch(r'[0-9a-f]{16}', fields[key])
           for key in ('producer_session', 'recovery_session', 'current_session')):
        return None
    if fields['current_session'] != session or fields['producer_session'] == '0'*16:
        return None
    for key in ('writes', 'readbacks', 'recoveries'):
        if not re.fullmatch(r'0|[1-9][0-9]{0,9}', fields[key]) or int(fields[key]) > 0xffffffff:
            return None
    if int(fields['readbacks']) == 0:
        return None
    phase = fields['phase']
    if phase == 'PREPARED':
        expected = dict(fresh_reload='0', previous_recovered='0', pending_rejected='0',
                        final_readback='0', recoveries='0', writes='2')
        identity = fields['producer_session'] == session and fields['recovery_session'] == '0'*16
    elif phase in ('RECOVERED', 'COMPLETE'):
        expected = dict(fresh_reload='1', previous_recovered='1', pending_rejected='1',
                        final_readback='1', recoveries='1' if phase == 'RECOVERED' else '0',
                        writes='1' if phase == 'RECOVERED' else '0')
        producer, recovery = fields['producer_session'], fields['recovery_session']
        identity = (recovery != '0'*16 and producer != recovery
                    and (recovery == session if phase == 'RECOVERED'
                         else session not in (producer, recovery)))
    else:
        return None
    return fields if identity and all(fields[key] == value for key, value in expected.items()) else None


def recovery_chain(runs):
    """A retained completion receipt cannot replace either original launch."""
    receipts = [run['storage'] for run in runs if run['storage'] is not None]
    if len(receipts) != len(runs):
        return 'FAIL', 'Every attempt must contain a valid production snapshot phase receipt.'
    prepared = [row for row in receipts if row['phase'] == 'PREPARED']
    recovered = [row for row in receipts if row['phase'] == 'RECOVERED']
    if len(prepared) > 1 or len(recovered) > 1:
        return 'FAIL', 'Conflicting preparation or recovery attempts cannot be selected away.'
    if not prepared or not recovered:
        return 'INCOMPLETE', 'Return both the PREPARED launch and the distinct RECOVERED launch; COMPLETE alone is a recheck.'
    producer, recovery = prepared[0]['current_session'], recovered[0]['current_session']
    if (producer == recovery or recovered[0]['producer_session'] != producer
            or any(row['producer_session'] != producer or row['recovery_session'] != recovery
                   for row in receipts if row['phase'] == 'COMPLETE')):
        return 'FAIL', 'The recovered and completion receipts must identify the returned producer/recovery process pair.'
    return 'PASS', 'A distinct process recovered the original production snapshot after prepared interruption, rejected pending data, and published/read back a replacement.'


def read(path, limit=4*1024*1024):
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > limit:
        raise ValueError('Expected a bounded regular evidence file')
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    with os.fdopen(fd, 'rb') as stream:
        opened = os.fstat(stream.fileno())
        if (before.st_dev, before.st_ino, before.st_size) != (opened.st_dev, opened.st_ino, opened.st_size):
            raise ValueError('Evidence changed while opening')
        data = stream.read(limit+1)
        closed = os.fstat(stream.fileno())
    fingerprint = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
    if fingerprint(before) != fingerprint(path.lstat()) or fingerprint(opened) != fingerprint(closed) or len(data) != before.st_size:
        raise ValueError('Evidence changed; finish FTP before reviewing')
    return data


def one(text, pattern):
    values = re.findall(pattern, text, re.MULTILINE)
    return values[0] if len(values) == 1 else None


def review(folder, source, observations=None):
    folder = Path(folder)
    if folder.is_symlink() or not folder.is_dir() or not re.fullmatch(r'[0-9a-f]{40}', source):
        raise ValueError('Require a local folder and independently recorded exact source')
    # A whole FTP return can contain older kits. Select only the exact source
    # child, without inspecting siblings or silently excluding a current attempt.
    selected = folder/source
    if selected.exists() or selected.is_symlink():
        if selected.is_symlink() or not selected.is_dir():
            raise ValueError('The selected source evidence must be a regular directory')
        folder = selected
    kit = load_json(read(folder/'kit.json', 1024*1024))
    if (not isinstance(kit, dict) or not isinstance(kit.get('package'), dict)
            or kit.get('kind') != 'ctr2wup-m1-acceptance' or kit.get('schema_version') != 1
            or kit.get('source_commit') != source or kit.get('source_dirty') is not False
            or kit.get('version') != '0.27.0' or kit.get('native_app') != 'native-install'
            or not re.fullmatch(r'[A-Za-z0-9._-]{1,80}', kit.get('suite_id', ''))
            or kit.get('package', {}).get('title_id') != TITLE or kit.get('package', {}).get('available') is not True):
        raise ValueError('Require the exact clean installed-title kit identity')
    completion = kit.get('acceptance_profile') == 'm1-completion-v2'
    if kit.get('acceptance_profile') not in (None, 'm1-completion-v2'):
        raise ValueError('Unknown installed-title acceptance profile')
    if completion and (not isinstance(kit.get('recovery'), dict)
            or not isinstance(kit.get('install_check'), dict)
            or kit['recovery'].get('available') is not True
            or kit['recovery'].get('protocol') != 'snapshot-v2'
            or kit['install_check'].get('storage_protocol') != 'snapshot-v2'
            or kit['install_check'].get('session_protocol') != 'install-v2'
            or kit['install_check'].get('log_directory') != 'ctr2wup/install-check/'+source):
        raise ValueError('Completion kits must identify the production snapshot-v2 protocol')
    protocol = 'install-v2' if completion else 'install-v1'
    checks, runs, used, total = [], [], set(), 0
    def check(identifier, status, detail):
        checks.append(dict(id=identifier, status=status, detail=detail))
    paths = sorted(p for p in folder.iterdir() if RESULT.fullmatch(p.name))
    if len(paths) > 16:
        raise ValueError('Too many attempts; preserve files for manual review')
    check('all-attempts', 'PASS' if len(paths) >= 2 else 'INCOMPLETE', 'At least two distinct fresh installed-title launches are required; every attempt is reviewed.')
    for path in paths:
        session = RESULT.fullmatch(path.name)[1]
        data = read(path, 8192); total += len(data); text = data.decode('utf-8').replace('\r\n', '\n')
        errors, missing = [], []
        wanted = ('CTR2WUP result report\n', 'Application: native-install | Build: 0.27.0\n',
                  'Session ticks: '+session+'\n', 'File: '+path.name+'\n',
                  'Source: '+source+' dirty=0 | Evidence schema: 2\n',
                  'Actual title ID: '+TITLE+' | Expected title ID: '+TITLE+' | Match: 1\n',
                  'Session: '+session+' | Protocol: '+protocol+'\n',
                  'Stage: resources_closed_before_system_transition\n')
        if len(data) != 8192 or any(text.count(value) != 1 for value in wanted): errors.append('result identity/size mismatch')
        if one(text, r'^Host exit result: (-?[0-9]+) .*') != '0': errors.append('unsuccessful exit result')
        counters = one(text, r'^Frames: host=([0-9]+) \| Input: samples=([0-9]+) errors=([0-9]+) Minus_presses=([0-9]+)$')
        if not counters or int(counters[0]) == 0 or int(counters[1]) == 0 or counters[2] != '0' or int(counters[3]) == 0: errors.append('display/input/Minus counters failed')
        if not (one(text, r'^Previous result writes verified: ([0-9]+)$') or '').strip('0'): missing.append('closed read-back counter')
        trace_name = one(text, r'^Full trace: SAVING \| file=(native-install-'+session+r'-[0-9]{2}\.txt) errno=0 operation=none FSA=-?[0-9]+ commits=[0-9]+$')
        storage = one(text, r'^(M1_STORAGE .*)$')
        probe = snapshot_receipt(storage, session) if completion else STORAGE.fullmatch(storage or '')
        if not probe: errors.append('automatic SD recovery failed or receipt malformed')
        trace_digest, trace_bytes = None, 0
        if not trace_name or not (folder/trace_name).exists(): missing.append('paired trace')
        else:
            raw = read(folder/trace_name); total += len(raw)
            trace_digest, trace_bytes = hashlib.sha256(raw).hexdigest(), len(raw)
            if total > 24*1024*1024: raise ValueError('Evidence exceeds review budget')
            trace = raw.decode('utf-8').replace('\r\n', '\n'); used.add(trace_name)
            session_line = 'INSTALL_SESSION ticks='+session+' source_commit='+source+' version=0.27.0 protocol='+protocol+' title_id='+TITLE+' expected_title_id='+TITLE+' title_match=1'
            if (one(trace, r'^INSTALL_SESSION .*$',) != session_line
                    or one(trace, r'^BUILD .*$',) != 'BUILD source_commit='+source+' source_dirty=0 evidence_schema=2 result_bytes=8192'
                    or one(trace, r'^Platform .*$',) != 'Platform title_id='+TITLE+' exit_route=system_menu SD_owner=WUT_devoptab'
                    or one(trace, r'^Application: (.*)$') != 'native-install | Build: 0.27.0'): errors.append('trace source/session/installed route mismatch')
            positions = [one(trace, r'^(EXIT stage='+stage+r'(?:;[^\n]*)?)$') for stage in STAGES]
            positions = [trace.find(value) if value is not None else -1 for value in positions]
            if -1 in positions: missing.append('complete exit checkpoints')
            elif positions != sorted(set(positions)): errors.append('exit checkpoint order')
            if 'EXIT trigger=Minus; cleaning resources before system transition' not in trace or 'EXIT status=EXITING' not in trace: missing.append('Minus and Menu transition')
            marker = one(trace, r'^(===== END OF REPORT: application resource cleanup reached; result=0 =====)$')
            if marker is None: missing.append('successful cleanup marker')
            elif not trace.find('EXIT stage=graphics_cleanup_done') < trace.find(marker) < trace.find('EXIT stage=report_close_begin'): errors.append('cleanup marker order')
            if not storage or set(re.findall(r'^M1_STORAGE .*$', trace, re.MULTILINE)) != {storage}: errors.append('trace/result recovery differs')
            if re.search(r'\bFAIL(?:ED)?\b', trace): errors.append('trace records failure')
        check('launch-'+session, 'FAIL' if errors else 'INCOMPLETE' if missing else 'PASS', '; '.join(errors+missing) or 'Installed source/session, display frames, SD recovery, read-back and ordered Minus cleanup corroborated.')
        runs.append(dict(session=session, result=path.name, trace=trace_name,
                         sha256=hashlib.sha256(data).hexdigest(),
                         result_bytes=len(data), trace_sha256=trace_digest, trace_bytes=trace_bytes,
                         fresh_reload=bool(probe and (probe['fresh_reload'] if completion else probe[1]) == '1'),
                         storage=probe if completion else None))
    if total > 24*1024*1024: raise ValueError('Evidence exceeds review budget')
    orphan = [p for p in folder.iterdir() if p.name.endswith('.txt') and p.name.startswith(('native-', 'result-')) and p.name not in used and p not in paths]
    check('paired-attempts', 'INCOMPLETE' if orphan else 'PASS', 'Every matching full trace must have a result; extra attempts cannot be discarded.')
    sessions = [r['session'] for r in runs]
    if completion:
        check('fresh-processes', 'FAIL' if len(set(sessions)) != len(sessions) else 'PASS' if len(runs) >= 2 else 'INCOMPLETE', 'Each returned attempt must identify a distinct process session.')
        check('production-snapshot-recovery', *recovery_chain(runs))
    else:
        check('fresh-processes', 'FAIL' if len(set(sessions)) != len(sessions) else 'PASS' if len(runs) >= 2 and all(r['fresh_reload'] for r in runs[1:]) else 'INCOMPLETE', 'Historical scratch-v1 fresh-process evidence; this does not prove production snapshot recovery.')
    obs = {} if observations is None else observations
    if not isinstance(obs, dict): raise ValueError('Malformed observations')
    records = obs.get('runs', [])
    if not isinstance(obs, dict) or not isinstance(records, list) or any(not isinstance(r, dict) for r in records): raise ValueError('Malformed observations')
    manual = [obs.get('suite_id') == kit.get('suite_id'), obs.get('installer') == 'WUP Installer GX2',
              obs.get('environment_unchanged') is True or all(isinstance(obs.get(k), str) and obs[k].strip() for k in ('firmware', 'aroma')),
              obs.get('install_success'), obs.get('title_id_checked')]
    for run in runs:
        matches = [r for r in records if r.get('session') == run['session']]
        manual.extend(matches[0].get(k) for k in ('menu_launch', 'both_screens', 'minus_pressed', 'returned_to_menu', 'no_problem')) if len(matches) == 1 else manual.append(None)
    explicit_failure = obs.get('install_success') is False or obs.get('title_id_checked') is False or any(r.get(k) is False for r in records for k in ('menu_launch', 'both_screens', 'minus_pressed', 'returned_to_menu', 'no_problem'))
    check('physical-observations', 'FAIL' if explicit_failure else 'PASS' if len(records) == len(runs) >= 2 and {r.get('session') for r in records} == set(sessions) and all(v is True for v in manual) else 'INCOMPLETE', 'Exact suite/environment/GX2 and both observed Menu launches/screens/Minus returns must be explicitly confirmed.')
    ledger = load_json(read(ROOT/'tests/hardware-evidence.json', 1024*1024))
    baseline = [dict(issue=c['issue'], status=c['reviewed_status']) for c in ledger['criteria'] if c['issue'] in (12, 14)]
    check('retained-baseline', 'PASS' if baseline == [dict(issue=12, status='Done'), dict(issue=14, status='Done')] else 'INCOMPLETE', 'Carry reviewed collection and native controls/audio/stability evidence; no native guide repeat requested.')
    status = next((s for s in ('FAIL', 'INCOMPLETE') if any(c['status'] == s for c in checks)), 'PASS')
    return dict(schema_version=1, kind='ctr2wup-install-review', source_commit=source,
                suite_id=kit['suite_id'], status=status, candidate_only=True,
                acceptance_profile='m1-completion-v2' if completion else 'legacy-install-v1',
                m1_completion_ready=completion and status == 'PASS',
                full_milestone_acceptance=False, physical_review_required=True,
                retained_evidence=baseline, checks=checks, runs=runs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path); parser.add_argument('--source', required=True); parser.add_argument('--observations', type=Path)
    args = parser.parse_args()
    try:
        result = review(args.folder, args.source, load_json(read(args.observations, 1024*1024)) if args.observations else None)
    except (ValueError, OSError, UnicodeError, KeyError, TypeError):
        result = dict(status='FAIL', candidate_only=True, full_milestone_acceptance=False, error='Invalid, changed or unsafe evidence; inspect locally without publishing raw notes.')
    print(json.dumps(result, indent=2)); return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
