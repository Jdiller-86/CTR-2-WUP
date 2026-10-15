"""Original structural delivery/return fixtures; no console acceptance inferred."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/filename)
    value = importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value


collector = module('round_collector', 'collect-hardware-results.py')
analyzer = module('round_analyzer', 'analyze-hardware-results.py')
package = module('round_package', 'package-hardware-round.py')
COMMIT = 'a'*40


class HardwareReturnTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name);self.sd = self.root/'sd';(self.sd/'ctr2wup').mkdir(parents=True)
        self.kit = {'kind': 'ctr2wup-hardware-round', 'version': collector.VERSION, 'source_commit': COMMIT}
        self.name = 'result-compiled-pica-draws-0.24.1-0000000000000001-00.txt'
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
        extra = self.sd/'ctr2wup/native-software-scene-0.24.1-0000000000000002-00.txt'
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


class HardwarePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.scene=self.root/'scene';self.app=self.root/'draw'
        self.scene.mkdir();(self.app/'wiiu').mkdir(parents=True)
        header=bytearray(92);header[:8]=b'\x7fELF\x01\x02\x01\xca';header[16:20]=b'\xfe\x01\x00\x14'
        struct.pack_into('>I',header,32,52)
        for offset,value in ((40,52),(46,40),(48,1),(50,0)):struct.pack_into('>H',header,offset,value)
        records=[]
        for folder,names in ((self.scene,package.SCENE),(self.app/'wiiu',package.DRAW)):
            rows=[]
            for name in names:
                data=bytes(header) if name.endswith('.rpx') else b'WUHBoriginal structural fixture'
                (folder/name).write_bytes(data);rows.append({'name':name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
            records.append(rows)
        self.native={'ok':True,'runtime_version':package.VERSION,'target':'scene-check','source_commit':COMMIT,'source_dirty':False,'artifacts':records[0]}
        checks=[]
        for screen,width in (('top',400),('bottom',320)):
            data=f'P6\n{width} 240\n255\n'.encode()+bytes(width*240*3)
            (self.app/f'frame-0-{screen}.ppm').write_bytes(data)
            checks.append({'screen':screen,'width':width,'height':240,'pixels':width*240,'exact_match':True,'sha256':hashlib.sha256(data).hexdigest()})
        app={'application':'draw','wiiu_artifacts':records[1],'independent_frame_checks':checks,
             'generation':{'input_identity':{'program_id':'000400000ff00f0c'}},
             'guest':{'state':'exited','fault':'ok','fallback':0,'messages':['CTR2WUP compiled PICA draws PASS'],
                      'pica_render':{'draws':4,'triangles':4,'fragments':97200,'shader_instructions':36}}}
        self.draw={'ok':True,'version':package.VERSION,'source_commit':COMMIT,'source_dirty':False,'libctru_pin':package.release.LIBCTRU,'applications':[app]}
        self.report=self.root/'draw-report.json';self.sdk=self.root/'sdk.txt';self.sdk.write_text(package.release.SDK_IMAGE+'\n')
        self.patch=patch.object(package,'identity',return_value=COMMIT);self.patch.start();self.addCleanup(self.patch.stop)

    def validate(self):
        (self.scene/'build-report.json').write_text(json.dumps(self.native));self.report.write_text(json.dumps(self.draw))
        return package.validate_inputs(self.scene,self.report,self.app,self.sdk)

    def test_matching_original_bytes_are_checked(self):
        commit,entries=self.validate();self.assertEqual(commit,COMMIT);self.assertEqual(len(entries),6)

    def test_source_dirty_missing_pixels_wrong_counters_and_sdk_fail(self):
        saved=copy.deepcopy(self.draw)
        for mutation in ('dirty','source','pixels','counters'):
            self.draw=copy.deepcopy(saved)
            if mutation=='dirty':self.draw['source_dirty']=True
            if mutation=='source':self.draw['source_commit']='b'*40
            if mutation=='pixels':self.draw['applications'][0]['independent_frame_checks'][0]['exact_match']=False
            if mutation=='counters':self.draw['applications'][0]['guest']['pica_render']['draws']=3
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):self.validate()
        self.draw=saved;self.sdk.write_text('unverified SDK')
        with self.assertRaises(ValueError):self.validate()

    def test_changed_binary_is_rejected(self):
        (self.scene/package.SCENE[1]).write_bytes(b'WUHBchanged')
        with self.assertRaises(ValueError):self.validate()
