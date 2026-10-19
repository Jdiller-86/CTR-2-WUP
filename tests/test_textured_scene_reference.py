"""Original sampler mathematics and malformed emitted fragment-program checks."""
from fractions import Fraction as F
import importlib.util
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('textured_scene_reference', ROOT/'tools/validate-textured-scenes.py')
ORACLE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ORACLE
SPEC.loader.exec_module(ORACLE)


class TexturedSceneReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.output = Path(cls.directory.name)
        cls.binary = cls.output/'scene-probe'
        ORACLE.compile_probe(cls.binary, os.environ.get('CC', 'cc'))
        # Capture actual native-adapter output, rather than embed a duplicate
        # shader byte fixture. The GX2 API model never executes GPU commands.
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                 '-I'+str(ROOT/'runtime/core'), '-I'+str(ROOT/'runtime/wiiu'),
                 '-I'+str(ROOT/'tests/gx2-native')]
        obj, binary = cls.output/'native.o', cls.output/'native-probe'
        subprocess.run(cc+flags+['-DCTR_GX2_SCENE_MOCK', '-Dmemalign=ctr_gx2_test_memalign',
                       '-Dfree=ctr_gx2_test_free', '-c', str(ROOT/'runtime/wiiu/gx2_scene.c'), '-o', str(obj)], check=True)
        subprocess.run(cc+flags+[str(ROOT/'tests/gx2-native/test_native.c'), str(obj), '-o', str(binary)], check=True)
        cls.capture = json.loads(subprocess.check_output([str(binary), '--fragment-programs'], text=True))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_nearest_wrap_and_mirror_exact_boundaries(self):
        cases = ((F(-17, 8), (0, 7, 0)), (F(-2), (0, 0, 0)),
                 (F(-9, 8), (0, 7, 7)), (F(-1), (0, 0, 7)),
                 (F(-1, 65536), (0, 7, 0)), (F(0), (0, 0, 0)),
                 (F(7, 8), (7, 7, 7)), (F(1), (7, 0, 7)),
                 (F(9, 8), (7, 1, 6)), (F(2), (7, 0, 0)))
        for value, expected in cases:
            for wrap, answer in zip((0, 2, 3), expected):
                with self.subTest(value=value, wrap=wrap):
                    self.assertEqual(ORACLE.sampled_texel(value, 8, wrap), answer)
        with self.assertRaises(ValueError): ORACLE.sampled_texel(F(1), 8, 1)

    def test_affine_barycentric_weights_are_exact_and_orientation_independent(self):
        points = ((F(3, 2), F(7, 4)), (F(21, 2), F(11, 4)), (F(9, 2), F(39, 4)))
        weights = (F(1, 4), F(1, 2), F(1, 4))
        point = tuple(sum(w*p[axis] for w, p in zip(weights, points)) for axis in range(2))
        self.assertEqual(ORACLE.barycentric(point, points), weights)
        self.assertEqual(ORACLE.barycentric(point, (points[0], points[2], points[1])),
                         (weights[0], weights[2], weights[1]))
        self.assertIsNone(ORACLE.barycentric(point, ((0, 0), (1, 1), (2, 2))))

    def test_recursive_morton_order_and_original_channels(self):
        self.assertEqual(ORACLE.morton_coordinates(4),
                         [(0, 0), (1, 0), (0, 1), (1, 1), (2, 0), (3, 0), (2, 1), (3, 1),
                          (0, 2), (1, 2), (0, 3), (1, 3), (2, 2), (3, 2), (2, 3), (3, 3)])
        data = ORACLE.original_texture(0, 16, 8)
        pixels = ORACLE.decode_texture(data, 0, 16, 8)
        for x, y in ((0, 0), (8, 0), (0, 7), (15, 7), (9, 3)):
            expected = bytes(((29*x+13*y+31) % 256, (7*x+37*y+89) % 256,
                              (43*x+3*y+149) % 256, (19*x+23*y+53) % 256))
            self.assertEqual(pixels[(y*16+x)*4:(y*16+x+1)*4], expected)

    def test_pica_v_zero_selects_raw_last_row_and_binary_modulation_masks_channels(self):
        case = ORACLE.Case('manual-v-origin', uv=((F(0), F(0)),)*4,
                           modulate=True, color=(1, 0, 1, 1))
        texture = ORACLE.original_texture(0, 8, 8)
        expected, fragments = ORACLE.golden(case, texture)
        self.assertEqual(fragments, 48*48)
        # At (raw x=0,y=7): R=122,G=92,B=170,A=214. G is masked.
        self.assertEqual(expected[(20*64+20)*4:(20*64+21)*4], bytes((122, 0, 170, 214)))
        self.assertEqual(expected[:4], ORACLE.BACKGROUND)

    def test_original_scene_sampling_and_late_failure_rollback(self):
        selected = {'format-00-clamp', 'format-03-repeat', 'format-13-mirror-modulate',
                    'asymmetric-fractional-mixed-wrap', 'inside-scissor', 'outside-scissor',
                    'nonindexed', 'late-unsupported-filter', 'late-truncated-binding', 'late-invalid-uv'}
        cases = [case for case in ORACLE.original_cases() if case.name in selected]
        self.assertEqual(len(cases), len(selected))
        for case in cases:
            with self.subTest(case=case.name):
                result = ORACLE.execute_case(self.binary, self.output, case)
                if case.late_error:
                    self.assertTrue(result['pixels_unchanged'])
                    self.assertTrue(result['state_unchanged'])
                else:
                    self.assertEqual(result['result'], 'ok')

    def test_actual_native_fragment_programs_match_independent_tev_arithmetic(self):
        path = self.output/'fragment-programs.json'
        path.write_text(json.dumps(self.capture))
        report = ORACLE.validate_fragment_programs(path)
        self.assertEqual(report['programs'], 2)
        self.assertEqual(report['arithmetic_cases'], 64)
        self.assertFalse(report['gpu_execution'])
        self.assertFalse(report['console_verified'])

    def test_linear_half_up_and_texel_centers_match_analytic_bytes(self):
        texture=ORACLE.original_texture(0,8,8)
        for name,uv,expected in (
            ('half-up',(F(1,8),F(1,16)),bytes((137,96,192,224))),
            ('center',(F(3,16),F(5,16)),bytes((125,25,207,187)))):
            with self.subTest(name=name):
                case=ORACLE.Case('linear-analytic-'+name,linear=True,uv=(uv,)*4)
                self.assertEqual(ORACLE.sampled_color(ORACLE.decode_texture(texture,0,8,8),8,8,*uv,(0,0),True),expected)
                ORACLE.execute_case(self.binary,self.output,case)
                pixels=(self.output/(case.name+'.rgba')).read_bytes()
                self.assertEqual(pixels[(20*64+20)*4:(20*64+21)*4],expected)

    def test_linear_filters_all_wraps_and_preserves_late_failure_atomicity(self):
        selected={'linear-format-00-clamp','linear-format-03-repeat','linear-format-13-mirror-modulate',
                  'linear-asymmetric-fractional-mixed-wrap','linear-outside-scissor',
                  'linear-maximum-q16-uv','linear-maximum-base-dimension',
                  'linear-late-truncated-binding','linear-late-invalid-uv','linear-mixed-filter','linear-mip-filter',
                  'linear-maximum-area-wide-weights'}
        cases=[case for case in ORACLE.original_cases() if case.name in selected]
        self.assertEqual(len(cases),len(selected))
        for case in cases:
            with self.subTest(case=case.name):ORACLE.execute_case(self.binary,self.output,case)

    def mutate(self, mode, offset, mask):
        row = dict(next(row for row in self.capture['programs'] if row['mode'] == mode))
        program = bytearray.fromhex(row['hex'])
        word = struct.unpack_from('<I', program, offset)[0]
        struct.pack_into('<I', program, offset, word ^ mask)
        row['hex'] = program.hex()
        return row

    def test_fragment_coordinates_must_remain_normalized_original_uv(self):
        with self.assertRaisesRegex(ValueError, 'normalized'):
            ORACLE.decode_fragment_program(self.mutate('replace', 36, 1 << 28), (1, 1, 1, 1), (17, 89, 233, 53))
        with self.assertRaisesRegex(ValueError, 'original UV'):
            ORACLE.decode_fragment_program(self.mutate('replace', 40, 1 << 20), (1, 1, 1, 1), (17, 89, 233, 53))

    def test_fragment_sample_opcode_and_bound_resource_are_checked(self):
        for offset, mask in ((32, 1), (32, 1 << 8), (40, 1 << 15)):
            with self.subTest(offset=offset, mask=mask):
                with self.assertRaisesRegex(ValueError, 'SAMPLE_LZ'):
                    ORACLE.decode_fragment_program(self.mutate('replace', offset, mask), (1, 1, 1, 1), (17, 89, 233, 53))

    def test_legal_but_wrong_alu_channel_changes_decoded_pixel(self):
        row = self.mutate('modulate', 48, 1 << 23)  # R0.y replaces R0.x.
        result = ORACLE.decode_fragment_program(row, (1, 0, 1, 1), (17, 89, 233, 53))
        self.assertEqual(result['color'][0], 0)
        self.assertNotEqual(result['color'], (F(17, 255), 0, F(233, 255), F(53, 255)))

    def test_fragment_clause_bounds_padding_and_end_are_checked(self):
        for mode, offset, mask in (('replace', 0, 1 << 8), ('replace', 120, 1),
                                   ('modulate', 20, 1 << 21)):
            with self.subTest(mode=mode, offset=offset):
                with self.assertRaises(ValueError):
                    ORACLE.decode_fragment_program(self.mutate(mode, offset, mask), (1, 1, 1, 1), (17, 89, 233, 53))

    def test_fragment_program_word_endianness_is_explicit(self):
        row = dict(self.capture['programs'][0])
        program = bytes.fromhex(row['hex'])
        row['hex'] = b''.join(program[at:at+4][::-1] for at in range(0, len(program), 4)).hex()
        with self.assertRaises(ValueError):
            ORACLE.decode_fragment_program(row, (1, 1, 1, 1), (17, 89, 233, 53))
