"""NCCH executable extraction with bounded, explicit guest segment layout."""
import hashlib
from pathlib import Path

from .containers import Reader, check_overlaps, decompress_code, inspect
from .errors import HostError

PAGE = 4096
MAX_MEMORY = 64 * 1024 * 1024


def load_image(path):
    report = inspect(path)
    main = report["partitions"][0]
    segments = main["segments"]
    regions = []
    total = 3 * PAGE + main["stack_size"]
    if not main["stack_size"] or main["stack_size"] % PAGE or main["stack_size"] >= 0x10000000:
        raise HostError("invalid_stack", "Guest stack must be nonempty, page-aligned and fit below 0x10000000", stage="load")
    for name, segment in segments.items():
        address, pages, size = (segment[key] for key in ("address", "pages", "size"))
        allocated = pages * PAGE
        if address % PAGE or pages != (size + PAGE - 1) // PAGE or address + allocated > 2**32:
            raise HostError("invalid_segment", f"Invalid {name} guest segment bounds/alignment", stage="load")
        if name == "data":
            allocated = max(allocated, (size + main["bss_size"] + PAGE - 1) // PAGE * PAGE)
        total += allocated
        if allocated:
            regions.append((address, allocated, name))
    if not segments["text"]["size"] or segments["text"]["size"] % 4:
        raise HostError("invalid_entry", "Initial fixture entry requires nonempty aligned ARM text", stage="load")
    data = segments["data"]
    bss = main["bss_size"]
    regions += [(0x10000000 - main["stack_size"], main["stack_size"], "stack"),
                (0x1FF80000, PAGE, "kernel-config"),
                (0x1FF81000, PAGE, "shared-time-config"),
                (0x1FF82000, PAGE, "tls")]
    if total > MAX_MEMORY or any(address + size > 2**32 for address, size, _ in regions):
        raise HostError("guest_memory_limit", "Guest layout exceeds the supported 64 MiB allocation limit", stage="load")
    check_overlaps(regions)
    code_record = next(entry for entry in main["exefs"] if entry["name"] == ".code")
    with Path(path).open("rb") as file:
        reader = Reader(file)
        code = reader.read(code_record["offset"], code_record["size"], "executable")
        if hashlib.sha256(code).hexdigest() != code_record["sha256"]:
            raise HostError("input_changed", "Executable changed after inspection", stage="load")
        if main["code_compressed"]:
            code = decompress_code(code)
        if reader.digest(0, reader.size) != report["input_sha256"]:
            raise HostError("input_changed", "Container changed after inspection", stage="load")
    preceding = (segments["text"]["pages"] + segments["rodata"]["pages"]) * PAGE
    maximum = preceding + segments["data"]["pages"] * PAGE
    if not preceding + data["size"] <= len(code) <= maximum:
        raise HostError("code_layout_mismatch", "Executable size does not match page-based segment layout", stage="load")
    return {"schema_version": 1, "inspection": report, "segments": segments,
            "bss_size": bss, "stack_size": main["stack_size"], "allocated_bytes": total,
            "entry": segments["text"]["address"], "tls_address": 0x1FF82000,
            "code": code, "text": code[:segments["text"]["size"]]}
