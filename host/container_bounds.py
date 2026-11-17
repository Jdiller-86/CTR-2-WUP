"""Shared range checks for container readers."""

from .errors import HostError


def fail(code, message):
    raise HostError(code, message)


def check_range(offset, length, limit, label):
    if offset < 0 or length < 0 or offset > limit or length > limit - offset:
        fail("invalid_bounds", f"{label} lies outside its containing region")
