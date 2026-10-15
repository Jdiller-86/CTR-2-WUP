"""Original structural delivery/return fixtures; no console acceptance inferred."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/filename)
    value = importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value


collector = module('round_collector', 'collect-hardware-results.py')
analyzer = module('round_analyzer', 'analyze-hardware-results.py')
COMMIT = 'a'*40


class HardwareReturnTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name);self.sd = self.root/'sd';(self.sd/'ctr2wup').mkdir(parents=True)
        self.kit = {'kind': 'ctr2wup-hardware-round', 'version': collector.VERSION, 'source_commit': COMMIT}
        self.name = f'result-compiled-pica-draws-{collector.VERSION}-0000000000000001-00.txt'
        self.data = (f'CTR2WUP result report\nSource: {COMMIT} dirty=0 | Evidence schema: 2\n'
            'Stage: cleanup\nHost exit result: 0\nGuide: COMPLETE | passed=3 failed=0 skipped=0\n'
            '1. Guest triangle pictures: PASS | User confirmed\n').encode().ljust(8192, b' ')
        (self.sd/'ctr2wup'/self.name).write_bytes(self.data)

    def test_collect_reparse_preserves_inputs_and_excludes_unrelated_payloads(self):
        for name in ('filesystem-test.snapshot', 'secret.keys', 'result-native-0.20.0-0000000000000001-00.txt', 'game.cia'):
            (self.sd/'ctr2wup'/name).write_bytes(b'private original negative fixture')
        report = collector.collect(self.sd, self.root/'return.zip', self.kit, {'draw_minus': 'returned'})
        self.assertEqual(len(report['files']), 1)
        self.assertEqual((self.sd/'ctr2wup'/self.name).read_bytes(), self.data)
        result = analyzer.analyze(self.root/'return.zip', COMMIT)
        self.assertTrue(result['summaries'][0]['complete_evidence'])
        self.assertFalse(result['full_milestone_acceptance'])
        self.assertEqual(result['observations']['draw_minus'], 'returned')

    def trace(self, version=collector.VERSION, commit=COMMIT, app='compiled-pica-draws'):
        return (f'CTR2WUP Wii U test report\nApplication: {app} | Build: {version}\n'
                f'BUILD source_commit={commit} source_dirty=0 evidence_schema=2 result_bytes=8192\n'
                'PHASE name=scale calls=2 total_us=260000 max_us=132000\n').encode()

    def test_native_unversioned_trace_is_collected_and_reparsed(self):
        name='compiled-pica-draws-0000000000000001-00.txt'
        trace=self.trace();(self.sd/'ctr2wup'/name).write_bytes(trace)
        report=collector.collect(self.sd,self.root/'trace.zip',self.kit)
        self.assertEqual(len(report['files']),2);self.assertEqual(report['warnings'],[])
        result=analyzer.analyze(self.root/'trace.zip',COMMIT)
        summary=next(row for row in result['summaries'] if row['kind']=='trace')
        self.assertTrue(summary['source_identity_seen']);self.assertEqual(summary['phase_records'],1)
        self.assertEqual((self.sd/'ctr2wup'/name).read_bytes(),trace)

    def test_results_subdirectory_preserves_legacy_intake_and_rejects_ambiguous_duplicates(self):
        folder = self.sd/'ctr2wup'
        results = folder/'results'; results.mkdir()
        (folder/self.name).rename(results/self.name)
        trace_name = 'compiled-pica-draws-0000000000000001-00.txt'
        (folder/trace_name).write_bytes(self.trace())
        (folder/'filesystem-test.snapshot').write_bytes(b'Original save is excluded')
        report = collector.collect(self.sd,self.root/'results.zip',self.kit)
        self.assertEqual(len(report['files']),2)
        self.assertEqual(report['warnings'],[])
        (folder/self.name).write_bytes(self.data)
        with self.assertRaisesRegex(ValueError,'Duplicate log filename'):
            collector.collect(self.sd,self.root/'duplicate-layout.zip',self.kit)

    def test_legacy_round_remains_source_bound_after_tool_upgrade(self):
        old = '0.24.1'
        (self.sd/'ctr2wup'/self.name).rename(self.sd/'ctr2wup'/self.name.replace(collector.VERSION, old))
        name = 'compiled-pica-draws-0000000000000001-00.txt'
        (self.sd/'ctr2wup'/name).write_bytes(self.trace(version=old))
        report = collector.collect(self.sd, self.root/'legacy.zip', {**self.kit, 'version': old})
        self.assertEqual(report['version'], old);self.assertEqual(report['collector_version'], collector.VERSION)
        result = analyzer.analyze(self.root/'legacy.zip', COMMIT)
        self.assertEqual(result['version'], old);self.assertEqual(result['warnings'], [])
        self.assertEqual(len(result['summaries']), 2)

    def test_partial_native_trace_without_result_is_preserved(self):
        (self.sd/'ctr2wup'/self.name).unlink()
        name='compiled-pica-draws-0000000000000001-00.txt'
        (self.sd/'ctr2wup'/name).write_bytes(self.trace())
        report=collector.collect(self.sd,self.root/'interrupted.zip',self.kit)
        self.assertEqual(len(report['files']),1)
        self.assertTrue(analyzer.analyze(self.root/'interrupted.zip',COMMIT)['summaries'][0]['source_identity_seen'])

    def test_trace_pair_uses_reported_collision_suffix(self):
        name='compiled-pica-draws-0000000000000001-01.txt'
        data=self.data.rstrip()+f'\nFull trace: SAVING | file={name} errno=0\n'.encode()
        (self.sd/'ctr2wup'/self.name).write_bytes(data)
        (self.sd/'ctr2wup'/name).write_bytes(self.trace())
        report=collector.collect(self.sd,self.root/'collision.zip',self.kit)
        self.assertEqual(report['warnings'],[])
        self.assertEqual(analyzer.analyze(self.root/'collision.zip',COMMIT)['warnings'],[])

    def test_unversioned_trace_rejects_other_build_source_and_application(self):
        name='compiled-pica-draws-0000000000000001-00.txt'
        for kind,trace in [('version',self.trace(version='0.23.0')),('source',self.trace(commit='b'*40)),
                           ('application',self.trace(app='native-software-scene'))]:
            with self.subTest(kind=kind):
                (self.sd/'ctr2wup'/name).write_bytes(trace)
                output=self.root/(kind+'-trace.zip')
                report=collector.collect(self.sd,output,self.kit)
                self.assertEqual(len(report['files']),1)
                self.assertTrue(any('identity differs' in warning for warning in report['warnings']))
                with zipfile.ZipFile(output) as archive:self.assertNotIn('logs/'+name,archive.namelist())

    def test_missing_trace_is_reported_without_discarding_result(self):
        report=collector.collect(self.sd,self.root/'result-only.zip',self.kit)
        self.assertEqual(len(report['files']),1)
        self.assertTrue(any('full trace missing' in warning for warning in report['warnings']))
        # Also diagnose older collector receipts that silently omitted traces.
        with zipfile.ZipFile(self.root/'result-only.zip') as archive:entries={n:archive.read(n) for n in archive.namelist()}
        receipt=json.loads(entries['collection.json']);receipt['warnings']=[]
        entries['collection.json']=json.dumps(receipt).encode()
        with zipfile.ZipFile(self.root/'old-collector.zip','w') as archive:
            for name,data in entries.items():archive.writestr(name,data)
        result=analyzer.analyze(self.root/'old-collector.zip',COMMIT)
        self.assertTrue(any('full trace missing' in warning for warning in result['warnings']))

    def test_returned_unversioned_trace_requires_source_identity_even_with_new_hash(self):
        collector.collect(self.sd,self.root/'result.zip',self.kit)
        with zipfile.ZipFile(self.root/'result.zip') as archive:entries={n:archive.read(n) for n in archive.namelist()}
        name='logs/compiled-pica-draws-0000000000000001-00.txt';data=self.trace(commit='b'*40)
        entries[name]=data;receipt=json.loads(entries['collection.json'])
        receipt['files'].append({'path':name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
        entries['collection.json']=json.dumps(receipt).encode()
        with zipfile.ZipFile(self.root/'wrong-trace.zip','w') as archive:
            for name,data in entries.items():archive.writestr(name,data)
        with self.assertRaisesRegex(ValueError,'trace build/source identity'):analyzer.analyze(self.root/'wrong-trace.zip',COMMIT)

    def test_partial_and_failed_reports_are_included_without_claiming_complete(self):
        (self.sd/'ctr2wup'/self.name).write_bytes(b'Session: STARTED\nGuide: not completed\n')
        collector.collect(self.sd, self.root/'partial.zip', self.kit)
        result = analyzer.analyze(self.root/'partial.zip', COMMIT)
        self.assertFalse(result['summaries'][0]['complete_evidence'])

    def test_oversized_files_and_symlinks_are_never_followed(self):
        extra = self.sd/f'ctr2wup/native-software-scene-{collector.VERSION}-0000000000000002-00.txt'
        extra.write_bytes(b'x'*(collector.MAX_FILE+1))
        result = collector.collect(self.sd, self.root/'bounded.zip', self.kit)
        self.assertEqual(len(result['files']), 1);self.assertTrue(any('exceeds 4 MiB' in w for w in result['warnings']))
        extra.unlink()
        try:extra.symlink_to(self.root/'outside.txt')
        except OSError:return  # Symlink creation is restricted on some Windows runners.
        (self.root/'outside.txt').write_bytes(b'never read this')
        result = collector.collect(self.sd, self.root/'link.zip', self.kit)
        self.assertEqual(len(result['files']), 1)

    def test_existing_return_and_wrong_kit_are_preserved(self):
        output = self.root/'existing.zip';output.write_bytes(b'owner output')
        with self.assertRaises(FileExistsError):collector.collect(self.sd, output, self.kit)
        self.assertEqual(output.read_bytes(), b'owner output')
        with self.assertRaises(ValueError):collector.collect(self.sd, self.root/'bad.zip', {**self.kit, 'version': '0.23.0'})

    def test_tampered_duplicate_and_traversal_return_payloads_fail(self):
        collector.collect(self.sd, self.root/'valid.zip', self.kit)
        with zipfile.ZipFile(self.root/'valid.zip') as archive:entries={n:archive.read(n) for n in archive.namelist()}
        for kind in ('changed', 'path', 'identity'):
            changed = dict(entries)
            if kind == 'changed':changed['logs/'+self.name] = b'changed'
            if kind == 'path':changed['../private.txt'] = b'forbidden'
            if kind == 'identity':
                receipt=json.loads(changed['collection.json']);receipt['source_commit']='b'*40
                changed['collection.json']=json.dumps(receipt).encode()
            path=self.root/(kind+'.zip')
            with zipfile.ZipFile(path, 'w') as archive:
                for name,data in changed.items():archive.writestr(name,data)
            with self.subTest(kind=kind), self.assertRaises(ValueError):analyzer.analyze(path, COMMIT)
