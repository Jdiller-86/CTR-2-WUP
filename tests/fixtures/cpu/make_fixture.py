#!/usr/bin/env python3
"""Create original, redistributable instruction fixtures; no commercial assets."""
from pathlib import Path
import struct

destination = Path(__file__).parent
# r3 = data mapping at 0x2000, r4 = Thumb entry 0x1101 at startup.
arm = [0xE3A00000, 0xE3A0100A, 0xE2900001, 0xE2511001, 0x1AFFFFFC,
       0xE5830000, 0xE5932000, 0xE352000A, 0x13A02000, 0xE12FFF14]
thumb = [0x2507, 0x3501, 0xDF7F]
(destination / "arithmetic.arm.bin").write_bytes(struct.pack("<" + "I" * len(arm), *arm))
(destination / "exit.thumb.bin").write_bytes(struct.pack("<" + "H" * len(thumb), *thumb))
