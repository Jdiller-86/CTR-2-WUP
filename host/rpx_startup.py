"""Read the bounded WUT RPX startup record, independently of title XML.

Format: devkitPro/wut-tools b22de1a3, elf.h RplFileInfo and elf2rpl.
Only the uncompressed CAFE0402 FILEINFO/CRC layout emitted by that tool is used.
"""
import struct
import zlib

from .package_crypto import fail

MAX_RPX_SIZE = 64*1024*1024


def rpx_sdk(data):
    if (not 52 <= len(data) <= MAX_RPX_SIZE
            or data[:8] != b'\x7fELF\x01\x02\x01\xca'
            or data[16:20] != b'\xfe\x01\x00\x14'):
        fail('invalid_native_startup','Require a bounded native WUT RPX')
    table=struct.unpack_from('>I',data,32)[0]
    header_size,entry_size,count,names=(struct.unpack_from('>H',data,n)[0] for n in (40,46,48,50))
    if (header_size!=52 or entry_size!=40 or not 1<=count<=4096 or names>=count
            or table<52 or table+40*count>len(data)):
        fail('invalid_native_startup','Invalid RPX startup section table')
    sections=[struct.unpack_from('>10I',data,table+40*n) for n in range(count)]
    selected={kind:[(n,s) for n,s in enumerate(sections) if s[1]==kind]
              for kind in (0x80000003,0x80000004)}
    if any(len(rows)!=1 for rows in selected.values()):
        fail('invalid_native_startup','Require one RPX FILEINFO and one CRC table')
    info_index,info=selected[0x80000004][0]
    _,crcs=selected[0x80000003][0]
    for section,size in ((info,0x60),(crcs,count*4)):
        if section[2]!=0 or section[5]!=size or section[4]<table+40*count or section[4]+size>len(data):
            fail('invalid_native_startup','Invalid or unsupported WUT startup record')
    raw=data[info[4]:info[4]+0x60]
    expected_crc=struct.unpack_from('>I',data,crcs[4]+info_index*4)[0]
    if zlib.crc32(raw)!=expected_crc:
        fail('invalid_native_startup','RPX startup record CRC mismatch')
    version=struct.unpack_from('>I',raw,0)[0]
    flags=struct.unpack_from('>I',raw,0x34)[0]
    minimum=struct.unpack_from('>I',raw,0x40)[0]
    sdk=struct.unpack_from('>I',raw,0x50)[0]
    if version!=0xcafe0402 or not flags&2 or not 0<minimum<=sdk:
        fail('invalid_native_startup','RPX must declare its native SDK compatibility')
    return sdk
