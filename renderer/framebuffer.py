"""Small format and presentation references, independent of any host byte order."""


def rgb565_to_rgb8(data: bytes) -> bytes:
    if len(data) % 2:
        raise ValueError("RGB565 buffer must contain whole little-endian pixels")
    result = bytearray()
    for i in range(0, len(data), 2):
        pixel = data[i] | (data[i + 1] << 8)
        r, g, b = (pixel >> 11) & 31, (pixel >> 5) & 63, pixel & 31
        result.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return bytes(result)


def ppm(rgb: bytes, width: int, height: int) -> bytes:
    if width <= 0 or height <= 0 or len(rgb) != width * height * 3:
        raise ValueError("dimensions do not match RGB8 pixels")
    return f"P6\n{width} {height}\n255\n".encode("ascii") + rgb


def viewport(buffer_width: int, buffer_height: int, guest_width: int, guest_height: int) -> tuple[int, int, int, int]:
    if min(buffer_width, buffer_height, guest_width, guest_height) <= 0:
        raise ValueError("dimensions must be positive")
    if buffer_width * guest_height <= buffer_height * guest_width:
        width, height = buffer_width, max(1, buffer_width * guest_height // guest_width)
    else:
        width, height = max(1, buffer_height * guest_width // guest_height), buffer_height
    return ((buffer_width - width) // 2, (buffer_height - height) // 2, width, height)


def touch_to_guest(x: float, y: float, bounds: tuple[int, int, int, int], guest_width: int, guest_height: int):
    left, top, width, height = bounds
    if min(width, height, guest_width, guest_height) <= 0:
        raise ValueError("dimensions must be positive")
    if not (left <= x < left + width and top <= y < top + height):
        return None
    return (min(guest_width - 1, int((x - left) * guest_width / width)),
            min(guest_height - 1, int((y - top) * guest_height / height)))
