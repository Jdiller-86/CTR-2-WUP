"""Original read-only modern CIA metadata reader. No keys or authentication."""
import hashlib
import struct

from .containers import check_range, fail

HEADER_SIZE = 0x2020
SIGNATURE_BODIES = {0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}


def aligned(value):
    return (value + 63) // 64 * 64


def signed_body(data, label):
    if len(data) < 4:
        fail("truncated_cia_metadata", f"CIA {label} is truncated")
    value = SIGNATURE_BODIES.get(struct.unpack_from(">I", data)[0])
    if value is None:
        fail("unsupported_cia_signature", f"CIA {label} requires a supported SHA-256 signature layout")
    return value


def read_cia(reader, *, verify_contents=True):
    header = reader.read(0, HEADER_SIZE, "CIA header")
    size, kind, version, cert_size, ticket_size, tmd_size, meta_size, content_size = struct.unpack_from("<IHHIIIIQ", header)
    if size != HEADER_SIZE or kind or version:
        fail("unsupported_cia_header", "Require a standard type-0/version-0 0x2020 CIA header")
    if not 4 <= cert_size <= 16384 or not 4 <= ticket_size <= 16384 or not 4 <= tmd_size <= 4 * 1024 * 1024 or meta_size > 1024 * 1024:
        fail("invalid_cia_metadata_size", "CIA metadata exceeds supported bounds")
    sections = {}
    position = aligned(size)
    for name, length in (("certificates", cert_size), ("ticket", ticket_size), ("tmd", tmd_size), ("contents", content_size)):
        check_range(position, length, reader.size, "CIA " + name)
        sections[name] = {"offset": position, "size": length}
        position = aligned(position + length)
    end = sections["contents"]["offset"] + content_size
    if meta_size:
        check_range(position, meta_size, reader.size, "CIA metadata")
        sections["meta"] = {"offset": position, "size": meta_size}
        end = position + meta_size
    if reader.size != end:
        fail("trailing_cia_data", "CIA file length must match its declared regions")
    tmd = reader.read(sections["tmd"]["offset"], tmd_size, "CIA TMD")
    body = signed_body(tmd, "TMD")
    check_range(body, 0xC4 + 64 * 36, len(tmd), "CIA TMD header")
    if tmd[body + 0x40] != 1:
        fail("unsupported_cia_tmd", "Require Wii U/3DS version-1 TMD table layout")
    title = tmd[body + 0x4C:body + 0x54].hex()
    title_version, count, boot = struct.unpack_from(">HHH", tmd, body + 0x9C)
    start = body + 0xC4 + 64 * 36
    if not count or len(tmd) != start + count * 48:
        fail("invalid_cia_tmd_size", "CIA TMD length must match its content count")
    infos = tmd[body + 0xC4:start]
    if hashlib.sha256(infos).digest() != tmd[body + 0xA4:body + 0xC4]:
        fail("cia_metadata_hash_mismatch", "CIA TMD content-info SHA-256 mismatch")
    covered = set()
    for index in range(64):
        first, length = struct.unpack_from(">HH", infos, index * 36)
        record = infos[index * 36:(index + 1) * 36]
        if not length:
            if any(record): fail("invalid_cia_content_info", "Unused CIA content-info records must be zero")
            continue
        positions = set(range(first, first + length))
        if first + length > count or covered.intersection(positions):
            fail("invalid_cia_content_info", "CIA content-info groups overlap or exceed the table")
        covered.update(positions)
        if hashlib.sha256(tmd[start + first * 48:start + (first + length) * 48]).digest() != record[4:]:
            fail("cia_metadata_hash_mismatch", "CIA TMD content-record SHA-256 mismatch")
    if covered != set(range(count)):
        fail("invalid_cia_content_info", "CIA content-info groups must cover the entire table")
    ticket = reader.read(sections["ticket"]["offset"], ticket_size, "CIA ticket")
    ticket_body = signed_body(ticket, "ticket")
    check_range(ticket_body, 0xA4, len(ticket), "CIA ticket identity")
    if ticket[ticket_body + 0x9C:ticket_body + 0xA4].hex() != title:
        fail("cia_ticket_title_mismatch", "CIA ticket and TMD title IDs differ")
    bitmap = header[0x20:]
    ids, indices, present, contents = set(), set(), set(), []
    offset = sections["contents"]["offset"]
    for number in range(count):
        content_id, index, flags, length = struct.unpack_from(">IHHQ", tmd, start + number * 48)
        if content_id in ids or index in indices or not length:
            fail("invalid_cia_content", "CIA content IDs/indices must be unique with nonzero sizes")
        ids.add(content_id); indices.add(index)
        included = bool(bitmap[index // 8] & (0x80 >> (index % 8)))
        digest = tmd[start + number * 48 + 16:start + (number + 1) * 48].hex()
        item = {"content_id": f"{content_id:08x}", "index": index, "type": flags, "size": length,
                "included": included, "encrypted": bool(flags & 1), "tmd_sha256": digest,
                "plaintext_integrity": "not_checked"}
        if included:
            check_range(offset, length, sections["contents"]["offset"] + content_size, "CIA content")
            item["offset"] = offset
            present.add(index)
            if verify_contents and not item["encrypted"]:
                if reader.digest(offset, length) != digest:
                    fail("cia_content_hash_mismatch", "Decrypted CIA content SHA-256 does not match its TMD")
                item["plaintext_integrity"] = "sha256_matches_tmd"
            offset = aligned(offset + length)
        contents.append(item)
    listed = {index for index in range(65536) if bitmap[index // 8] & (0x80 >> (index % 8))}
    if listed != present:
        fail("invalid_cia_bitmap", "CIA bitmap declares contents absent from the TMD")
    included = [item for item in contents if item["included"]]
    if not included or included[-1]["offset"] + included[-1]["size"] != sections["contents"]["offset"] + content_size:
        fail("invalid_cia_content_size", "CIA declared content span differs from included records")
    return {"title_id": title, "title_version": title_version, "tmd_boot_index": boot,
            "sections": sections, "contents": contents,
            "signature_authenticity_verified": False, "decryption_performed": False}


def main_content(metadata):
    if not metadata["title_id"].startswith("00040000"):
        fail("unsupported_cia_title", "First-title CIA intake requires a base application, not updates/DLC/system titles")
    main = next((item for item in metadata["contents"] if item["index"] == 0 and item["included"]), None)
    if main is None:
        fail("missing_cia_executable", "CIA does not include base application content index 0")
    if main["encrypted"]:
        fail("encrypted_cia_content", "CIA executable is outer-encrypted; fully decrypted executable data is needed for analysis. No keys are requested")
    return main
