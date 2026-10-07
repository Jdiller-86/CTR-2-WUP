"""Publish a completed directory atomically without replacing any destination."""
import ctypes
import errno
import os
import sys

from .errors import HostError


def publish_directory(staging, output):
    if os.name == "nt":
        # Windows rename fails when the destination already exists.
        try:
            os.rename(staging, output)
        except FileExistsError as exc:
            raise HostError("output_exists", "Output already exists; it was preserved", stage="publish") from exc
        return
    if sys.platform.startswith("linux"):
        library = ctypes.CDLL(None, use_errno=True)
        rename = getattr(library, "renameat2", None)
        if rename is None:
            raise HostError("atomic_publish_unavailable", "Atomic no-replace directory publication requires renameat2", stage="publish")
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(-100, os.fsencode(staging), -100, os.fsencode(output), 1):
            error = ctypes.get_errno()
            if error in (errno.EEXIST, errno.ENOTEMPTY):
                raise HostError("output_exists", "Output already exists; it was preserved", stage="publish")
            raise OSError(error, os.strerror(error))
        return
    raise HostError("atomic_publish_unavailable", "Prototype publication has only been implemented for Linux and Windows", stage="publish")
