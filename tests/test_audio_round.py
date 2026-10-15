"""Original Audio Streams delivery/FTP-return fixtures; no console acceptance."""
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'a'*40
SESSION = '0000000000000001'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


collector = module('audio_round_collector', 'collect-audio-results.py')
package = module('audio_round_package', 'package-audio-round.py')


class AudioReturnTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sd = self.root/'download'
        self.folder = self.sd/'ctr2wup'
        self.folder.mkdir(parents=True)
        self.kit = {'kind': 'ctr2wup-audio-round', 'version': collector.VERSION,
                    'source_commit': COMMIT, 'source_dirty': False}
        self.result_name = f'result-native-audio-streams-0.26.0-{SESSION}-00.txt'
        self.trace_name = f'native-audio-streams-{SESSION}-00.txt'
        self.result_data = self.result()
        (self.folder/self.result_name).write_bytes(self.result_data)
        (self.folder/self.trace_name).write_bytes(self.trace())

    def result(self, commit=COMMIT, application=collector.APPLICATION, version=collector.VERSION,
               session=SESSION, filename=None, trace_name=None):
        text = ('CTR2WUP result report\n'
                f'Application: {application} | Build: {version}\nSession ticks: {session}\n'
                f'File: {filename or self.result_name}\n'
                f'Source: {commit} dirty=0 | Evidence schema: 2\n'
                'Stage: cleanup\nHost exit result: 0\nGuide: COMPLETE | passed=4 failed=0 skipped=0\n'
                f'Full trace: SAVING | file={trace_name or self.trace_name} errno=0\n'
                '1. Mono stream: PASS | User confirmed\n2. Stereo stream: PASS | User confirmed\n'
                'Stream native: status=complete result=ok prepared=1 running=0/0\n'
                'Stream counters: prepares=7 plays=7 pauses=1 resumes=1 stops=7\n'
                'Portable render: fixtures_ready=1 pulls=18 frames=72000 output_rate=48000\n'
                'Audio sessions: 2\n'
                'STREAM GUIDE SUMMARY passed=8 failed=0 skipped=0\n')
        return text.encode().ljust(8192, b' ')

    def trace(self, commit=COMMIT, version=collector.VERSION, application=collector.APPLICATION):
        return ('CTR2WUP Wii U test report\n'
                f'Application: {application} | Build: {version}\n'
                f'BUILD source_commit={commit} source_dirty=0 evidence_schema=2 result_bytes=8192\n'
                'STREAM RENDER step=1 session=1 source_rate=32000 seek_sample=0 voices=1 output_rate=48000 frames=72000\n'
                'STREAM NATIVE stage=complete status=complete result=ok running=0/0 frames=72000\n'
                'END OF REPORT\nEXIT stage=main_return result=0\n').encode()

    def collect(self, name='return.zip'):
        return collector.collect(self.sd, self.root/name, self.kit)

    def test_ftp_pair_exact_bytes_audio_and_named_manual_results(self):
        report = self.collect()
        self.assertEqual(report['warnings'], [])
        self.assertEqual(len(report['files']), 2)
        summary = next(row for row in report['summaries'] if row['kind'] == 'result')
        self.assertTrue(summary['guide_complete'])
        self.assertEqual(len(summary['named_results']), 2)
        self.assertEqual(len(summary['audio_records']), 1)
        self.assertEqual(len(summary['native_summary']), 4)
        self.assertFalse(report['guest_dsp_accepted'])
        self.assertFalse(report['full_milestone_acceptance'])
        with zipfile.ZipFile(self.root/'return.zip') as archive:
            self.assertIsNone(archive.testzip())
            for row in report['files']:
                data = archive.read(row['path'])
                self.assertEqual(len(data), row['size'])
                self.assertEqual(hashlib.sha256(data).hexdigest(), row['sha256'])
        self.assertEqual((self.folder/self.result_name).read_bytes(), self.result_data)

    def test_unknown_private_files_are_not_read_and_every_drop_warns(self):
        for name in ('game.cia', 'private.keys', 'filesystem-test.snapshot', 'result-native-audio-streams-0.25.0-'+SESSION+'-00.txt'):
            (self.folder/name).write_bytes(b'never publish this private negative fixture')
        report = self.collect()
        self.assertEqual(len(report['files']), 2)
        self.assertTrue(any('4 unrelated entries' in warning for warning in report['warnings']))
        with zipfile.ZipFile(self.root/'return.zip') as archive:
            self.assertNotIn(b'never publish', b''.join(archive.read(name) for name in archive.namelist()))
            self.assertNotIn(b'private.keys', archive.read('collection.json'))

    def test_wrong_source_version_app_dirty_and_header_identity_are_excluded(self):
        originals = {'wrong_source': self.result(commit='b'*40), 'wrong_app': self.result(application='native-audio-check'),
                     'wrong_version': self.result(version='0.25.0'), 'wrong_session': self.result(session='0'*16),
                     'wrong_filename': self.result(filename='../private.txt'), 'dirty': self.result().replace(b'dirty=0', b'dirty=1'),
                     'truncated': self.result()[:-1]}
        for name, data in originals.items():
            with self.subTest(name=name):
                (self.folder/self.result_name).write_bytes(data)
                report = self.collect(name+'.zip')
                self.assertEqual(len(report['files']), 1)
                self.assertTrue(any('identity differs' in warning for warning in report['warnings']))
                self.assertTrue(any('matching result missing' in warning for warning in report['warnings']))

    def test_trace_wrong_source_and_dirty_headers_are_excluded(self):
        for name, data in [('source', self.trace(commit='b'*40)), ('version', self.trace(version='0.25.0')),
                           ('application', self.trace(application='native-audio-check')),
                           ('dirty', self.trace().replace(b'source_dirty=0', b'source_dirty=1'))]:
            with self.subTest(name=name):
                (self.folder/self.trace_name).write_bytes(data)
                report = self.collect(name+'.zip')
                self.assertEqual(len(report['files']), 1)
                self.assertTrue(any('full trace missing' in warning for warning in report['warnings']))

    def test_conflicting_duplicate_identity_headers_are_rejected(self):
        for field in (f'Application: other | Build: {collector.VERSION}',
                      f'BUILD source_commit={"b"*40} source_dirty=0 evidence_schema=2 result_bytes=8192'):
            (self.folder/self.trace_name).write_bytes(self.trace()+field.encode()+b'\n')
            report = self.collect('duplicate-'+str(len(field))+'.zip')
            self.assertEqual(len(report['files']), 1)
            self.assertTrue(any('identity differs' in warning for warning in report['warnings']))

    def test_missing_pair_retains_partial_and_failed_evidence(self):
        (self.folder/self.trace_name).unlink()
        report = self.collect()
        self.assertEqual(len(report['files']), 1)
        self.assertTrue(any('full trace missing' in warning for warning in report['warnings']))
        (self.folder/self.result_name).unlink()
        (self.folder/self.trace_name).write_bytes(self.trace().replace(b'END OF REPORT', b'INTERRUPTED'))
        report = self.collect('trace-only.zip')
        self.assertFalse(report['summaries'][0]['cleanup_seen'])
        self.assertTrue(any('matching result missing' in warning for warning in report['warnings']))

    def test_result_trace_collision_suffix_hint_is_checked(self):
        actual = self.trace_name.replace('-00.txt', '-01.txt')
        (self.folder/self.trace_name).rename(self.folder/actual)
        (self.folder/self.result_name).write_bytes(self.result(trace_name=actual))
        self.assertEqual(self.collect()['warnings'], [])
        for index, hint in enumerate(('../private.txt', actual.replace(SESSION, '0'*16))):
            (self.folder/self.result_name).write_bytes(self.result(trace_name=hint))
            report = self.collect(f'wrong-{index}.zip')
            self.assertTrue(any('pair not accepted' in warning for warning in report['warnings']))

    def test_symlink_files_and_selected_directories_are_rejected(self):
        original = self.root/'original.txt'
        original.write_bytes(self.result_data)
        (self.folder/self.result_name).unlink()
        try:
            (self.folder/self.result_name).symlink_to(original)
        except OSError:
            self.skipTest('Symlink creation unavailable')
        report = self.collect()
        self.assertEqual(len(report['files']), 1)
        self.assertTrue(any('not a regular file' in warning for warning in report['warnings']))
        linked = self.root/'linked-download'
        linked.symlink_to(self.sd, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            collector.collect(linked, self.root/'linked.zip', self.kit)

    def test_changed_file_race_is_fatal_without_output(self):
        real_fstat = collector.os.fstat
        calls = 0
        def changed(descriptor):
            nonlocal calls
            calls += 1
            row = real_fstat(descriptor)
            if calls == 2:
                (self.folder/self.trace_name).write_bytes(b'changed while downloaded')
            return row
        with patch.object(collector.os, 'fstat', side_effect=changed):
            with self.assertRaisesRegex(RuntimeError, 'changed'):
                self.collect()
        self.assertFalse((self.root/'return.zip').exists())

    def test_path_and_descriptor_timestamp_semantics_may_differ(self):
        original = collector.os.fstat
        def descriptor_stat(fd):
            row = original(fd)
            return SimpleNamespace(st_dev=row.st_dev, st_ino=row.st_ino,
                st_mode=row.st_mode, st_size=row.st_size,
                st_mtime_ns=row.st_mtime_ns, st_ctime_ns=row.st_ctime_ns+1000000000)
        with patch.object(collector.os, 'fstat', side_effect=descriptor_stat):
            report = self.collect()
        self.assertEqual(len(report['files']), 2)
        self.assertEqual(report['warnings'], [])

    def test_bounds_and_overwrite_preserve_original_files(self):
        (self.folder/self.trace_name).write_bytes(b'x'*(collector.MAX_FILE+1))
        report = self.collect()
        self.assertTrue(any('exceeds 4 MiB' in warning for warning in report['warnings']))
        with self.assertRaises(FileExistsError):
            self.collect()
        with patch.object(collector, 'MAX_TOTAL', 8191):
            with self.assertRaisesRegex(ValueError, '24 MiB'):
                self.collect('total.zip')
        with patch.object(collector, 'MAX_FILES', 1):
            with self.assertRaisesRegex(ValueError, '64'):
                self.collect('count.zip')

    def test_wrong_kit_or_no_matches_never_creates_return(self):
        for field, value in [('kind', 'other'), ('version', '0.25.0'), ('source_commit', '../private'), ('source_dirty', True)]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                collector.collect(self.sd, self.root/(field+'.zip'), {**self.kit, field: value})
        (self.folder/self.result_name).unlink()
        (self.folder/self.trace_name).unlink()
        with self.assertRaisesRegex(ValueError, 'No bounded'):
            self.collect()


class AudioPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = self.root/'app'
        self.app.mkdir()
        self.references = self.root/'references'
        self.references.mkdir()
        self.source = self.root/'source'
        (self.source/'docs').mkdir(parents=True)
        (self.source/'tools').mkdir()
        (self.source/'docs/audio-stream-testing.md').write_text('Original guide [Contract](audio-engine.md#limits).\n')
        for name in ('LICENSE', 'THIRD_PARTY_NOTICES'):
            (self.source/name).write_text('Original notice fixture\n')
        (self.source/'tools/collect-audio-results.py').write_bytes((ROOT/'tools/collect-audio-results.py').read_bytes())
        self.licenses = self.root/'sdk-licenses'
        self.licenses.mkdir()
        (self.licenses/'wut.txt').write_text('Original structural license fixture\n')
        self.sdk = self.root/'sdk.txt'
        self.sdk.write_text(package.release.SDK_IMAGE+'\n')
        header = bytearray(92)
        header[:8] = b'\x7fELF\x01\x02\x01\xca'
        header[16:20] = b'\xfe\x01\x00\x14'
        struct.pack_into('>I', header, 32, 52)
        for offset, value in ((40, 52), (46, 40), (48, 1), (50, 0)):
            struct.pack_into('>H', header, offset, value)
        records = []
        for name in package.NAMES:
            data = bytes(header) if name.endswith('.rpx') else b'WUHBoriginal structural fixture'
            (self.app/name).write_bytes(data)
            records.append({'name': name, 'size': len(data), 'sha256': package.digest(data)})
        self.native = {'ok': True, 'runtime_version': package.VERSION, 'target': 'audio-stream-check',
                       'profile': package.APPLICATION, 'source_commit': COMMIT, 'source_dirty': False,
                       'input_identity': None, 'artifacts': records}
        self.reference = {'ok': True, 'version': package.VERSION, 'source_commit': COMMIT, 'source_dirty': False,
                          **{name: True for name in package.REFERENCE_FLAGS}, 'cases': 240, 'output_frames': 48000,
                          'sample_rates': sorted(package.REFERENCE_RATES), 'modes': ['normal', 'sanitized'],
                          'original_vectors_sha256': '1'*64, 'pcm_results_sha256': '2'*64, 'guided_scenes': []}
        for scene in sorted(package.SCENES):
            name = f'scene-{scene}-48k.wav'
            stream = io.BytesIO()
            with wave.open(stream, 'wb') as output:
                output.setnchannels(2)
                output.setsampwidth(2)
                output.setframerate(48000)
                output.writeframes(b'\x00'*32)
            data = stream.getvalue()
            (self.references/name).write_bytes(data)
            self.reference['guided_scenes'].append({'id': scene, 'wav_file': name, 'wav_sha256': package.digest(data),
                                                    'pcm_sha256': package.digest(b'\x00'*32), 'output_frames': 8,
                                                    'native_pause_or_console_verified': False})
        self.report = self.references/'report.json'
        self.write_reports()
        for name, replacement in [('identity', lambda: COMMIT), ('ROOT', self.source)]:
            stub = patch.object(package, name, replacement)
            stub.start()
            self.addCleanup(stub.stop)
        self.source_zip = io.BytesIO()
        with zipfile.ZipFile(self.source_zip, 'w') as archive:
            archive.writestr('CTR2WUP/README.md', 'Original source fixture')
        stub = patch.object(package.subprocess, 'check_output', return_value=self.source_zip.getvalue())
        stub.start()
        self.addCleanup(stub.stop)

    def write_reports(self):
        (self.app/'build-report.json').write_text(json.dumps(self.native))
        self.report.write_text(json.dumps(self.reference))
        (self.references/'guided-scenes.json').write_text(json.dumps({'version': package.VERSION,
            'output_rate': 48000, 'source_commit': COMMIT, 'source_dirty': False,
            'original_inputs_only': True, 'independent_reference': True, 'scenes': self.reference['guided_scenes']}))

    def validate(self):
        self.write_reports()
        return package.validate_inputs(self.app, self.report, self.sdk)

    def package(self, name='kit'):
        self.write_reports()
        return package.package(self.app, self.report, self.sdk, self.licenses, self.root/name)

    def test_original_bytes_source_notices_hashes_links_and_crc_are_verified(self):
        (self.app/'secret.keys').write_text('must never publish unknown app inputs')
        (self.references/'unknown.cia').write_text('must never publish unknown reference inputs')
        report = self.package()
        self.assertTrue(report['zip_crc_verified'])
        self.assertTrue(report['exact_byte_readback'])
        self.assertFalse(report['console_verified'])
        with zipfile.ZipFile(self.root/'kit'/report['archive']) as archive:
            self.assertIsNone(archive.testzip())
            names = archive.namelist()
            self.assertEqual(len(names), len(set(names)))
            manifest = json.loads(archive.read('manifest.json'))
            self.assertEqual(manifest['source_commit'], COMMIT)
            self.assertTrue(manifest['finite_prerender'])
            self.assertFalse(manifest['guest_dsp_accepted'])
            self.assertIn(COMMIT.encode(), archive.read('START-HERE.md'))
            for row in manifest['files']:
                data = archive.read(row['path'])
                self.assertEqual(package.digest(data), row['sha256'])
                self.assertEqual(len(data), row['size'])
            hashes = archive.read('SHA256SUMS.txt').decode().splitlines()
            for line in hashes:
                expected, name = line.split('  ', 1)
                self.assertEqual(package.digest(archive.read(name)), expected)
            self.assertFalse(any('secret.keys' in name or 'unknown.cia' in name for name in names))
            self.assertEqual(sum(name.endswith('.wav') for name in names), 7)
        self.assertEqual(package.release.sha256(self.root/'kit'/report['archive']), report['sha256'])
        with self.assertRaises(FileExistsError):
            self.package()

    def test_stale_dirty_unverified_reports_and_wrong_sdk_fail_before_output(self):
        originals = copy.deepcopy(self.native), copy.deepcopy(self.reference)
        for kind, field, value in [('native', 'source_commit', 'b'*40), ('native', 'source_dirty', True),
                                   ('native', 'profile', 'native-audio-check'), ('reference', 'source_commit', 'b'*40),
                                   ('reference', 'source_dirty', True), ('reference', 'independent_reference', False),
                                   ('reference', 'exact_linear_resample_q15_mix', False), ('reference', 'cases', 239),
                                   ('reference', 'modes', ['normal']), ('reference', 'version', '0.25.0')]:
            self.native, self.reference = copy.deepcopy(originals)
            (self.native if kind == 'native' else self.reference)[field] = value
            with self.subTest(kind=kind, field=field), self.assertRaises(ValueError):
                self.package()
            self.assertFalse((self.root/'kit').exists())
        self.native, self.reference = copy.deepcopy(originals)
        self.sdk.write_text('wrong SDK')
        with self.assertRaisesRegex(ValueError, 'pinned SDK'):
            self.package()

    def test_changed_binary_and_reference_waves_are_rejected(self):
        (self.app/package.NAMES[1]).write_bytes(b'WUHBchanged original fixture')
        with self.assertRaisesRegex(ValueError, 'receipt'):
            self.package()
        row = self.native['artifacts'][1]
        data = (self.app/package.NAMES[1]).read_bytes()
        row.update(size=len(data), sha256=package.digest(data))
        (self.references/'scene-left-48k.wav').write_bytes(b'changed original reference')
        with self.assertRaisesRegex(ValueError, 'WAVE bytes'):
            self.package()

    def test_reference_filename_traversal_and_duplicates_fail(self):
        for name in ('../original-0.wav', '/original-0.wav', 'scene-right-48k.wav', 'private\\file.wav'):
            self.reference['guided_scenes'][0]['wav_file'] = name
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.package()

    def test_mismatched_scene_metadata_and_pcm_format_fail(self):
        original = copy.deepcopy(self.reference)
        for field, value in [('guided_scenes', []), ('sample_rates', None), ('modes', ['normal', 'normal']),
                             ('original_vectors_sha256', None)]:
            self.reference = copy.deepcopy(original)
            self.reference[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.package()
        self.reference = copy.deepcopy(original)
        self.write_reports()
        metadata_path = self.references/'guided-scenes.json'
        metadata = json.loads(metadata_path.read_text())
        metadata['source_dirty'] = True
        metadata_path.write_text(json.dumps(metadata))
        with self.assertRaisesRegex(ValueError, 'metadata'):
            package.validate_inputs(self.app, self.report, self.sdk)
        for field, value in [('output_frames', 9), ('pcm_sha256', 'b'*64)]:
            self.reference = copy.deepcopy(original)
            self.reference['guided_scenes'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.package()

    def test_source_archive_privacy_guard_and_sdk_symlinks_fail(self):
        bad = io.BytesIO()
        with zipfile.ZipFile(bad, 'w') as archive:
            archive.writestr('CTR2WUP/private/game.cia', b'original forbidden path fixture')
        with patch.object(package.subprocess, 'check_output', return_value=bad.getvalue()):
            with self.assertRaisesRegex(ValueError, 'tracked source path'):
                self.package()
        try:
            (self.licenses/'linked.txt').symlink_to(self.sdk)
        except OSError:
            return
        with self.assertRaisesRegex(ValueError, 'symlinks'):
            self.package()

    def test_source_changes_before_publication_fail(self):
        with patch.object(package, 'identity', side_effect=[COMMIT, 'b'*40]):
            with self.assertRaisesRegex(ValueError, 'Source changed'):
                self.package()
        self.assertFalse((self.root/'kit').exists())


if __name__ == '__main__':
    unittest.main()
