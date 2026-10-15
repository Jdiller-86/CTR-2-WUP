"""Original adversarial evidence fixtures; no actual hardware/WUP pass inferred."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('m1_review', ROOT/'tools/review-m1-results.py')
reviewer = importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(reviewer)
SOURCE = 'a' * 40
OTHER = 'b' * 40
TITLE = '0005000012345678'


class M1AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.manifest = {
            'schema_version': 1, 'kind': 'ctr2wup-m1-acceptance', 'suite_id': 'm1-original-test',
            'version': '0.26.0', 'source_commit': SOURCE, 'source_dirty': False, 'native_app': 'native',
            'recovery': {'available': True, 'protocol': 'scratch-v1'},
            # These are invented declarations for a parser fixture, not an
            # installable package or evidence of real credential provenance.
            'package': {'available': True, 'source_commit': SOURCE, 'title_id': TITLE,
                        'sha256': 'c' * 64, 'integrity_verified': True,
                        'identity_collision_checked': True, 'provenance_reviewed': True,
                        'synthetic_credentials': False},
        }
        self.observations = {
            'suite_id': self.manifest['suite_id'],
            'environment': {'firmware': '5.5.6 U', 'aroma': 'Beta 27',
                            'launcher': 'Aroma Menu', 'launcher_version': 'no separate launcher',
                            'sd_format': 'FAT32', 'sd_capacity': '32 GB', 'tv_mode': '1080p',
                            'installer': 'WUP Installer GX2', 'installer_version': 'original test fixture',
                            'installation_target': 'USB'},
            'runs': {}, 'reviewed_attempts': {},
            'native_observations': {key: True for key in ('tv_correct', 'gamepad_correct', 'touch_aligned',
                'tone_tv', 'tone_gamepad', 'launch_music_stopped', 'stability_no_problems')},
            'installation': {'installation_succeeded': True, 'title_identity_correct': True,
                             'installed_both_screens_correct': True},
        }
        self.names = {}
        for index, role in enumerate(reviewer.ROLES, 1):
            name, _ = self.write_run(index, full=role == 'native-full', home=role == 'native-home',
                                     installed=role.startswith('installed'), fresh=index != 1)
            self.names[role] = name
            self.observations['runs'][role] = {'result': name, 'exit_control': 'home' if role == 'native-home' else 'minus',
                'returned_ok': True, 'no_unexplained_problem': True, 'launched_from_menu': True}

    def write_run(self, index, full=False, home=False, installed=False, fresh=True):
        app = self.manifest.get('native_app', 'native')
        session = f'{index:016x}';version = self.manifest['version']
        name = f'result-{app}-{version}-{session}-00.txt'
        trace = f'{app}-{session}-00.txt'
        outcomes = '\n'.join(f'{i}. {step}: {"PASS" if full else "NOT TESTED"} | Original fixture observation'
                             for i, step in enumerate(reviewer.NATIVE_STEPS, 1))
        report = (f'CTR2WUP result report\nApplication: {app} | Build: {version}\nSession ticks: {session}\nFile: {name}\n'
                  f'Source: {SOURCE} dirty=0 | Evidence schema: 2\n'
                  'Stage: resources_closed_before_system_transition\nHost exit result: 0 (-1 means still running)\n'
                  f'Guide: {"COMPLETE" if full else "CANCELLED"} | passed={10 if full else 0} failed=0 skipped=0 current_step=1\n'
                  'Self-tests finished: 1 | CPU=0 services=0 heap=0 objects=0 HID=0 GSP=0 relay=0 stack=0 threads=0\n'
                  'Input: samples=1000 errors=0 A_presses=1 Minus_presses=1\n'
                  # Native shutdown legitimately releases its AX context and
                  # voice before this final persisted snapshot.
                  'Native audio context: result=READY owned=0 inherited=1 attempts=1 successes=1 failures=0 resets=1 releases=1 tone_ready=0 tone_requests=1; guest audio unavailable\n'
                  f'Full trace: SAVING | file={trace} errno=0 operation=none FSA=0 commits=4\n'
                  'Previous result writes verified: 30\nNamed guide results:\n' + outcomes + '\n')
        data = report.encode().ljust(8192, b' ')
        self.assertEqual(len(data), 8192)
        (self.folder/name).write_bytes(data)
        trace_text = (f'CTR2WUP Wii U test report\nApplication: {app} | Build: {version}\n'
                      f'BUILD source_commit={SOURCE} source_dirty=0 evidence_schema=2 result_bytes=8192\n'
                      f'M1_SESSION ticks={session} source_commit={SOURCE} version={version} protocol=m1-v1\n'
                      f'Platform title_id={TITLE if installed else "0005000013374842"} exit_route={"system_menu" if installed else "HBL_relaunch"} SD_owner=WUT_devoptab\n'
                      'Native audio begin: result=READY owned=1 inherited=1 attempts=1 failures=0 resets=1; launcher silence requires human check\n'
                      'audio configured=1 TV_result=0 DRC_result=0 rate=48000\n'
                      'tone request=1 frequency_hz=440 duration_ms=250\n'
                      f'M1_STORAGE protocol=scratch-v1 sentinel_written=1 sentinel_readback=1 fresh_reload={int(fresh)} previous_recovered=1 pending_rejected=1 invalid_path_rejected=1 final_readback=1 result=PASS\n')
        if full:
            for number, step in enumerate(reviewer.NATIVE_STEPS, 1):
                elapsed = 600000 if number == 9 else 1000
                trace_text += f'GUIDE RESULT: step={number} name={step} result=PASS elapsed_ms={elapsed} observed=00000000 evidence=Original fixture\n'
        trace_text += ('EXIT trigger=HBL_home_button\n' if home else 'EXIT trigger=Minus; cleaning resources before system transition\n')
        trace_text += f'Application: {app} | Build: {version} | Entering cleanup: result=0\n'
        trace_text += 'Guest fault observed during session: NO\n'
        for stage in reviewer.EXIT_STAGES:
            if stage == 'report_close_done':
                trace_text += '===== END OF REPORT: application resource cleanup reached; result=0 =====\n'
            trace_text += f'EXIT stage={stage}' + ('; SD mount cleanup belongs to WUT' if stage == 'report_close_done' else '') + '\n'
        (self.folder/trace).write_bytes(trace_text.encode())
        return name, trace

    def result(self):
        return reviewer.review(self.folder, self.manifest, self.observations, SOURCE)

    def criterion(self, name):
        return next(row for row in self.result()['criteria'] if row['id'] == name)

    def alter(self, role, old, new, trace=False):
        path = self.folder/self.names[role]
        if trace:
            name = path.name.removeprefix('result-').replace('-0.26.0-', '-', 1)
            path = self.folder/name
        data = path.read_bytes()
        self.assertIn(old.encode(), data)
        data = data.rstrip(b' ').replace(old.encode(), new.encode())
        path.write_bytes(data if trace else data.ljust(8192, b' '))

    def test_complete_original_parser_fixture_is_candidate_never_acceptance(self):
        report = self.result()
        self.assertEqual(report['status'], 'PASS')
        self.assertTrue(report['candidate_complete']);self.assertFalse(report['full_milestone_acceptance'])
        self.assertTrue(report['review_required'])
        self.assertEqual({row['status'] for row in report['criteria']}, {'PASS'})
        self.assertNotIn('trace_text', json.dumps(report))
        self.assertNotIn('tone request=', reviewer.checklist(report))

    def test_ftp_reports_with_crlf_line_endings_keep_the_same_evidence(self):
        for path in self.folder.glob('*.txt'):
            data = path.read_bytes().rstrip(b' ').replace(b'\n', b'\r\n')
            path.write_bytes(data.ljust(8192, b' ') if path.name.startswith('result-') else data)
        report = self.result()
        self.assertEqual(report['status'], 'PASS')
        self.assertTrue(report['candidate_complete'])
        self.assertFalse(report['full_milestone_acceptance'])

    def test_no_actual_wup_blocks_full_gate_while_platform_evidence_passes(self):
        self.manifest['package'] = {'available': False, 'blockers': ['real packaging material unavailable']}
        report = self.result()
        self.assertEqual(report['status'], 'BLOCKED')
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'PASS')
        self.assertEqual(self.criterion('m1-lifecycle')['status'], 'PASS')
        self.assertFalse(report['candidate_complete'])

    def test_current_source_and_clean_build_are_independently_required(self):
        for key, value in [('source_commit', OTHER), ('source_dirty', True), ('suite_id', '../bad')]:
            manifest = copy.deepcopy(self.manifest);manifest[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                reviewer.review(self.folder, manifest, self.observations, SOURCE)
        with self.assertRaises(ValueError):reviewer.review(self.folder, self.manifest, self.observations, 'not-source')

    def test_manifest_application_override_matches_actual_native_profile(self):
        self.manifest['native_app'] = 'native-platform'
        for index, role in enumerate(reviewer.ROLES, 1):
            name, _ = self.write_run(index, full=role == 'native-full', home=role == 'native-home', installed=role.startswith('installed'), fresh=index != 1)
            self.observations['runs'][role]['result'] = name
        self.assertEqual(self.result()['status'], 'PASS')

    def test_report_source_conflict_even_same_version_is_failure(self):
        self.alter('native-full', 'Source: '+SOURCE, 'Source: '+OTHER)
        self.assertEqual(self.criterion('m1-results')['status'], 'FAIL')
        self.assertFalse(self.result()['candidate_complete'])

    def test_trace_identity_rejects_stale_source_and_extra_conflicting_header(self):
        self.alter('native-full', 'source_commit='+SOURCE, 'source_commit='+OTHER, trace=True)
        self.assertEqual(self.criterion('m1-results')['status'], 'FAIL')
        self.alter('native-full', 'source_commit='+OTHER, 'source_commit='+SOURCE, trace=True)
        self.alter('native-full', 'BUILD source_commit=', 'Application: other | Build: 0.26.0\nBUILD source_commit=', trace=True)
        self.assertEqual(self.criterion('m1-results')['status'], 'FAIL')

    def test_trace_body_must_identify_the_same_process_even_after_a_rename(self):
        self.alter('native-full', 'M1_SESSION ticks=0000000000000001', 'M1_SESSION ticks=0000000000000099', trace=True)
        self.assertEqual(self.criterion('m1-results')['status'], 'FAIL')

    def test_missing_pair_and_interrupted_lifecycle_remain_incomplete(self):
        trace = self.folder/self.names['native-full'].removeprefix('result-').replace('-0.26.0-', '-', 1)
        trace.unlink()
        self.assertEqual(self.criterion('m1-results')['status'], 'INCOMPLETE')
        self.assertEqual(len(self.result()['inventory']), 5)
        self.write_run(1, full=True, fresh=False)
        self.alter('native-full', 'EXIT stage=main_return', 'INTERRUPTED before main return', trace=True)
        self.assertEqual(self.criterion('m1-results')['status'], 'INCOMPLETE')

    def test_skipped_full_guide_and_599999ms_are_not_complete(self):
        self.alter('native-full', 'elapsed_ms=600000', 'elapsed_ms=599999', trace=True)
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'INCOMPLETE')
        self.alter('native-full', 'elapsed_ms=599999', 'elapsed_ms=600000', trace=True)
        self.alter('native-full', '2. Buttons: PASS', '2. Buttons: NOT TESTED')
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'INCOMPLETE')

    def test_failed_input_and_selftest_are_not_masked_by_user_r_answers(self):
        self.alter('native-full', 'errors=0 A_presses', 'errors=2 A_presses')
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'FAIL')
        self.alter('native-full', 'errors=2 A_presses', 'errors=0 A_presses')
        self.alter('native-full', 'CPU=0 services=0', 'CPU=1 services=0')
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'FAIL')

    def test_fresh_process_distinctness_and_exit_route_need_evidence(self):
        self.observations['runs']['native-repeat']['result'] = self.names['native-full']
        self.assertEqual(self.criterion('m1-lifecycle')['status'], 'FAIL')
        self.observations['runs']['native-repeat']['result'] = self.names['native-repeat']
        self.alter('native-home', 'EXIT trigger=HBL_home_button', 'EXIT trigger=Minus; cleaning resources before system transition', trace=True)
        self.assertEqual(self.criterion('m1-lifecycle')['status'], 'INCOMPLETE')

    def test_fresh_storage_reload_recovery_and_errors_are_all_required(self):
        self.alter('native-repeat', 'fresh_reload=1', 'fresh_reload=0', trace=True)
        self.assertEqual(self.criterion('m1-lifecycle')['status'], 'INCOMPLETE')
        self.alter('native-repeat', 'fresh_reload=0', 'fresh_reload=1', trace=True)
        self.alter('native-repeat', 'pending_rejected=1', 'pending_rejected=0', trace=True)
        self.assertEqual(self.criterion('m1-lifecycle')['status'], 'FAIL')

    def test_no_actual_probe_is_blocked_and_duplicate_receipt_fields_fail(self):
        self.manifest['recovery']['available'] = False
        self.assertEqual(self.criterion('m1-lifecycle')['status'], 'BLOCKED')
        self.manifest['recovery']['available'] = True
        self.alter('native-full', 'result=PASS\n', 'result=PASS result=PASS\n', trace=True)
        self.assertEqual(self.criterion('m1-lifecycle')['status'], 'FAIL')

    def test_unknown_manual_observation_and_real_problem_are_distinct(self):
        del self.observations['native_observations']['tone_gamepad']
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'INCOMPLETE')
        self.observations['native_observations']['tone_gamepad'] = False
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'FAIL')
        self.observations['native_observations']['tone_gamepad'] = True
        self.observations['runs']['native-repeat']['returned_ok'] = False
        self.assertEqual(self.criterion('m1-lifecycle')['status'], 'FAIL')

    def test_structural_package_and_aroma_icon_do_not_prove_installation(self):
        self.manifest['package']['synthetic_credentials'] = True
        self.assertEqual(self.criterion('m1-package-launch')['status'], 'BLOCKED')
        self.manifest['package']['synthetic_credentials'] = False
        self.observations['runs']['installed-first']['launched_from_menu'] = False
        self.assertEqual(self.criterion('m1-package-launch')['status'], 'FAIL')
        self.observations['runs']['installed-first']['launched_from_menu'] = True
        self.alter('installed-first', 'Platform title_id='+TITLE, 'Platform title_id=0005000013374842', trace=True)
        self.assertEqual(self.criterion('m1-package-launch')['status'], 'INCOMPLETE')

    def test_unassigned_attempt_is_preserved_until_explicit_review(self):
        name, _ = self.write_run(6)
        self.assertEqual(self.criterion('m1-results')['status'], 'INCOMPLETE')
        self.observations['reviewed_attempts'][name] = 'Original fixture: extra voluntary short launch, no problem reported.'
        self.assertEqual(self.criterion('m1-results')['status'], 'PASS')
        self.assertEqual(len(self.result()['inventory']), 6)

    def test_zero_reports_and_missing_environment_cannot_accept(self):
        for path in self.folder.iterdir():path.unlink()
        self.assertEqual(self.result()['status'], 'INCOMPLETE')
        self.assertFalse(self.result()['candidate_complete'])
        self.write_run(1, full=True)
        del self.observations['environment']['aroma']
        self.assertEqual(self.result()['status'], 'INCOMPLETE')

    def test_no_readback_or_trace_storage_error_cannot_pass(self):
        self.alter('native-full', 'Previous result writes verified: 30', 'Previous result writes verified: 0')
        self.assertEqual(self.criterion('m1-results')['status'], 'INCOMPLETE')
        self.alter('native-full', 'Full trace: SAVING', 'Full trace: FAILED')
        self.assertEqual(self.criterion('m1-results')['status'], 'FAIL')

    def test_trace_guide_failures_and_missing_outcomes_cannot_hide_behind_result_pass(self):
        self.alter('native-full', 'name=Buttons result=PASS', 'name=Buttons result=FAIL', trace=True)
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'FAIL')
        self.alter('native-full', 'name=Buttons result=FAIL', 'name=Buttons result=PASS', trace=True)
        self.alter('native-full', 'GUIDE RESULT: step=2', 'MISSING RESULT: step=2', trace=True)
        self.assertEqual(self.criterion('m1-controls-audio')['status'], 'INCOMPLETE')

    def test_path_injection_private_files_and_symlinks_are_not_read(self):
        (self.folder/'common.key').write_bytes(b'private original negative fixture')
        (self.folder/'filesystem-test.snapshot').write_bytes(b'private original negative fixture')
        self.assertEqual(len(self.result()['inventory']), 5)
        self.observations['runs']['native-full']['result'] = '../common.key'
        with self.assertRaises(ValueError):self.result()
        self.observations['runs']['native-full']['result'] = self.names['native-full']
        path = self.folder/self.names['native-full'];path.unlink()
        try:path.symlink_to(self.folder/'common.key')
        except OSError:self.skipTest('Symlink creation restricted on this Windows runner')
        with self.assertRaises(ValueError):self.result()

    def test_json_duplicate_fields_and_oversized_trace_are_rejected(self):
        path = self.folder/'observations.json';path.write_text('{"suite_id":"a","suite_id":"b"}')
        with self.assertRaises(ValueError):reviewer.read_json(path)
        trace = self.folder/self.names['native-full'].removeprefix('result-').replace('-0.26.0-', '-', 1)
        trace.write_bytes(b'x' * (reviewer.MAX_FILE+1))
        with self.assertRaises(ValueError):self.result()


if __name__ == '__main__':
    unittest.main()
