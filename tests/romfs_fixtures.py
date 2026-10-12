"""Original fixture builders, independent of the production verifier."""
import hashlib
import importlib.util
from pathlib import Path
import struct

from tests.host_fixtures import ncch

spec = importlib.util.spec_from_file_location('original_romfs_builder', Path(__file__).resolve().parents[1]/'tools/generate-romfs-fixture.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
level3_image = builder.image
ivfc_image = builder.ivfc_image


def ncch_romfs(level3=None):
    image = ncch(); payload = ivfc_image(level3 if level3 is not None else level3_image())
    offset = len(image); image.extend(payload)
    image[0x18F] &= ~2
    struct.pack_into('<I', image, 0x104, len(image)//512)
    struct.pack_into('<III', image, 0x1B0, offset//512, len(payload)//512, 1)
    image[0x1E0:0x200] = hashlib.sha256(payload[:512]).digest()
    return image
