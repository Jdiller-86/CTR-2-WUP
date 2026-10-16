"""Original semantic and malformed-machine-code checks for the Latte oracle."""
from fractions import Fraction
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('gx2_reference_validation', ROOT / 'tools/validate-gx2-shaders.py')
VALIDATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


class ShaderReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.binary = VALIDATOR.compile_probe(Path(cls.directory.name))
        cls.identity = next(VALIDATOR.cases())
        cls.result = json.loads(subprocess.check_output([str(cls.binary)], input=cls.identity.payload().encode()))
        cls.program = bytes.fromhex(cls.result['bytes'])
        cls.registers = {(register + 1, lane): raw for register, values in enumerate(cls.identity.inputs)
                         for lane, raw in enumerate(values)}

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def decode(self, program):
        return VALIDATOR.decode(program, self.registers, gprs=self.result['gprs'])

    def changed_word(self, offset, value):
        program = bytearray(self.program)
        struct.pack_into('<I', program, offset, value)
        return bytes(program)

    def test_original_corpus_matches_public_cpu_and_independent_machine_decoder(self):
        report = VALIDATOR.validate(self.binary)
        self.assertGreater(report['cases'], 1600)
        self.assertGreater(report['decoded_alu_instructions'], 60000)
        self.assertGreater(report['rejected'], 10)
        self.assertEqual(report['numeric_preflight_rejected'], 1)
        self.assertFalse(report['console_verified'])
        self.assertLess(len(json.dumps(report)), 6000)

    def test_valid_but_wrong_swizzle_changes_semantic_result(self):
        # The first MOV captures v0.x. Selecting y is legal machine code, but
        # must change the independent expected position, rather than pass it.
        address = struct.unpack_from('<I', self.program, 8)[0] * 8
        original = struct.unpack_from('<I', self.program, address)[0]
        changed = self.changed_word(address, original | (1 << 10))
        actual = self.decode(changed)['exports'][0]['values']
        expected = self.identity.oracle()['position']
        self.assertNotEqual(actual, expected)
        self.assertEqual(actual[0], self.identity.inputs[0][1])

    def test_little_endian_program_cannot_be_word_swapped(self):
        swapped = b''.join(self.program[index:index + 4][::-1] for index in range(0, len(self.program), 4))
        with self.assertRaises(ValueError): self.decode(swapped)

    def test_clause_address_cannot_point_into_control_flow(self):
        with self.assertRaisesRegex(ValueError, 'outside program'):
            self.decode(self.changed_word(8, 0))

    def test_clause_count_cannot_extend_beyond_owned_bytes(self):
        control = struct.unpack_from('<I', self.program, 12)[0]
        with self.assertRaisesRegex(ValueError, 'outside program'):
            self.decode(self.changed_word(12, control | (127 << 18)))

    def test_unsupported_machine_arithmetic_is_not_approximated(self):
        address = struct.unpack_from('<I', self.program, 8)[0] * 8
        original = struct.unpack_from('<I', self.program, address + 4)[0]
        with self.assertRaisesRegex(ValueError, 'Unsupported ALU opcode'):
            self.decode(self.changed_word(address + 4, (original & ~(2047 << 7)) | (3 << 7)))

    def test_missing_scalar_group_termination_is_rejected(self):
        address = struct.unpack_from('<I', self.program, 8)[0] * 8
        original = struct.unpack_from('<I', self.program, address)[0]
        with self.assertRaisesRegex(ValueError, 'grouped'):
            self.decode(self.changed_word(address, original & ~(1 << 31)))

    def test_undefined_and_out_of_budget_registers_are_rejected(self):
        address = struct.unpack_from('<I', self.program, 8)[0] * 8
        original = struct.unpack_from('<I', self.program, address)[0]
        with self.assertRaisesRegex(ValueError, 'allocation'):
            self.decode(self.changed_word(address, (original & ~511) | 127))
        with self.assertRaisesRegex(ValueError, 'Undefined GPR'):
            self.decode(self.changed_word(address, (original & ~511) | 32))

    def test_export_destination_must_be_position_zero(self):
        position = struct.unpack_from('<I', self.program, 16)[0]
        with self.assertRaisesRegex(ValueError, 'POS0'):
            self.decode(self.changed_word(16, (position & ~8191) | 61))

    def test_unreferenced_bytes_are_rejected(self):
        changed = self.changed_word(len(self.program) - 4, 0x12345678)
        with self.assertRaisesRegex(ValueError, 'padding'):
            self.decode(changed)

    def test_signed_zero_and_exact_fraction_arithmetic_are_independent(self):
        self.assertEqual(VALIDATOR.arithmetic('MUL', 0x80000000, 0xbf800000), 0)
        self.assertEqual(VALIDATOR.arithmetic('ADD', 0x80000000, 0x80000000), 0x80000000)
        self.assertEqual(VALIDATOR.arithmetic('ADD', 0xbf800000, 0x3f800000), 0)
        self.assertEqual(VALIDATOR.rational(0x3e800000), Fraction(1, 4))
        self.assertEqual(VALIDATOR.arithmetic('ADD', 0x3e800000, 0x3e800000), 0x3f000000)
        with self.assertRaisesRegex(ValueError, 'exact binary32'):
            VALIDATOR.binary32(Fraction(1) + Fraction(1, 2**24))

    def test_optional_external_reference_is_pinned_before_parsing(self):
        candidate = Path(self.directory.name) / 'untrusted.gsh'
        candidate.write_bytes(b'Gfx2' + b'\0' * 60)
        with self.assertRaisesRegex(ValueError, 'pinned bytes'):
            VALIDATOR.check_wut_reference(candidate)


if __name__ == '__main__':
    unittest.main()
