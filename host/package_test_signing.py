"""Original homebrew signatures for signature-patched IOSU, never retail trust.

Every RSA key is generated in memory. No Nintendo key, certificate, signature or
donor title is used. The conventional CA/CP/XS *names* describe certificate roles;
their generated public keys do not authenticate against Nintendo's embedded Root.

Format facts, inspected as references without importing implementation:
* makefst e363897e29109e6f1125d60376822c3768d3c2d4: certs.h/pki layouts and
  main.c's plaintext 0x2000 content profile (https://github.com/shinyquagsire23/makefst).
* dat_cdnverify 8cd27b84fe70191f902238ff05817184d606bde8, util/verify.py:
  TMD signs its 0xc4-byte header; ticket signs through its declared rights end
  (https://github.com/ZeroSkill1/dat_cdnverify).
* SigpatchesModuleWiiU 0690b801bbd643606604a0e38aa0ac6819616399, source/main.cpp:
  patches certificate, cached-certificate, public-key-signature and MCP checks
  (https://github.com/marco-calautti/SigpatchesModuleWiiU).

These signatures establish consistency of our original generated package only.
They neither prove installer acceptance nor work on unpatched retail IOSU.
"""
from __future__ import annotations

import struct

from .package_crypto import certificate_subjects, fail
from .errors import HostError

CA_NAME = "CA00000003"
CA_ISSUER = "Root"
CA_SUBJECT = CA_ISSUER + "-" + CA_NAME
CP_NAME = "CP0000000b"
XS_NAME = "XS0000000c"
CP_ISSUER = CA_SUBJECT + "-" + CP_NAME
XS_ISSUER = CA_SUBJECT + "-" + XS_NAME
CHAIN_BYTES = 0xA00


def _crypto():
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import padding, rsa
    except ImportError as exc:
        raise HostError("crypto_unavailable", "Install the documented ctr2wup[packaging] dependency",
                        stage="packaging") from exc
    return hashes, padding, rsa, InvalidSignature


def _field(value):
    return value.encode("ascii").ljust(0x40, b"\0")


def _certificate(key, issuer, name, signing_key):
    """RSA2048 subject; SHA256/PKCS1 signature by RSA4096 Root or RSA2048 CA."""
    hashes, padding, _, _ = _crypto()
    header = 0x240 if signing_key.key_size == 4096 else 0x140
    data = bytearray(header + 0x1C0)
    struct.pack_into(">I", data, 0, 0x10003 if header == 0x240 else 0x10004)
    data[header:header + 0x40] = _field(issuer)
    struct.pack_into(">I", data, header + 0x40, 1)  # RSA2048 subject key
    data[header + 0x44:header + 0x84] = _field(name)
    # Date/reserved word at +0x84 remains zero; public-key data begins at +0x88.
    numbers = key.public_key().public_numbers()
    data[header + 0x88:header + 0x188] = numbers.n.to_bytes(256, "big")
    struct.pack_into(">I", data, header + 0x188, numbers.e)
    signature = signing_key.sign(bytes(data[header:]), padding.PKCS1v15(), hashes.SHA256())
    data[4:4 + len(signature)] = signature
    return bytes(data)


def generate_chain():
    """Return public certificate bytes and the ephemeral CP/XS signing objects.

    Standard sizes are 0x400 (Root-signed CA) + 0x300 (CA-signed CP) +
    0x300 (CA-signed XS). The generated Root and CA private keys are not returned.
    No key is serialized or written to a file.
    """
    _, _, rsa, _ = _crypto()
    root = rsa.generate_private_key(public_exponent=65537, key_size=4096)
    ca = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    cp = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    xs = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    certificates = (_certificate(ca, CA_ISSUER, CA_NAME, root)
                    + _certificate(cp, CA_SUBJECT, CP_NAME, ca)
                    + _certificate(xs, CA_SUBJECT, XS_NAME, ca))
    return certificates, cp, xs


def parse_chain(certificates):
    """Parse only this bounded public chain; returned numbers contain no secrets.

    This low-level result includes public cryptography key objects and bytes for
    verification. Use verify_test_chain's small JSON-safe receipt in reports.
    """
    _, _, rsa, _ = _crypto()
    if not isinstance(certificates, (bytes, bytearray)) or len(certificates) != CHAIN_BYTES:
        fail("invalid_test_certificate", "Original signing requires the exact bounded CA/CP/XS chain")
    certificates = bytes(certificates)
    subjects = certificate_subjects(certificates)
    if set(subjects) != {CA_SUBJECT, CP_ISSUER, XS_ISSUER}:
        fail("invalid_test_certificate", "Original signing certificate subjects do not match their roles")
    result = {}
    for role, offset, length, header, signature_type, issuer, name in (
            ("ca", 0, 0x400, 0x240, 0x10003, CA_ISSUER, CA_NAME),
            ("cp", 0x400, 0x300, 0x140, 0x10004, CA_SUBJECT, CP_NAME),
            ("xs", 0x700, 0x300, 0x140, 0x10004, CA_SUBJECT, XS_NAME)):
        data = certificates[offset:offset + length]
        if (struct.unpack_from(">I", data)[0] != signature_type
                or data[header:header + 0x40] != _field(issuer)
                or struct.unpack_from(">I", data, header + 0x40)[0] != 1
                or data[header + 0x44:header + 0x84] != _field(name)
                or any(data[4 + (header - 0x40):header])
                or any(data[header + 0x84:header + 0x88])
                or any(data[header + 0x18C:])):
            fail("invalid_test_certificate", "Original signing certificate format is invalid")
        modulus = int.from_bytes(data[header + 0x88:header + 0x188], "big")
        exponent = struct.unpack_from(">I", data, header + 0x188)[0]
        if modulus.bit_length() != 2048 or not modulus & 1 or exponent != 65537:
            fail("invalid_test_certificate", "Original signing public-key profile is invalid")
        try:
            key = rsa.RSAPublicNumbers(exponent, modulus).public_key()
        except ValueError as exc:
            raise HostError("invalid_test_certificate", "Original signing public key is invalid",
                            stage="packaging") from exc
        result[role] = {"issuer": issuer, "name": name, "modulus": modulus,
                        "exponent": exponent, "signature_type": signature_type,
                        "signature": data[4:4 + header - 0x40],
                        "body": data[header:], "public_key": key}
    return result


def _signed_body(data, kind):
    if not isinstance(data, (bytes, bytearray)) or len(data) < 0x204:
        fail("invalid_test_document", "Original signing document is truncated")
    if struct.unpack_from(">I", data)[0] != 0x10004 or any(data[0x104:0x140]):
        fail("invalid_test_document", "Original signing requires RSA2048/SHA256 document framing")
    if kind == "tmd":
        count = struct.unpack_from(">H", data, 0x1DE)[0]
        if not 1 <= count <= 4096 or len(data) != 0xB04 + count * 48:
            fail("invalid_test_document", "Original TMD must match its bounded content count")
        end, issuer = 0x204, CP_ISSUER
    elif kind == "ticket":
        if len(data) != 0x350:
            fail("invalid_test_document", "Original ticket must use the bounded four-content rights profile")
        if (struct.unpack_from(">HHIIHHI", data, 0x2A4)
                != (1, 0x14, 0xAC, 0x14, 1, 0x14, 0)
                or struct.unpack_from(">IIIIHH", data, 0x2B8)
                != (0x28, 1, 0x84, 0x84, 3, 0)):
            fail("invalid_test_document", "Original ticket rights header is invalid")
        end, issuer = 0x2A4 + 0xAC, XS_ISSUER
    else:
        fail("invalid_test_document", "Original signing kind must be tmd or ticket")
    if data[0x140:0x180] != _field(issuer):
        fail("invalid_test_document", "Original document issuer does not match its signing role")
    return bytes(data[0x140:end])


def sign_document(data, private_key, kind="tmd"):
    """Return a new document; TMD hash tables are verified by the package reader."""
    body = _signed_body(data, kind)
    hashes, padding, rsa, _ = _crypto()
    if not isinstance(private_key, rsa.RSAPrivateKey) or private_key.key_size != 2048:
        fail("invalid_test_signing_key", "Original document signing requires an in-memory RSA2048 key")
    signature = private_key.sign(body, padding.PKCS1v15(), hashes.SHA256())
    result = bytearray(data)
    result[4:0x104] = signature
    return bytes(result)


def verify_test_chain(certificates, tmd, ticket):
    """Verify original chain consistency, without asserting Nintendo Root trust."""
    chain = parse_chain(certificates)
    hashes, padding, _, InvalidSignature = _crypto()
    try:
        for role in ("cp", "xs"):
            chain["ca"]["public_key"].verify(chain[role]["signature"], chain[role]["body"],
                                               padding.PKCS1v15(), hashes.SHA256())
        for role, document, kind in (("cp", tmd, "tmd"), ("xs", ticket, "ticket")):
            body = _signed_body(document, kind)
            chain[role]["public_key"].verify(bytes(document[4:0x104]), body,
                                              padding.PKCS1v15(), hashes.SHA256())
    except InvalidSignature as exc:
        raise HostError("invalid_test_signature", "Original package signing consistency check failed",
                        stage="packaging") from exc
    return {"signature_policy": "original_self_signed",
            "certificate_chain": "original generated CA/CP/XS",
            "ca_child_signatures_verified": True,
            "tmd_signature_verified": True, "ticket_signature_verified": True,
            "root_trusted": False, "ca_root_signature_verified": False,
            "signature_patches_required": True,
            "installation": "unverified", "console_verified": False}
