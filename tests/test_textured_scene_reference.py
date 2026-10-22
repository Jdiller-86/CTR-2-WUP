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
                       '-Dctr_gx2_cache_create=ctr_gx2_test_cache_create',
                       '-Dctr_gx2_cache_destroy=ctr_gx2_test_cache_destroy',
                       '-Dfree=ctr_gx2_test_free', '-c', str(ROOT/'runtime/wiiu/gx2_scene.c'), '-o', str(obj)], check=True)
        subprocess.run(cc+flags+[str(ROOT/'tests/gx2-native/test_native.c'), str(ROOT/'runtime/core/ctr_tev.c'),
                       str(ROOT/'runtime/core/ctr_gx2_cache.c'),
                       str(ROOT/'runtime/core/ctr_fragment.c'), str(obj), '-o', str(binary)], check=True)
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
        self.assertGreater(report['programs'], 35)
        self.assertEqual(report['programs'], len(self.capture['programs']))
        self.assertEqual(report['arithmetic_cases'], len(self.capture['programs'])*64)
        self.assertTrue(any('buffer_color' in row for row in self.capture['programs'][35:]))
        self.assertGreaterEqual(report['max_alu_instructions'], 384)
        self.assertEqual(report['integer_division_cases'], 65026)
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

    def test_tev_byte_operations_have_manual_numeric_anchors(self):
        # a=64, b=128, c=77. RGB and alpha select different packed operand
        # fields but must produce the same independently worked scalar result.
        answers = {0:64, 1:32, 2:192, 3:64, 4:108, 5:0, 8:109, 9:57}
        for operation, expected in answers.items():
            stage = ORACLE.tev_stage(rgb=(3,14,14), operation=(operation,operation),
                operands=((0,4,2),(0,2,0)), constant=(128,33,222,77))
            with self.subTest(operation=operation):
                self.assertEqual(ORACLE.tev_reference((stage,), (0,255,0,255), (64,)*4), (expected,)*4)

    def test_six_stage_chain_has_manual_intermediate_results(self):
        expected = ((89,36,116,194), (126,187,135,161), (126,146,129,252),
                    (255,170,255,174), (128,108,128,174), (184,212,157,191))
        for stage in range(6):
            self.assertEqual(ORACLE.tev_reference(ORACLE.tev_chain()[:stage+1], (0,255,0,255), (128,128,128,255)), expected[stage])
        row = next(row for row in self.capture['programs'] if row['mode'] == 'combiner-27')
        decoded = ORACLE.decode_fragment_program(row, (0,1,0,1), (128,128,128,255))
        self.assertEqual(decoded['color'], tuple(F(v,255) for v in expected[-1]))

    def test_tev_scene_operations_constants_scales_and_late_rollback(self):
        names = {'tev-six-stage-chain', 'tev-six-stage-chain-linear', 'tev-untextured-constant',
                 'tev-previous-alpha-swizzle', 'tev-operands-3', 'tev-operands-13',
                 'tev-scale-0-2', 'tev-scale-2-0', 'tev-scale-2-2'}
        cases = [c for c in ORACLE.tev_cases() if c.name in names or c.late_error or
                 (c.name.startswith('tev-operation-') and not c.linear)]
        self.assertEqual(len(cases), 24)
        for case in cases:
            with self.subTest(case=case.name):ORACLE.execute_case(self.binary,self.output,case)

    def test_buffer_visibility_has_manual_stage_anchors(self):
        constants = ((31,73,127,181), (211,47,89,13), (59,239,7,109),
                     (163,23,193,251), (83,149,227,61))
        initial = (17,199,43,157)
        stages = tuple(ORACLE.tev_stage(rgb=(14,0,0),constant=value) for value in constants)
        consume = ORACLE.tev_stage(rgb=(13,0,0))
        answers = ((0,0,0,0), initial, *constants[:4])
        for stage, expected in enumerate(answers):
            with self.subTest(stage=stage):
                self.assertEqual(ORACLE.tev_reference(stages[:stage]+(consume,), (0,255,0,255),
                                 (19,89,233,53), initial, (15,15)), expected)
        # Stage1 is an arithmetic passthrough but still publishes stage0's
        # pending result. RGB and alpha can retain different historical stages.
        passthrough = ORACLE.tev_stage(rgb=(15,0,0))
        self.assertEqual(ORACLE.tev_reference((stages[0],passthrough,consume), (0,)*4,
                         (0,)*4,initial,(1,1)), constants[0])
        self.assertEqual(ORACLE.tev_reference((stages[0],stages[1],passthrough,consume),
                         (0,)*4,(0,)*4,initial,(1,2)), (*constants[0][:3],constants[1][3]))

    def test_buffer_all_selector_pairs_and_late_state_rollback(self):
        cases = ORACLE.buffer_cases()
        masks = [case for case in cases if case.name.startswith('buffer-masks-')]
        self.assertEqual(len(masks),256)
        constants = ((31,73,127,181), (211,47,89,13), (59,239,7,109), (163,23,193,251))
        for case in cases:
            with self.subTest(case=case.name):
                if case in masks:
                    latest = [max((stage for stage in range(4) if mask & (1 << stage)), default=-1)
                              for mask in case.buffer_update]
                    expected = tuple((constants[latest[int(lane == 3)]][lane]
                                      if latest[int(lane == 3)] >= 0 else case.buffer_color[lane])
                                     for lane in range(4))
                    self.assertEqual(ORACLE.tev_reference(case.tev,(0,)*4,(0,)*4,
                                     case.buffer_color,case.buffer_update), expected)
                ORACLE.execute_case(self.binary,self.output,case)

    def test_buffer_native_storage_metadata_and_initial_color_are_checked(self):
        row = next(row for row in self.capture['programs'] if 'buffer_color' in row)
        self.assertEqual(row['gprs'],11)
        self.assertEqual(len(row['uniforms']),36)
        for change in ({'gprs':9}, {'uniforms':row['uniforms'][:-1]},
                       {'buffer_color':(0,0,0,256)}, {'buffer_update':(16,0)}):
            with self.subTest(change=change):
                with self.assertRaises(ValueError):
                    ORACLE.decode_fragment_program(dict(row,**change),(0,1,0,1),(17,89,233,53))
        incomplete = dict(row);del incomplete['buffer_update']
        with self.assertRaises(ValueError):
            ORACLE.decode_fragment_program(incomplete,(0,1,0,1),(17,89,233,53))

    def test_alpha_comparisons_have_manual_edges_and_extrema(self):
        answers = ((False,False,False), (True,True,True), (False,True,False),
                   (True,False,True), (True,False,False), (True,True,False),
                   (False,False,True), (False,True,True))
        for function, expected in enumerate(answers):
            for source, answer in zip((127,128,129), expected):
                self.assertEqual(ORACLE.alpha_reference(function,source,128), answer)
        # Exhausting the reference extrema catches signed-byte conversions and
        # complements that accidentally compare 255 as -1.
        for reference, expected in ((0,(0,256,1,255,0,1,255,256)),
                                    (255,(0,256,1,255,255,256,0,1))):
            for function, count in enumerate(expected):
                self.assertEqual(sum(ORACLE.alpha_reference(function,value,reference)
                                     for value in range(256)), count)

    def test_logic_boolean_tables_have_manual_nontrivial_byte_anchors(self):
        answers = (0x00,0x42,0x18,0x5a,0xff,0xa5,0xc3,0x3c,
                   0xbd,0xdb,0x24,0x99,0x66,0x81,0x7e,0xe7)
        for operation, expected in enumerate(answers):
            with self.subTest(operation=operation):
                self.assertEqual(ORACLE.logic_reference(operation,0x5a,0xc3),expected)
        for value in range(256):
            self.assertEqual(ORACLE.logic_reference(11,value,value),0)
            self.assertEqual(ORACLE.logic_reference(12,value,value),255)
            self.assertEqual(ORACLE.logic_reference(1,value,255),value)
            self.assertEqual(ORACLE.logic_reference(9,value,0),value)

    def test_output_mask_preserves_every_unselected_lane_and_alpha_discard(self):
        source, destination = bytes((90,195,105,126)), ORACLE.BACKGROUND
        for mask in range(16):
            registers = ORACLE.output_registers(ORACLE.output_state(mask=mask))
            expected = bytes(source[lane] if mask & (1 << lane) else destination[lane] for lane in range(4))
            self.assertEqual(ORACLE.output_reference(registers,source,destination),expected)
        registers = ORACLE.output_registers(ORACLE.output_state(alpha=True,function=2,reference=127))
        self.assertEqual(ORACLE.output_reference(registers,source,destination),destination)
        merged = ORACLE.output_registers(((0x104,126 << 8,2),),registers)
        self.assertEqual(merged[0x104],0x7e21)
        self.assertEqual(ORACLE.output_reference(merged,source,destination),source)
        for value in range(8):
            self.assertEqual(ORACLE.output_reference(ORACLE.output_registers(ORACLE.output_state(
                logic=value,write=0)),source,destination),destination)

    def test_output_scenes_exact_alpha_logic_masks_and_late_atomicity(self):
        cases = ORACLE.fragment_cases()
        self.assertEqual(len(cases),75)
        for case in cases:
            with self.subTest(case=case.name):ORACLE.execute_case(self.binary,self.output,case)

    def test_blend_factors_have_manual_rgb_and_alpha_byte_anchors(self):
        source,destination,constant = (31,83,173,127),(211,61,19,97),(43,149,227,191)
        expected_rgb = (0,255,31,224,211,44,127,128,97,158,43,212,191,64,127)
        expected_alpha = (0,255,127,128,97,158,127,128,97,158,191,64,191,64,255)
        for factor in range(15):
            self.assertEqual(ORACLE.blend_factor_reference(factor,0,constant,source,destination)*255,
                             expected_rgb[factor])
            self.assertEqual(ORACLE.blend_factor_reference(factor,3,constant,source,destination)*255,
                             expected_alpha[factor])

    def test_blend_divides_once_after_equation_and_truncates_instead_of_rounding(self):
        for source,destination,expected in ((1,0,0),(1,1,1),(2,0,1),(127,128,128),
                                             (128,128,128),(254,254,254),(255,254,255)):
            with self.subTest(source=source,destination=destination):
                actual = ORACLE.blend_reference((0,10,10),(0,10,10),(128,)*4,
                                                (source,)*4,(destination,)*4)
                self.assertEqual(actual,bytes((expected,)*4))
        # (2*128 - 2*1)/255 = 254/255 truncates to zero. Rounding either
        # product first would incorrectly give 1-0 = 1.
        for equation,source_factor,destination_factor in ((1,12,10),(2,10,12)):
            self.assertEqual(ORACLE.blend_reference((equation,source_factor,destination_factor),
                (0,1,0),(1,1,1,128),(2,)*4,(2,)*4),bytes((0,0,0,2)))
        self.assertEqual(ORACLE.blend_reference((0,1,1),(1,1,1),(0,)*4,
            (200,200,200,10),(100,100,100,11)),bytes((255,255,255,0)))

    def test_blend_min_max_apply_factors_and_saturate_alpha_is_one(self):
        source,destination = (100,200,17,127),(200,100,9,191)
        self.assertEqual(ORACLE.blend_reference((3,0,1),(3,0,1),(0,)*4,
                         source,destination),bytes((0,)*4))
        self.assertEqual(ORACLE.blend_reference((4,0,1),(4,0,1),(0,)*4,
                         source,destination),bytes(destination))
        source,destination = (255,128,1,200),(17,99,201,100)
        self.assertEqual(ORACLE.blend_reference((0,14,0),(0,14,0),(0,)*4,
                         source,destination),bytes((155,77,0,200)))
        self.assertEqual(ORACLE.blend_reference((0,0,14),(0,0,14),(0,)*4,
                         source,destination),bytes((10,60,122,100)))

    def test_blend_scenes_factors_equations_retained_state_and_late_rollback(self):
        cases = ORACLE.blend_cases()
        self.assertEqual(len(cases),57)
        for case in cases:
            with self.subTest(case=case.name):ORACLE.execute_case(self.binary,self.output,case)

    def test_fragment_undefined_gpr_and_missing_constants_fail(self):
        row = self.mutate('replace', 256, 511)
        with self.assertRaisesRegex(ValueError, 'Undefined'):
            ORACLE.decode_fragment_program(row, (1,1,1,1), (17,89,233,53))
        row = dict(self.capture['programs'][0]);row['uniforms'] = row['uniforms'][:-1]
        with self.assertRaisesRegex(ValueError, 'constants'):
            ORACLE.decode_fragment_program(row, (1,1,1,1), (17,89,233,53))

    def test_primary_byte_quantization_tolerates_interpolator_roundoff(self):
        row = next(row for row in self.capture['programs'] if row['mode'] == 'modulate')
        result = ORACLE.decode_fragment_program(row, (1-2**-24, 2**-25, 1, 1), (17,89,233,53))
        self.assertEqual(result['color'], (F(17,255), F(0), F(233,255), F(53,255)))

    def mutate(self, mode, offset, mask):
        row = dict(next(row for row in self.capture['programs'] if row['mode'] == mode))
        program = bytearray.fromhex(row['hex'])
        word = struct.unpack_from('<I', program, offset)[0]
        struct.pack_into('<I', program, offset, word ^ mask)
        row['hex'] = program.hex()
        return row

    def test_fragment_coordinates_must_remain_normalized_original_uv(self):
        with self.assertRaisesRegex(ValueError, 'normalized'):
            ORACLE.decode_fragment_program(self.mutate('replace', 132, 1 << 28), (1, 1, 1, 1), (17, 89, 233, 53))
        with self.assertRaisesRegex(ValueError, 'original UV'):
            ORACLE.decode_fragment_program(self.mutate('replace', 136, 1 << 20), (1, 1, 1, 1), (17, 89, 233, 53))

    def test_fragment_sample_opcode_and_bound_resource_are_checked(self):
        for offset, mask in ((128, 1), (128, 1 << 8), (136, 1 << 15)):
            with self.subTest(offset=offset, mask=mask):
                with self.assertRaisesRegex(ValueError, 'SAMPLE_LZ'):
                    ORACLE.decode_fragment_program(self.mutate('replace', offset, mask), (1, 1, 1, 1), (17, 89, 233, 53))

    def test_legal_but_wrong_stage_constant_changes_decoded_pixel(self):
        row = dict(next(row for row in self.capture['programs'] if row['mode'] == 'combiner-28'))
        row['uniforms'] = list(row['uniforms'])
        row['uniforms'][0] = struct.unpack('<I', struct.pack('<f', 23))[0]
        result = ORACLE.decode_fragment_program(row, (1, 0, 1, 1), (17, 89, 233, 53))
        self.assertEqual(result['color'][0], F(23, 255))
        self.assertNotEqual(result['color'][0], F(19, 255))

    def test_fragment_clause_bounds_padding_and_end_are_checked(self):
        for mode, offset, mask in (('replace', 0, 1 << 8), ('replace', 120, 1),
                                   ('modulate', 28, 1 << 21)):
            with self.subTest(mode=mode, offset=offset):
                with self.assertRaises(ValueError):
                    ORACLE.decode_fragment_program(self.mutate(mode, offset, mask), (1, 1, 1, 1), (17, 89, 233, 53))

    def test_fragment_program_word_endianness_is_explicit(self):
        row = dict(self.capture['programs'][0])
        program = bytes.fromhex(row['hex'])
        row['hex'] = b''.join(program[at:at+4][::-1] for at in range(0, len(program), 4)).hex()
        with self.assertRaises(ValueError):
            ORACLE.decode_fragment_program(row, (1, 1, 1, 1), (17, 89, 233, 53))
