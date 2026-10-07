"""Windows-compatible JSON CLI; use ``python -m host``."""

from __future__ import annotations

import argparse
import json
import sys

from . import __version__
from . import build
from .containers import inspect
from .doctor import doctor
from .errors import HostError


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise HostError("usage", message, stage="arguments")


def parser():
    result = Parser(prog="ctr2wup", description="CTR2WUP research CLI. Commercial conversion is not supported yet.")
    result.add_argument("--version", action="version", version=__version__)
    commands = result.add_subparsers(dest="command", required=True)
    commands.add_parser("doctor", help="Discover real compiler and WUT prerequisites")
    inspection = commands.add_parser("inspect", help="Strictly inspect a fully decrypted .3ds/.cxi")
    inspection.add_argument("path")
    fixture = commands.add_parser("make-fixture", help="Generate the original redistributable NCCH runtime fixture")
    fixture.add_argument("--output", required=True)
    guest = commands.add_parser("run-fixture", help="Load, translate, compile and execute the controlled guest fixture on desktop")
    guest.add_argument("path")
    guest.add_argument("--output", required=True)
    guest.add_argument("--frames", type=int, default=2)
    guest.add_argument("--buttons", type=lambda text: int(text, 0), default=0)
    guest.add_argument("--touch", action="store_true")
    guest.add_argument("--touch-x", type=int, default=0)
    guest.add_argument("--touch-y", type=int, default=0)
    guest.add_argument("--mode", choices=("aot", "interpreter"), default="aot")
    compile_command = commands.add_parser("build", help="Compile the native diagnostic RPX or check a ROM support gate")
    compile_command.add_argument("path", nargs="?")
    compile_command.add_argument("--target", choices=("prototype", "guest-fixture", "commercial"), default="commercial")
    compile_command.add_argument("--output", required=True)
    package = commands.add_parser("validate-package", help="Validate a WUP directory; reports structural limits")
    package.add_argument("path")
    package.add_argument("--registry")
    return result


def execute(args):
    if args.command == "doctor":
        return doctor()
    if args.command == "inspect":
        return {"ok": True, "stage": "inspect", **inspect(args.path)}
    if args.command == "make-fixture":
        from .guest_fixture import create
        return create(args.output)
    if args.command == "run-fixture":
        from .guest_pipeline import run_fixture
        return run_fixture(args.path,args.output,frames=args.frames,buttons=args.buttons,touch=args.touch,
                           x=args.touch_x,y=args.touch_y,translated=args.mode=="aot")
    if args.command == "build":
        if args.target == "prototype":
            if args.path:
                raise HostError("usage", "A native prototype build does not take a ROM", stage="arguments")
            return build.prototype(args.output)
        if args.target == "guest-fixture":
            if not args.path:
                raise HostError("usage", "Guest fixture build requires the original fixture .cxi", stage="arguments")
            return build.prototype(args.output,guest_path=args.path)
        if not args.path:
            raise HostError("usage", "Commercial build requires an input .3ds or .cxi", stage="arguments")
        return build.commercial(args.path, args.output)
    if args.command == "validate-package":
        try:
            from .package_validator import validate_package
        except ImportError as exc:
            raise HostError("validator_unavailable", "Package validator has not been installed in this checkout", stage="packaging") from exc
        try:
            return validate_package(args.path, registry=args.registry)
        except ValueError as exc:
            raise HostError(getattr(exc, "code", "invalid_package"), str(exc), stage="packaging", details=getattr(exc, "details", None)) from exc
    raise HostError("usage", "Unknown command", stage="arguments")


def main(argv=None):
    try:
        args = parser().parse_args(argv)
        result = execute(args)
        print(json.dumps(result, indent=2, ensure_ascii=True))
        return 0 if result.get("ok", True) else 1
    except HostError as exc:
        print(json.dumps(exc.as_dict(), indent=2, ensure_ascii=True), file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print(json.dumps(HostError("cancelled", "Build or inspection cancelled; no incomplete final output was published", stage="cancelled").as_dict()), file=sys.stderr)
        return 130
    except OSError as exc:
        print(json.dumps(HostError("host_io", str(exc), stage="host").as_dict()), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
