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
    inspection = commands.add_parser("inspect", help="Strictly inspect a fully decrypted .cia/.3ds/.cxi")
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
    guest.add_argument("--backend",choices=("original","upstream"),default="original")
    translation=commands.add_parser("translate",help="Generate checked C with the separately installed pinned lifter; execution remains profile gated")
    translation.add_argument("path")
    translation.add_argument("--output",required=True)
    intake=commands.add_parser("analyze-title",help="Write private revision/capability intake; no profile is enabled")
    intake.add_argument("path");intake.add_argument("--output",required=True);intake.add_argument("--lifter",action="store_true")
    small=commands.add_parser("prepare-intake",help="Create a private small executable view or encryption report; not a full ROM or supported conversion")
    small.add_argument("path");small.add_argument("--output",required=True)
    preparation=commands.add_parser("prepare-rom",help="Create a private checked decrypted main CXI using optional local 3DS keys; no commercial conversion")
    preparation.add_argument("path");preparation.add_argument("--output",required=True)
    preparation.add_argument("--keys-config",help="Local schema-1 key file references; never pass key values")
    assets=commands.add_parser("prepare-assets",help="Verify the full main RomFS hash tree and write a private Level-3 blob/manifest")
    assets.add_argument("path");assets.add_argument("--output",required=True)
    key_import=commands.add_parser("import-rom-keys",help="Import existing plaintext 3DS key/seed exports to a private local store; no device extraction")
    key_import.add_argument("--output",required=True)
    key_import.add_argument("--aeskeydb");key_import.add_argument("--seeddb")
    key_import.add_argument("--decrypted-titlekeys",help="Locally obtained decTitleKeys-format database declared to contain plaintext title keys")
    compile_command = commands.add_parser("build", help="Compile the native diagnostic RPX or check a ROM support gate")
    compile_command.add_argument("path", nargs="?")
    compile_command.add_argument("--target", choices=("prototype", "audio-check", "scene-check", "guest-fixture", "commercial"), default="commercial")
    compile_command.add_argument("--output", required=True)
    compile_command.add_argument("--backend",choices=("original","upstream"),default="original")
    package = commands.add_parser("validate-package", help="Validate a WUP directory; reports structural limits")
    package.add_argument("path")
    package.add_argument("--registry")
    staging=commands.add_parser("stage-title",help="Stage an original homebrew RPX and metadata/artwork; output is not an installable WUP")
    staging.add_argument("rpx");staging.add_argument("--output",required=True);staging.add_argument("--title-id",required=True)
    staging.add_argument("--name",default="CTR2WUP Prototype");staging.add_argument("--title-version",type=int,default=0)
    packing=commands.add_parser("pack-title",help="Encrypt original staged homebrew using local inputs; installer acceptance remains unverified")
    packing.add_argument("path");packing.add_argument("--output",required=True);packing.add_argument("--config",required=True)
    integrity=commands.add_parser("verify-package",help="Decrypt and verify the original four-content package profile with a local key")
    integrity.add_argument("path");integrity.add_argument("--common-key-file",required=True);integrity.add_argument("--expected-staging")
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
                           x=args.touch_x,y=args.touch_y,translated=args.mode=="aot",backend=args.backend)
    if args.command == "translate":
        from translator.upstream import generate
        return generate(args.path,args.output)
    if args.command == "stage-title":
        from .title_stage import stage_title
        return stage_title(args.rpx,args.output,title_id=args.title_id,name=args.name,version=args.title_version)
    if args.command == "pack-title":
        from .package_writer import pack_title
        return pack_title(args.path,args.output,args.config)
    if args.command == "verify-package":
        from .package_integrity import verify_package
        return verify_package(args.path,args.common_key_file,expected_staging=args.expected_staging)
    if args.command == "analyze-title":
        from .title_analysis import analyze_title
        return analyze_title(args.path,args.output,lifter=args.lifter)
    if args.command == "prepare-intake":
        from .intake_export import export_intake
        return export_intake(args.path,args.output)
    if args.command == "prepare-rom":
        from .rom_prepare import prepare_rom
        return prepare_rom(args.path,args.output,keys_config=args.keys_config)
    if args.command == "prepare-assets":
        from .assets import prepare_assets
        return prepare_assets(args.path,args.output)
    if args.command == "import-rom-keys":
        from .rom_key_import import import_keys
        return import_keys(args.output,aeskeydb=args.aeskeydb,seeddb=args.seeddb,decrypted_titlekeys=args.decrypted_titlekeys)
    if args.command == "build":
        if args.target in ("prototype", "audio-check", "scene-check"):
            if args.path:
                raise HostError("usage", "A native prototype build does not take a ROM", stage="arguments")
            return build.prototype(args.output,backend=args.backend,audio_only=args.target=="audio-check",scene_only=args.target=="scene-check")
        if args.target == "guest-fixture":
            if not args.path:
                raise HostError("usage", "Guest fixture build requires the original fixture .cxi", stage="arguments")
            return build.prototype(args.output,guest_path=args.path,backend=args.backend)
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
