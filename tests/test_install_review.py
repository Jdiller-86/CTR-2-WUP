"""Original focused fixtures for the changed installed-title evidence route."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('install_review', Path(__file__).resolve().parents[1]/'tools/review-install-results.py')
reviewer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reviewer)
SOURCE = 'a'*40
STORAGE = 'M1_STORAGE protocol=scratch-v1 sentinel_written=1 sentinel_readback=1 fresh_reload={fresh} previous_recovered=1 pending_rejected=1 invalid_path_rejected=1 final_readback=1 result=PASS errno=0 error_stage=none writes=4 readbacks=5 recoveries=1; software_recovery_not_power_loss=1'


class InstallReviewTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        self.kit = dict(schema_version=1, kind='ctr2wup-m1-acceptance', source_commit=SOURCE,
                        source_dirty=False, version='0.27.0', native_app='native-install', suite_id='original-test',
                        package=dict(available=True, title_id=reviewer.TITLE))
        (self.folder/'kit.json').write_text(json.dumps(self.kit))
        self.observations = dict(suite_id='original-test', installer='WUP Installer GX2', environment_unchanged=True,
                                 install_success=True, title_id_checked=True, runs=[])
        self.add_run(1, 0); self.add_run(2, 1)

    def add_run(self, number, fresh):
        session = f'{number:016x}'
        result = f'result-native-install-0.27.0-{session}-00.txt'
        trace = f'native-install-{session}-00.txt'
        storage = STORAGE.format(fresh=fresh)
        body = (f'CTR2WUP result report\nApplication: native-install | Build: 0.27.0\nSession ticks: {session}\nFile: {result}\n'
                f'Source: {SOURCE} dirty=0 | Evidence schema: 2\nStage: resources_closed_before_system_transition\n'
                f'Host exit result: 0 (-1 means still running)\nActual title ID: {reviewer.TITLE} | Expected title ID: {reviewer.TITLE} | Match: 1\n'
                f'Session: {session} | Protocol: install-v1\nFrames: host=20 | Input: samples=20 errors=0 Minus_presses=1\n{storage}\n'
                f'Full trace: SAVING | file={trace} errno=0 operation=none FSA=0 commits=3\nPrevious result writes verified: 3\n')
        (self.folder/result).write_bytes(body.encode().ljust(8191, b' ')+b'\n')
        lines = ['CTR2WUP Wii U test report', 'Application: native-install | Build: 0.27.0',
                 f'BUILD source_commit={SOURCE} source_dirty=0 evidence_schema=2 result_bytes=8192',
                 f'INSTALL_SESSION ticks={session} source_commit={SOURCE} version=0.27.0 protocol=install-v1 title_id={reviewer.TITLE} expected_title_id={reviewer.TITLE} title_match=1',
                 f'Platform title_id={reviewer.TITLE} exit_route=system_menu SD_owner=WUT_devoptab', storage,
                 'EXIT trigger=Minus; cleaning resources before system transition', storage]
        for stage in reviewer.STAGES:
            if stage == 'report_close_begin': lines.append('===== END OF REPORT: application resource cleanup reached; result=0 =====')
            lines.append('EXIT stage='+stage)
        lines.append('EXIT status=EXITING')
        (self.folder/trace).write_text('\n'.join(lines)+'\n')
        self.observations['runs'].append(dict(session=session, menu_launch=True, both_screens=True, minus_pressed=True,
                                               returned_to_menu=True, no_problem=True))
        return self.folder/trace

    def test_focused_two_launches_reuse_baseline_and_extra_good_launch_is_allowed(self):
        (self.folder/'generated-scratch.txt').write_text('ignored original scratch; no key/save scanning')
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'PASS', result['checks'])
        self.assertTrue(result['candidate_only']); self.assertFalse(result['full_milestone_acceptance'])
        self.assertEqual(result['retained_evidence'], [dict(issue=12, status='Done'), dict(issue=14, status='Done')])
        self.add_run(3, 1)
        self.assertEqual(reviewer.review(self.folder, SOURCE, self.observations)['status'], 'PASS')

    def test_unknown_observations_and_incomplete_exit_are_not_passes(self):
        self.assertEqual(reviewer.review(self.folder, SOURCE)['status'], 'INCOMPLETE')
        trace = self.folder/'native-install-0000000000000002-00.txt'
        trace.write_text(trace.read_text().replace('EXIT stage=main_return\n', ''))
        self.assertEqual(reviewer.review(self.folder, SOURCE, self.observations)['status'], 'INCOMPLETE')

    def test_extra_failed_attempt_and_wrong_route_are_never_selected_away(self):
        trace = self.add_run(3, 1)
        trace.write_text(trace.read_text()+'INSTALL_IDENTITY result=FAIL\n')
        self.assertEqual(reviewer.review(self.folder, SOURCE, self.observations)['status'], 'FAIL')
        trace.unlink(); self.observations['runs'].pop()
        trace = self.folder/'native-install-0000000000000002-00.txt'
        trace.write_text(trace.read_text().replace('exit_route=system_menu', 'exit_route=HBL_relaunch'))
        self.assertEqual(reviewer.review(self.folder, SOURCE, self.observations)['status'], 'FAIL')

    def test_source_mismatch_and_symlink_are_rejected_without_reading_target(self):
        with self.assertRaises(ValueError): reviewer.review(self.folder, 'b'*40, self.observations)
        path = self.folder/'result-native-install-0.27.0-0000000000000002-00.txt'
        data = path.read_bytes(); path.unlink()
        target = self.folder/'private-unread.bin'; target.write_bytes(data)
        try: path.symlink_to(target)
        except (OSError, NotImplementedError): self.skipTest('Symlinks unavailable')
        with self.assertRaises(ValueError): reviewer.review(self.folder, SOURCE, self.observations)


if __name__ == '__main__':
    unittest.main()
