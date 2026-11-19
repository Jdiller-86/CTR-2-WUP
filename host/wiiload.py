"""Original stdlib sender for the public wiiload 0.5 TCP framing.

Framing checked against devkitPro/wiiload v0.5.3 source/main.c and
wiiu-env/wiiload_plugin v0.2.5 src/utils/TcpReceiver.cpp. No upstream code copied.
The protocol has no launch/execution acknowledgement.
"""
import ipaddress
import socket
import struct
import time
import zlib

from .errors import HostError


def send(host: str, filename: str, data: bytes, *, port: int = 4299, timeout: float = 60) -> dict:
    ipaddress.IPv4Address(host)
    if not 4 <= len(data) <= 64 * 1024 * 1024 or not 0 < timeout <= 60:
        raise ValueError("Invalid wiiload payload size or timeout")
    if any(c in filename for c in "\x00\r\n/\\"):
        raise ValueError("wiiload filename must be a basename")
    arguments = filename.encode("utf-8") + b"\0\0"
    if len(arguments) > 1024:
        raise ValueError("wiiload filename exceeds the argument limit")
    compressed = zlib.compress(data)
    header = struct.pack(">4sBBHII", b"HAXX", 0, 5, len(arguments), len(compressed), len(data))
    deadline = time.monotonic() + timeout
    try:
        connection = socket.create_connection((host, port), timeout=min(timeout, 10))
    except OSError as error:
        raise HostError("wiiload_unreachable", "The Wii U's wiiload listener is unreachable. Check the console address and the enabled Aroma wiiload plugin", stage="connect") from error
    with connection as stream:
        for part in (header, compressed, arguments):
            for offset in range(0, len(part), 65536):
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("wiiload submission timed out; execution state is unknown")
                stream.settimeout(remaining)
                stream.sendall(part[offset:offset + 65536])
    return {"sender": "builtin", "wire_bytes": len(header) + len(compressed) + len(arguments),
            "execution_confirmed": False}
