"""Strictly profiled NCCH -> original generated C -> desktop execution pipeline."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from .atomic import publish_directory
from .build import _run_build, _sha256, MIN_BUILD_FREE
from .doctor import repository_root, tool_environment
from .errors import HostError
from .guest_fixture import fixture_bytes, guest_code, TEXT_ADDRESS, DATA_ADDRESS, BSS_SIZE, PROGRAM_ID
from .loader import load_image


def profiled_image(path):
    image = load_image(path)
    if (image["code"] != fixture_bytes()[0xC00:] or image["entry"] != TEXT_ADDRESS
            or image["segments"]["text"] != {"address": TEXT_ADDRESS, "pages": 1, "size": len(guest_code())}
            or image["segments"]["data"] != {"address": DATA_ADDRESS, "pages": 1, "size": 4096}
            or image["segments"]["rodata"] != {"address": 0, "pages": 0, "size": 0}
            or image["bss_size"] != BSS_SIZE
            or image["stack_size"] != 0x4000
            or image["inspection"]["identity"]["program_id"] != f"{PROGRAM_ID:016x}"):
        raise HostError("unsupported_fixture_profile", "Guest execution is currently limited to the original CTR2WUP framebuffer fixture; no commercial profile is enabled", stage="profile")
    return image


def generated_sources(image):
    # Delayed import keeps installed inspection tools independent of the source-only translator.
    try:
        from translator.aot import translate
    except ImportError as exc:
        raise HostError("missing_translator_source", "Install the complete CTR2WUP package/source before guest compilation", stage="translate") from exc
    code, segments = image["code"], image["segments"]
    blob = ",\n".join("    " + ",".join(f"0x{byte:02x}" for byte in code[index:index+32]) for index in range(0, len(code), 32))
    descriptors = ",\n".join("    {" + f"0x{segments[name]['address']:08x}u,{segments[name]['pages']}u,{segments[name]['size']}u" + "}" for name in ("text", "rodata", "data"))
    metadata = '#include "ctr_guest_fixture.h"\n\n'
    metadata += f"static const uint8_t ctr_frame_image_bytes[] = {{\n{blob}\n}};\n"
    metadata += "const ctr_guest_image ctr_frame_image = {\n    ctr_frame_image_bytes,sizeof(ctr_frame_image_bytes),\n" + descriptors + ",\n"
    metadata += f'    {image["bss_size"]}u,{image["stack_size"]}u,"{image["inspection"]["partitions"][0]["executable_sha256"]}"\n}};\n'
    return {"framebuffer.generated.c": translate(image["text"], image["entry"], "ctr_frame_fixture", False),
            "image.generated.c": metadata}


def run_fixture(path, output, *, frames=2, buttons=0, touch=False, x=0, y=0, translated=True, backend="original"):
    if backend not in ("original","upstream") or (backend=="upstream" and not translated):
        raise HostError("invalid_backend", "Use original aot/interpreter or upstream aot", stage="arguments")
    if not 1 <= frames <= 30 or not 0 <= buttons <= 0xFFFFFFFF or not 0 <= x < 320 or not 0 <= y < 240:
        raise HostError("invalid_fixture_input", "Require 1..30 frames, 32-bit buttons and touch coordinates within 320x240", stage="arguments")
    image = profiled_image(path)
    repo = repository_root()
    if not (repo/"runtime/desktop/guest_runner.c").is_file():
        raise HostError("missing_guest_recipe", "Guest execution requires the complete source distribution; set CTR2WUP_PROJECT_ROOT to that checkout", stage="prepare")
    env = tool_environment()
    compiler = shutil.which("gcc", path=env.get("PATH"))
    if not compiler:
        raise HostError("missing_host_compiler", "Install a native GCC compiler for desktop fixture execution", stage="compile")
    output = Path(output).absolute()
    if output.exists():
        raise HostError("output_exists", "Guest output already exists; choose a new directory", stage="prepare")
    output.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(output.parent).free < MIN_BUILD_FREE:
        raise HostError("insufficient_disk", "Guest staging requires 64 MiB free space", stage="prepare")
    stage = Path(tempfile.mkdtemp(prefix=".ctr2wup-guest-", dir=output.parent))
    try:
        for name, contents in generated_sources(image).items():
            (stage / name).write_text(contents, encoding="utf-8")
        binary = stage / ("ctr2wup-guest.exe" if __import__("os").name == "nt" else "ctr2wup-guest")
        logs=repo/"artifacts/logs"
        logs.mkdir(parents=True,exist_ok=True)
        build_log=logs/(stage.name+"-compile.log")
        execution_log=logs/(stage.name+"-execute.log")
        core = repo / "runtime/core"
        sources = [repo / "runtime/desktop/guest_runner.c"] + [core / name for name in ("ctr_memory.c", "ctr_cpu.c", "ctr_guest.c", "ctr_guest_fixture.c")]
        flags=[];generation=None
        if backend=="upstream":
            from translator.upstream import generate_into
            generation=generate_into(path,stage/"aot")
            sources += [repo/"runtime/recomp/ctr_recomp.c",stage/"image.generated.c"]
            sources += [stage/"aot"/name for name in generation["sources"]]
            flags=["-DCTR_EXTERNAL_AOT","-I",str(stage/"aot"),"-I",str(repo/"runtime/recomp"),"-ffp-contract=off","-fno-fast-math"]
            (stage/"framebuffer.generated.c").unlink()
        else:
            sources += [stage / name for name in generated_sources(image)]
        command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(core), *flags,*map(str, sources), "-lm", "-o", str(binary)]
        _run_build(command, env, build_log, repo)
        _run_build([str(binary), str(frames), str(buttons), str(int(touch)), str(x), str(y), str(int(translated))],
                   env,execution_log,stage,stage="execute",timeout=60)
        shutil.copy2(build_log,stage/"build.log"); shutil.copy2(execution_log,stage/"execution.log")
        try:
            result = json.loads(execution_log.read_text().splitlines()[-1])
        except json.JSONDecodeError as exc:
            raise HostError("invalid_guest_report", "Runner did not return a valid completion report", stage="execute") from exc
        if result.get("ok") is not True or result.get("frames") != frames or result.get("exit_state") != "exited":
            raise HostError("invalid_guest_report", "Runner did not complete the requested frames and orderly guest exit", stage="execute")
        report = {"schema_version": 1, "ok": True, "target": "guest-fixture", "profile": "ctr2wup-framebuffer-fixture-v1",
                  "input_identity": image["inspection"]["identity"], "executable_revision": image["inspection"]["executable_revision"],
                  "mode": "aot" if translated else "interpreter", "backend":backend,"generation":generation,"guest": result,
                  "console_verified": False, "io": "explicit fixture input/raw-framebuffer contract; HID/GSP/PICA unavailable",
                  "artifacts": [{"name": item.name, "size": item.stat().st_size, "sha256": _sha256(item)} for item in (binary, stage/"top.ppm", stage/"bottom.ppm")]}
        (stage/"build-report.json").write_text(json.dumps(report, indent=2)+"\n")
        publish_directory(stage, output)
        return {**report, "output": str(output)}
    except subprocess.TimeoutExpired as exc:
        raise HostError("guest_timeout", "Desktop guest exceeded its execution limit", stage="execute") from exc
    finally:
        if stage.exists(): shutil.rmtree(stage)
