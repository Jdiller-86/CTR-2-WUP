"""Real prototype compilation and transactional publication.

Commercial conversion stays gated until guest execution, GPU, service and
physical-console milestones are met. Never publish placeholder game packages.
"""

from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import tempfile

from . import __version__
from .containers import inspect
from .doctor import doctor, repository_root, tool_environment
from .errors import HostError
from .atomic import publish_directory

MIN_BUILD_FREE = 64 * 1024 * 1024


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _source_state(repo):
    """Read source identity without depending on a caller's current directory."""
    try:
        commit = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
        dirty = bool(subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain'], text=True))
        return commit, dirty
    except (OSError, subprocess.CalledProcessError):
        return None, True  # A packager must reject unverified source identity.


def _validate_rpx(path):
    if not path.is_file():
        raise HostError("missing_artifact", "Build did not produce the expected RPX", stage="compile")
    with path.open("rb") as handle:
        header = handle.read(52)
        if (len(header) != 52 or header[:7] != b"\x7fELF\x01\x02\x01"
                or header[7] != 0xCA or header[16:20] != b"\xfe\x01\x00\x14"):
            raise HostError("invalid_artifact", "Expected a big-endian PowerPC Wii U RPX ELF", stage="compile")
        section_offset = struct.unpack_from(">I", header, 32)[0]
        header_size, section_size, section_count, names_index = (
            struct.unpack_from(">H", header, offset)[0] for offset in (40, 46, 48, 50))
        if (header_size != 52 or section_size != 40 or not 0 < section_count <= 4096
                or names_index >= section_count or section_offset < 52
                or section_offset + section_size * section_count > path.stat().st_size):
            raise HostError("invalid_artifact", "RPX ELF section table is invalid", stage="compile")
        handle.seek(section_offset)
        sections = handle.read(section_size * section_count)
        for index in range(section_count):
            _, kind, _, _, offset, size, _, _, _, _ = struct.unpack_from(">10I", sections, index * 40)
            if kind not in (0, 8) and offset + size > path.stat().st_size:
                raise HostError("invalid_artifact", "RPX ELF section extends beyond the artifact", stage="compile")


def _run_build(command, env, log, cwd, *, stage="compile", timeout=None):
    """No shell interpolation; cancellation terminates and joins the compiler."""
    with log.open("wb") as output:
        process_options = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
                           if os.name == "nt" else {"start_new_session": True})
        process = subprocess.Popen(command, env=env, cwd=cwd, stdout=output,
                                   stderr=subprocess.STDOUT, **process_options)
        try:
            result = process.wait(timeout=timeout)
        except BaseException:
            if process.poll() is None:
                try:
                    if os.name == "nt":
                        process.send_signal(signal.CTRL_BREAK_EVENT)
                    else:
                        os.killpg(process.pid, signal.SIGTERM)
                except (OSError, ProcessLookupError):
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    if os.name == "nt":
                        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                       capture_output=True, check=False)
                    else:
                        os.killpg(process.pid, signal.SIGKILL)
                    process.kill()
                    process.wait()
            raise
    if result:
        raise HostError("compile_failed" if stage=="compile" else "guest_execution_failed", "Compilation/execution failed; inspect the retained log", stage=stage,
                        details={"exit_code": result, "log": str(log)})


def prototype(output: str | Path, guest_path=None, *, backend="original", audio_only=False, scene_only=False, stream_audio=False, m1_only=False):
    if m1_only and (guest_path is not None or backend != "original" or audio_only or scene_only or stream_audio):
        raise HostError("invalid_profile", "M1 Check requires its own native platform profile", stage="arguments")
    if stream_audio and (audio_only or scene_only or guest_path is not None or backend != "original"):
        raise HostError("invalid_profile", "Audio Streams requires its own native profile without guest inputs", stage="arguments")
    if scene_only and (audio_only or guest_path is not None or backend != "original"):
        raise HostError("invalid_profile", "The original Scene Check requires its own native profile without guest inputs", stage="arguments")
    if audio_only and (guest_path is not None or backend != "original"):
        raise HostError("invalid_profile", "The original native Audio Check does not accept a guest input or external backend", stage="arguments")
    if backend not in ("original","upstream") or (backend=="upstream" and guest_path is None):
        raise HostError("invalid_backend", "Upstream builds require the original guest fixture", stage="arguments")
    repo = repository_root()
    source_commit, source_dirty = _source_state(repo)
    image = None
    if guest_path is not None:
        from .guest_pipeline import generated_sources, profiled_image
        image = profiled_image(guest_path)
        for name,contents in generated_sources(image).items():
            source=repo/"tests/fixtures/guest"/name
            if not source.is_file() or source.read_text()!=contents:
                raise HostError("stale_guest_sources", "Regenerate the original guest fixture sources before building", stage="translate")
    output = Path(output).expanduser().absolute()
    if output.exists():
        raise HostError("output_exists", "Output already exists. Choose a new directory to preserve existing files", stage="prepare")
    report = doctor()
    if not report["ok"]:
        raise HostError("missing_toolchain", report["next_action"], stage="prepare", details={"missing": report["missing_required"]})
    makefile = repo / "runtime" / "wiiu" / "Makefile"
    if not makefile.is_file():
        raise HostError("missing_build_recipe", "runtime/wiiu/Makefile is unavailable", stage="prepare")
    output.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(output.parent).free < MIN_BUILD_FREE:
        raise HostError("insufficient_disk", "At least 64 MiB free disk space is required for prototype staging", stage="prepare")
    # Stage beside the destination so the final rename stays on one filesystem.
    staging = Path(tempfile.mkdtemp(prefix=".ctr2wup-staging-", dir=output.parent))
    logs = repo / "artifacts" / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / ("prototype-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + staging.name.rsplit("-", 1)[-1] + ".log")
    work = None
    try:
        env = tool_environment()
        command = [shutil.which("make", path=env.get("PATH")), "-C", str(makefile.parent)]
        target="ctr2wup-guest-fixture" if image else "ctr2wup-prototype"
        if audio_only:
            target="ctr2wup-audio-check"
            command += ["OUTPUT=../../artifacts/wiiu-audio", "TARGET="+target, "AUDIO_ONLY=1"]
        if stream_audio:
            target="ctr2wup-audio-stream-check"
            command += ["OUTPUT=../../artifacts/wiiu-audio-streams", "TARGET="+target, "STREAM_AUDIO=1"]
        if m1_only:
            target="ctr2wup-m1-check"
            command += ["OUTPUT=../../artifacts/wiiu-m1", "TARGET="+target, "M1_ONLY=1", "GUEST_DEFAULT=0"]
        if scene_only:
            target="ctr2wup-scene-check"
            command += ["OUTPUT=../../artifacts/wiiu-scene", "TARGET="+target, "SCENE_ONLY=1"]
        if image:
            command += ["OUTPUT=../../artifacts/wiiu-guest", "TARGET="+target, "GUEST_DEFAULT=1"]
        generation=None
        if backend=="upstream":
            from translator.upstream import generate_into
            work=Path(tempfile.mkdtemp(prefix="wiiu-upstream-",dir=repo/"artifacts"))
            generation=generate_into(guest_path,work/"aot")
            target="ctr2wup-upstream-guest"
            command=[shutil.which("make",path=env.get("PATH")),"-C",str(makefile.parent),
                     "OUTPUT=../../artifacts/"+work.name+"/wiiu","TARGET="+target,"GUEST_DEFAULT=1",
                     "EXTERNAL_AOT=1","UPSTREAM_DIR=../../artifacts/"+work.name+"/aot","-j3"]
        _run_build(command, env, log, repo)
        if (source_commit, source_dirty) != _source_state(repo):
            raise HostError('source_changed', 'Source identity changed during compilation; use a fresh build', stage='compile')
        artifact = repo / "artifacts" / ("wiiu-guest" if image else "wiiu-m1" if m1_only else "wiiu-audio-streams" if stream_audio else "wiiu-audio" if audio_only else "wiiu-scene" if scene_only else "wiiu") / (target+".rpx")
        if work: artifact=work/"wiiu"/(target+".rpx")
        _validate_rpx(artifact)
        shutil.copy2(artifact, staging / artifact.name)
        _validate_rpx(staging / artifact.name)
        published = [staging / artifact.name]
        shortcut = artifact.with_suffix(".wuhb")
        if shortcut.is_file():
            with shortcut.open("rb") as handle:
                if handle.read(4) != b"WUHB":
                    raise HostError("invalid_artifact", "WUHB shortcut header is invalid", stage="compile")
            shutil.copy2(shortcut, staging / shortcut.name)
            published.append(staging / shortcut.name)
        shutil.copy2(log, staging / "build.log")
        manifest = {"schema_version": 1, "ok": True, "stage": "complete",
                    "target": "guest-fixture" if image else "m1-check" if m1_only else "audio-stream-check" if stream_audio else "audio-check" if audio_only else "scene-check" if scene_only else "prototype", "package_format": "RPX",
                    "host_version": __version__, "runtime_version": __version__,
                    "source_commit": source_commit, "source_dirty": source_dirty,
                    "translator_version": ("external-abi4-checked-v1" if backend=="upstream" else "bounded-aot-v2") if image else None,
                    "backend":backend,"generation":generation,
                    "profile": "ctr2wup-framebuffer-fixture-v1" if image else "native-m1" if m1_only else "native-audio-streams" if stream_audio else "native-audio-check" if audio_only else "owned-software-scenes-v1" if scene_only else "native-diagnostics",
                    "input_identity": image["inspection"]["identity"] if image else None,
                    "executable_revision": image["inspection"]["executable_revision"] if image else None,
                    "package_identity": None,
                    "display": {"top_guest_size": [400, 240], "bottom_guest_size": [320, 240],
                                "top_output": "TV", "bottom_output": "GamePad"},
                    "toolchain": report,
                    "built_at_utc": datetime.now(timezone.utc).isoformat(),
                    "artifacts": [{"name": item.name, "size": item.stat().st_size,
                                   "sha256": _sha256(item)} for item in published],
                    "stages": [{"name": "compile", "state": "passed"},
                               {"name": "rpx_header_validation", "state": "passed"},
                               {"name": "console_dual_screen_input_audio_exit", "state": "awaiting_console_test"}],
                    "limitations": ["RPX homebrew launch artifact; this directory is not an installable WUP",
                                    "Cross-compilation does not establish console execution"]}
        if image:
            manifest["stages"].insert(0,{"name":"fixture_profile_and_generated_sources","state":"passed"})
            manifest["stages"].append({"name":"guest_framebuffer_input_exit","state":"awaiting_console_test"})
            manifest["limitations"].append("Original controlled ARM fixture with explicit input/raw-framebuffer contract; HID/GSP/IPC/PICA unavailable")
        if stream_audio:
            manifest["stages"].append({"name":"original_cstm_mixing_native_pcm_human_hearing","state":"awaiting_console_test"})
            manifest["limitations"].append("Original checked CSTM, two portable voices and fixed finite native PCM; guest DSP IPC, asynchronous streaming and audible hardware output remain unverified")
        if m1_only:
            manifest["m1_acceptance"]={"full_native_guide":True,"storage_protocol":"scratch-v1",
                "sentinel_bytes":128,"process_session_trace":True,"installed_wup":False,
                "console_verified":False,"full_milestone_acceptance":False}
            manifest["stages"].append({"name":"native_controls_stability_sd_recovery_fresh_lifecycle","state":"awaiting_console_test"})
            manifest["limitations"].append("Original closed-write/readback and software recovery probe; no power-loss durability or installed-WUP acceptance")
        if scene_only:
            manifest["stages"].append({"name":"original_scene_pixels_and_user_display_confirmation","state":"awaiting_console_test"})
            manifest["limitations"].append("Original exact ADD/MUL scenes rasterized on CPU and copied to GX2 surfaces; physical picture/performance and GPU shader translation remain unverified")
        (staging / "build-report.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        publish_directory(staging, output)
        return {**manifest, "output": str(output)}
    finally:
        if work and work.exists(): shutil.rmtree(work)
        if staging.exists():
            shutil.rmtree(staging)


def commercial(path, output):
    inspection = inspect(path)
    raise HostError("unsupported_profile", "ROM inspection succeeded, but commercial ARM/HLE/PICA conversion is not yet supported. No package was generated. Use 'build --target prototype' for the native Wii U diagnostic", stage="profile",
                    details={"identity": inspection["identity"],
                             "executable_revision": inspection["executable_revision"],
                             "compatibility": "unsupported",
                             "required_gates": ["translated guest boot", "required HLE services", "PICA rendering", "physical Wii U gameplay/save verification"]})
