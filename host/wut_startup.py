"""Arrange the SDK's C runtime startup code before a large AOT text image."""
from pathlib import Path

from .errors import HostError


def prepare_wut_startup_layout(wut: Path, output: Path, make_relative: str) -> None:
    """Generate local specs from the SDK without editing the SDK installation."""
    try:
        linker = (wut / "share/wut.ld").read_text(encoding="utf-8")
        specs = (wut / "share/wut.specs").read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise HostError("missing_wut_linker", "Could not read WUT linker files",
                        stage="compile") from exc

    init = "      KEEP (*(SORT_NONE(.init)))"
    fini = "      KEEP (*(SORT_NONE(.fini)))"
    linker_reference = "%:getenv(DEVKITPRO /wut/share/wut.ld)"
    if (linker.count(init) != 1 or linker.count(fini + "\n") != 1
            or specs.count(linker_reference) != 1):
        raise HostError("unrecognized_wut_linker", "Use the supported WUT linker layout",
                        stage="compile")
    # GCC's crtend helper normally lands after all AOT objects. Keep its
    # branches from .init and .fini within the PowerPC REL24 displacement.
    linker = linker.replace(fini + "\n", "").replace(init, "\n".join((
        init,
        "      *crtbegin.o(.text .text.*)",
        "      *crtbegin?.o(.text .text.*)",
        "      *crtend.o(.text .text.*)",
        "      *crtend?.o(.text .text.*)",
        fini,
    )))
    (output / "wut-startup.ld").write_text(linker, encoding="utf-8", newline="\n")
    specs = specs.replace(linker_reference, f"{make_relative}/wut-startup.ld")
    (output / "wut-startup.specs").write_text(specs, encoding="utf-8", newline="\n")
