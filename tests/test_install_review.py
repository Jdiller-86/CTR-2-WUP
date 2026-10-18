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
        self.completion = False
        self.io_barrier = False
        self.kit = dict(schema_version=1, kind='ctr2wup-m1-acceptance', source_commit=SOURCE,
                        source_dirty=False, version='0.27.0', native_app='native-install', suite_id='original-test',
                        package=dict(available=True, title_id=reviewer.TITLE))
        (self.folder/'kit.json').write_text(json.dumps(self.kit))
        self.observations = dict(suite_id='original-test', installer='WUP Installer GX2', environment_unchanged=True,
                                 install_success=True, title_id_checked=True, runs=[])
        self.add_run(1, 0); self.add_run(2, 1)

    def add_run(self, number, fresh, phase=None):
        session = f'{number:016x}'
        result = f'result-native-install-0.27.0-{session}-00.txt'
        trace = f'native-install-{session}-00.txt'
        if self.completion:
            phase = phase or ('PREPARED' if number == 1 else 'RECOVERED' if number == 2 else 'COMPLETE')
            prepared, recovered = phase == 'PREPARED', phase == 'RECOVERED'
            storage = ('M1_STORAGE protocol=snapshot-v2 phase='+phase+
                       f' producer_session={1:016x} recovery_session={0 if prepared else 2:016x}'
                       f' current_session={session} production_adapter=1 interruption_prepared=1'
                       ' sentinel_written=1 sentinel_readback=1'+
                       f' fresh_reload={int(not prepared)} previous_recovered={int(not prepared)}'+
                       f' pending_rejected={int(not prepared)} invalid_path_rejected=1 final_readback={int(not prepared)}'+
                       ' result=PASS errno=0 error_stage=none'+
                       f' writes={2 if prepared else 1 if recovered else 0} readbacks=5 recoveries={int(recovered)};'+
                       ' software_recovery_not_power_loss=1')
        else:
            storage = STORAGE.format(fresh=fresh)
        protocol = 'install-v3' if self.io_barrier else 'install-v2' if self.completion else 'install-v1'
        home=self.io_barrier and number==2
        body = (f'CTR2WUP result report\nApplication: native-install | Build: 0.27.0\nSession ticks: {session}\nFile: {result}\n'
                f'Source: {SOURCE} dirty=0 | Evidence schema: 2\nStage: resources_closed_before_system_transition\n'
                f'Host exit result: 0 (-1 means still running)\nActual title ID: {reviewer.TITLE} | Expected title ID: {reviewer.TITLE} | Match: 1\n'
                f'Session: {session} | Protocol: {protocol}\nFrames: host=20 | Input: samples=20 errors=0 Minus_presses={0 if home else 1}\n{storage}\n'
                f'Full trace: SAVING | file={trace} errno=0 operation=none FSA=0 commits=3\nPrevious result writes verified: 3\n')
        (self.folder/result).write_bytes(body.encode().ljust(8191, b' ')+b'\n')
        lines = ['CTR2WUP Wii U test report', 'Application: native-install | Build: 0.27.0',
                 f'BUILD source_commit={SOURCE} source_dirty=0 evidence_schema=2 result_bytes=8192',
                 f'INSTALL_SESSION ticks={session} source_commit={SOURCE} version=0.27.0 protocol={protocol} title_id={reviewer.TITLE} expected_title_id={reviewer.TITLE} title_match=1',
                 f'Platform title_id={reviewer.TITLE} exit_route=system_menu SD_owner=WUT_devoptab', storage,
                 'EXIT trigger=system_exit; ProcUI EXITING observed before cleanup' if home else 'EXIT trigger=Minus; cleaning resources before system transition', storage]
        if home:lines.append('APP ProcUI status=3')
        stages = reviewer.STAGES[:reviewer.STAGES.index('request_menu_begin')]+('runtime_io_finalize_begin',) if self.io_barrier else reviewer.STAGES
        for stage in stages:
            if stage == 'report_close_begin': lines.append('===== END OF REPORT: application resource cleanup reached; result=0 =====')
            lines.append('EXIT stage='+stage)
        if not self.io_barrier:lines.append('EXIT status=EXITING')
        (self.folder/trace).write_text('\n'.join(lines)+'\n')
        self.observations['runs'].append(dict(session=session, menu_launch=True, both_screens=True, minus_pressed=not home,exit_action='home' if home else 'minus',
                                               returned_to_menu=True, no_problem=True))
        return self.folder/trace

    def completion_kit(self,io_barrier=False):
        self.completion = True
        self.io_barrier = io_barrier
        self.kit.update(acceptance_profile='m1-completion-v3' if io_barrier else 'm1-completion-v2',
                        recovery=dict(available=True, protocol='snapshot-v2'),
                        install_check=dict(storage_protocol='snapshot-v2', session_protocol='install-v3' if io_barrier else 'install-v2',
                                           log_directory='ctr2wup/install-check/'+SOURCE))
        (self.folder/'kit.json').write_text(json.dumps(self.kit))
        for path in self.folder.glob('*.txt'):
            path.unlink()
        self.observations['runs'] = []
        self.add_run(1, 0); self.add_run(2, 1)

    def test_io_barrier_requires_real_menu_observations_and_never_accepts_a_hang(self):
        self.completion_kit(io_barrier=True)
        result=reviewer.review(self.folder,SOURCE,self.observations)
        self.assertEqual(result['status'],'PASS',result['checks'])
        self.assertEqual(result['acceptance_profile'],'m1-completion-v3')
        self.assertTrue(result['m1_completion_ready'])
        self.assertEqual(reviewer.review(self.folder,SOURCE)['status'],'INCOMPLETE')
        self.observations['runs'][0]['returned_to_menu']=False
        self.assertEqual(reviewer.review(self.folder,SOURCE,self.observations)['status'],'FAIL')

    def test_io_barrier_missing_reordered_or_late_sd_io_is_not_completion(self):
        for mutation in ('missing','order','late'):
            with self.subTest(mutation=mutation):
                self.completion_kit(io_barrier=True)
                trace=self.folder/'native-install-0000000000000001-00.txt'
                text=trace.read_text()
                if mutation=='missing':text=text.replace('EXIT stage=runtime_io_finalize_begin\n','')
                elif mutation=='order':text=text.replace('EXIT stage=callbacks_cleared\n','').replace('EXIT stage=runtime_io_finalize_begin\n','EXIT stage=runtime_io_finalize_begin\nEXIT stage=callbacks_cleared\n')
                else:text+='EXIT stage=main_return\n'
                trace.write_text(text)
                self.assertNotEqual(reviewer.review(self.folder,SOURCE,self.observations)['status'],'PASS')

    def replace_run(self, number, before, after):
        session = f'{number:016x}'
        result = self.folder/f'result-native-install-0.27.0-{session}-00.txt'
        text = result.read_text().rstrip(' \n').replace(before, after)+'\n'
        self.assertLess(len(text.encode()), 8192)
        result.write_bytes(text.encode().ljust(8191, b' ')+b'\n')
        trace = self.folder/f'native-install-{session}-00.txt'
        trace.write_text(trace.read_text().replace(before, after))

    def remove_run(self, number):
        session = f'{number:016x}'
        (self.folder/f'result-native-install-0.27.0-{session}-00.txt').unlink()
        (self.folder/f'native-install-{session}-00.txt').unlink()
        self.observations['runs'] = [row for row in self.observations['runs'] if row['session'] != session]

    def test_focused_two_launches_reuse_baseline_and_extra_good_launch_is_allowed(self):
        (self.folder/'generated-scratch.txt').write_text('ignored original scratch; no key/save scanning')
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'PASS', result['checks'])
        self.assertTrue(result['candidate_only']); self.assertFalse(result['full_milestone_acceptance'])
        self.assertFalse(result['m1_completion_ready'])
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

    def test_production_preparation_and_distinct_recovery_complete_the_candidate(self):
        self.completion_kit()
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'PASS', result['checks'])
        self.assertTrue(result['m1_completion_ready'])
        self.assertTrue(result['physical_review_required'])
        self.assertFalse(result['full_milestone_acceptance'])
        self.assertEqual(result['acceptance_profile'], 'm1-completion-v2')
        self.assertEqual([row['storage']['phase'] for row in result['runs']], ['PREPARED', 'RECOVERED'])
        self.assertTrue(all(row['result_bytes'] == 8192 and len(row['trace_sha256']) == 64
                            for row in result['runs']))
        self.add_run(3, 1)
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertTrue(result['m1_completion_ready'], result['checks'])
        self.assertEqual(result['runs'][-1]['storage']['phase'], 'COMPLETE')

    def test_completion_cannot_substitute_for_either_original_phase(self):
        self.completion_kit(); self.add_run(3, 1)
        self.remove_run(2)
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'INCOMPLETE', result['checks'])
        self.assertFalse(result['m1_completion_ready'])
        self.add_run(2, 1); self.remove_run(1)
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'INCOMPLETE', result['checks'])
        self.assertFalse(result['m1_completion_ready'])

    def test_conflicting_session_chain_and_replayed_preparation_fail(self):
        self.completion_kit()
        before = 'producer_session=0000000000000001'
        self.replace_run(2, before, 'producer_session=0000000000000009')
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'FAIL', result['checks'])
        self.assertFalse(result['m1_completion_ready'])
        self.replace_run(2, 'producer_session=0000000000000009', before)
        self.add_run(3, 0, phase='PREPARED')
        self.replace_run(3, before, 'producer_session=0000000000000003')
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'FAIL', result['checks'])

    def test_snapshot_flags_adapter_counters_and_identity_are_required(self):
        mutations = (('production_adapter=1', 'production_adapter=0'),
                     ('interruption_prepared=1', 'interruption_prepared=0'),
                     ('pending_rejected=1', 'pending_rejected=0'),
                     ('current_session=0000000000000002', 'current_session=0000000000000001'),
                     ('recovery_session=0000000000000002', 'recovery_session=0000000000000001'),
                     ('recoveries=1;', 'recoveries=0;'),
                     ('writes=1 readbacks=5', 'writes=0 readbacks=5'),
                     ('writes=1 readbacks=5', 'writes=1 readbacks=0'),
                     ('protocol=snapshot-v2', 'protocol=scratch-v1'),
                     ('result=PASS', 'result=FAIL'),
                     ('production_adapter=1', 'production_adapter=1 production_adapter=1'))
        for before, after in mutations:
            with self.subTest(after=after):
                self.completion_kit()
                self.replace_run(2, before, after)
                result = reviewer.review(self.folder, SOURCE, self.observations)
                self.assertEqual(result['status'], 'FAIL', result['checks'])
                self.assertFalse(result['m1_completion_ready'])

    def test_new_profile_rejects_scratch_receipts_and_incomplete_cleanup(self):
        self.completion_kit()
        self.completion = False
        self.add_run(2, 1)
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'FAIL', result['checks'])
        self.completion_kit()
        self.replace_run(1, 'Stage: resources_closed_before_system_transition', 'Stage: startup')
        self.assertFalse(reviewer.review(self.folder, SOURCE, self.observations)['m1_completion_ready'])

    def test_every_extra_failed_attempt_and_missing_pair_blocks_completion(self):
        self.completion_kit()
        trace = self.add_run(3, 1)
        trace.write_text(trace.read_text()+'INSTALL_IDENTITY result=FAIL\n')
        self.assertFalse(reviewer.review(self.folder, SOURCE, self.observations)['m1_completion_ready'])
        self.remove_run(3)
        (self.folder/'native-install-0000000000000003-00.txt').write_text('Partial failed attempt\n')
        result = reviewer.review(self.folder, SOURCE, self.observations)
        self.assertEqual(result['status'], 'INCOMPLETE', result['checks'])
        self.assertFalse(result['m1_completion_ready'])

    def test_physical_outcomes_remain_explicit_and_cannot_be_inferred(self):
        self.completion_kit()
        self.assertFalse(reviewer.review(self.folder, SOURCE)['m1_completion_ready'])
        self.observations['runs'][1]['returned_to_menu'] = False
        self.assertEqual(reviewer.review(self.folder, SOURCE, self.observations)['status'], 'FAIL')

    def test_bound_mcp_capture_is_a_supported_installer_without_accepting_unknown_tools(self):
        self.completion_kit()
        self.observations['installer']='CTR2WUP Install Capture'
        self.assertTrue(reviewer.review(self.folder,SOURCE,self.observations)['m1_completion_ready'])
        self.observations['installer']='Unknown'
        self.assertFalse(reviewer.review(self.folder,SOURCE,self.observations)['m1_completion_ready'])

    def test_whole_ftp_parent_selects_exact_source_without_reading_older_sources(self):
        self.completion_kit()
        parent = self.folder/'ftp-return'; parent.mkdir()
        current = parent/SOURCE; current.mkdir()
        for path in list(self.folder.iterdir()):
            if path.is_file():
                path.rename(current/path.name)
        # Neither the legacy parent reports nor a sibling source should be read.
        (parent/'kit.json').write_text('Unrelated old manifest; deliberately invalid JSON')
        (parent/'result-native-install-0.27.0-0000000000000009-00.txt').write_text('Old attempt')
        previous = parent/('b'*40); previous.mkdir()
        (previous/'kit.json').write_text('Unrelated old source; deliberately invalid JSON')
        result = reviewer.review(parent, SOURCE, self.observations)
        self.assertTrue(result['m1_completion_ready'], result['checks'])
        direct = reviewer.review(current, SOURCE, self.observations)
        self.assertEqual(direct, result)
        wrong = current/'result-native-install-0.27.0-0000000000000009-00.txt'
        wrong.write_text('Foreign or malformed file inside current source')
        self.assertFalse(reviewer.review(parent, SOURCE, self.observations)['m1_completion_ready'])

    def test_source_directory_symlink_manifest_protocol_and_duplicate_json_fail_closed(self):
        self.completion_kit()
        parent = self.folder/'ftp-return'; parent.mkdir()
        try: (parent/SOURCE).symlink_to(self.folder, target_is_directory=True)
        except (OSError, NotImplementedError): self.skipTest('Symlinks unavailable')
        with self.assertRaises(ValueError): reviewer.review(parent, SOURCE, self.observations)
        self.kit['install_check']['log_directory'] = 'ctr2wup/install-check/'+('b'*40)
        (self.folder/'kit.json').write_text(json.dumps(self.kit))
        with self.assertRaises(ValueError): reviewer.review(self.folder, SOURCE, self.observations)
        with self.assertRaises(ValueError): reviewer.load_json('{"source_commit":"a","source_commit":"b"}')


if __name__ == '__main__':
    unittest.main()
