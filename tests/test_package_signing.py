"""Bounded original-key signing and independent OpenSSL verification."""
import importlib.util
import json
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
import unittest

from host.errors import HostError
from host.package_test_signing import (CA_SUBJECT, CP_ISSUER, XS_ISSUER,
                                       generate_chain, parse_chain, sign_document,
                                       verify_test_chain)
from host.package_writer import _ticket, _tmd


@unittest.skipUnless(importlib.util.find_spec("cryptography"), "install ctr2wup[packaging]")
class OriginalSigningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.certificates, cls.cp, cls.xs = generate_chain()
        cls.tmd = bytes(_tmd("00050000cf520001", 7, 0x1337, 0x000500101000400A,
                             0x80000000, [0x8000] * 4, [bytes(32)] * 4, CP_ISSUER))
        # This AES value is an original synthetic fixture, never a console key.
        cls.ticket = bytes(_ticket("00050000cf520001", 7, bytes(16), bytes(16), XS_ISSUER))
        cls.signed_tmd = sign_document(cls.tmd, cls.cp)
        cls.signed_ticket = sign_document(cls.ticket, cls.xs, kind="ticket")

    def rejected(self, action, code=None):
        with self.assertRaises(HostError) as error:
            action()
        if code:
            self.assertEqual(error.exception.code, code)

    def test_chain_uses_standard_sizes_and_original_public_keys(self):
        self.assertEqual(len(self.certificates), 0xA00)
        chain = parse_chain(self.certificates)
        self.assertEqual({role: entry["issuer"] for role, entry in chain.items()},
                         {"ca": "Root", "cp": CA_SUBJECT, "xs": CA_SUBJECT})
        self.assertEqual(chain["cp"]["modulus"], self.cp.public_key().public_numbers().n)
        self.assertEqual(chain["xs"]["modulus"], self.xs.public_key().public_numbers().n)
        self.assertEqual(len({entry["modulus"] for entry in chain.values()}), 3)

    def test_receipt_never_asserts_retail_trust_or_console_acceptance(self):
        receipt = verify_test_chain(self.certificates, self.signed_tmd, self.signed_ticket)
        self.assertTrue(receipt["ca_child_signatures_verified"])
        self.assertTrue(receipt["tmd_signature_verified"])
        self.assertTrue(receipt["ticket_signature_verified"])
        self.assertTrue(receipt["signature_patches_required"])
        self.assertFalse(receipt["root_trusted"])
        self.assertFalse(receipt["ca_root_signature_verified"])
        self.assertFalse(receipt["console_verified"])
        self.assertEqual(receipt["installation"], "unverified")
        self.assertLess(len(json.dumps(receipt)), 700)

    def test_signing_preserves_input_and_all_nonsignature_bytes(self):
        for original, signed in ((self.tmd, self.signed_tmd), (self.ticket, self.signed_ticket)):
            self.assertEqual(signed[:4], original[:4])
            self.assertEqual(signed[0x104:], original[0x104:])
            self.assertEqual(original[4:0x104], bytes(256))
            self.assertNotEqual(signed[4:0x104], bytes(256))

    def test_tmd_signed_extent_is_fixed_header_and_not_chunk_table(self):
        changed = bytearray(self.tmd)
        changed[0x208] ^= 1
        self.assertEqual(sign_document(changed, self.cp)[4:0x104], self.signed_tmd[4:0x104])
        changed = bytearray(self.tmd)
        changed[0x1DC] ^= 1
        self.assertNotEqual(sign_document(changed, self.cp)[4:0x104], self.signed_tmd[4:0x104])

    def test_ticket_signature_covers_last_rights_byte(self):
        changed = bytearray(self.signed_ticket)
        changed[-1] ^= 1
        self.rejected(lambda: verify_test_chain(self.certificates, self.signed_tmd, changed),
                      "invalid_test_signature")

    def test_wrong_document_signer_rejected(self):
        wrong = sign_document(self.tmd, self.xs)
        self.rejected(lambda: verify_test_chain(self.certificates, wrong, self.signed_ticket),
                      "invalid_test_signature")

    def test_corrupted_child_chain_or_metadata_signature_rejected(self):
        for offset in (0x404, 0x704, 0x400 + 0x1C8, 0x700 + 0x1C8):
            with self.subTest(offset=offset):
                changed = bytearray(self.certificates)
                changed[offset] ^= 2
                self.rejected(lambda: verify_test_chain(changed, self.signed_tmd, self.signed_ticket))
        for which in ("tmd", "ticket"):
            changed = bytearray(self.signed_tmd if which == "tmd" else self.signed_ticket)
            changed[0x190] ^= 1
            self.rejected(lambda: verify_test_chain(self.certificates,
                          changed if which == "tmd" else self.signed_tmd,
                          changed if which == "ticket" else self.signed_ticket),
                          "invalid_test_signature")

    def test_bounded_framing_issuer_padding_and_rights_rejected(self):
        for data in (b"", self.certificates[:-1], self.certificates + bytes(1),
                     bytes(0xA00), b"x" * 20000):
            self.rejected(lambda: parse_chain(data))
        for offset in (0, 0x204, 0x240, 0x280, 0x284, 0x2C4, 0x3CC):
            changed = bytearray(self.certificates)
            changed[offset] ^= 1
            self.rejected(lambda: parse_chain(changed))
        for kind, original in (("tmd", self.tmd), ("ticket", self.ticket)):
            for offset in (0, 0x104, 0x140):
                changed = bytearray(original)
                changed[offset] ^= 1
                self.rejected(lambda: sign_document(changed, self.cp, kind), "invalid_test_document")
            self.rejected(lambda: sign_document(original[:-1], self.cp, kind), "invalid_test_document")
            self.rejected(lambda: sign_document(original + bytes(1), self.cp, kind), "invalid_test_document")
        changed = bytearray(self.ticket)
        changed[0x2A8] = 0xFF
        self.rejected(lambda: sign_document(changed, self.xs, "ticket"), "invalid_test_document")
        self.rejected(lambda: sign_document(self.tmd, object()), "invalid_test_signing_key")
        self.rejected(lambda: sign_document(self.tmd, self.cp, "certificate"), "invalid_test_document")

    @unittest.skipUnless(shutil.which("openssl"), "independent OpenSSL unavailable")
    def test_openssl_verifies_children_and_both_document_signature_extents(self):
        from cryptography.hazmat.primitives import serialization
        chain = parse_chain(self.certificates)
        cases = [(chain["ca"]["public_key"], chain[role]["signature"], chain[role]["body"])
                 for role in ("cp", "xs")]
        cases += [(chain["cp"]["public_key"], self.signed_tmd[4:0x104], self.tmd[0x140:0x204]),
                  (chain["xs"]["public_key"], self.signed_ticket[4:0x104], self.ticket[0x140:0x350])]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for key, signature, body in cases:
                (root / "public.pem").write_bytes(key.public_bytes(
                    serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
                (root / "signature.bin").write_bytes(signature)
                result = subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(root / "public.pem"),
                                         "-signature", str(root / "signature.bin")],
                                        input=body, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))


if __name__ == "__main__":
    unittest.main()
