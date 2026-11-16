"""Exercise the frozen Windows programs using original local fixtures only."""
import argparse
import asyncio
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def screenshots(gui, kit, output):
    import win32con
    import win32gui
    from PIL import ImageGrab
    for page in range(1, 4):
        process = subprocess.Popen([str(gui), "--kit", str(kit), "--preview-page", str(page)])
        handle = None
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                handles = []
                def collect(handle, context):
                    if win32gui.IsWindowVisible(handle) and win32gui.GetWindowText(handle) == "CTR2WUP · Wii U MCP Setup":
                        handles.append(handle)
                    return True
                win32gui.EnumWindows(collect, None)
                if handles:
                    handle = handles[0]
                    break
                if process.poll() is not None:
                    raise RuntimeError("Installer exited before showing a window")
                time.sleep(0.2)
            if handle is None:
                raise RuntimeError("Installer window did not appear")
            time.sleep(0.5)
            ImageGrab.grab(bbox=win32gui.GetWindowRect(handle)).save(output / f"installer-step-{page}.png")
        finally:
            if handle:
                win32gui.PostMessage(handle, win32con.WM_CLOSE, 0, 0)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=10)


def check(kit, output):
    output.mkdir(parents=True)
    gui = kit / "Install Wii U MCP.exe"
    subprocess.run([str(gui), "--kit", str(kit), "--self-test", "--report", str(output / "gui-check.json")], check=True, timeout=40)
    backend = load("windows_setup", kit / "setup.py")
    actions = load("windows_installer_actions", kit / "installer-actions.py")
    with tempfile.TemporaryDirectory(prefix="CTR2WUP installer check ") as temporary:
        root = Path(temporary)
        installed = root / "Installed bridge"
        result = actions.install(backend, kit, installed)
        assert result["command"] == [str(installed / "runtime/WiiU-MCP-Bridge.exe")]
        assert not (installed / "local/venv").exists(), "Frozen install unexpectedly needed a Python environment"
        shortcut = actions.make_shortcut(installed, root / "Start menu/Wii U MCP.lnk")
        assert Path(shortcut).is_file()
        ftp_root = root / "ftp/fs/vol/external01"
        ftp_root.mkdir(parents=True)
        authorizer = DummyAuthorizer()
        authorizer.add_anonymous(str(root / "ftp"))

        class Handler(FTPHandler):
            def log(self, *args, **kwargs):
                pass
            def logline(self, *args, **kwargs):
                pass

        Handler.authorizer = authorizer
        server = FTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, kwargs={"timeout": 0.05, "handle_exit": False}, daemon=True)
        thread.start()
        connection = actions.configure(backend, installed, "127.0.0.1")
        config = Path(connection["configuration"])
        with config.open("a") as stream:
            stream.write("ftp_port = " + str(server.socket.getsockname()[1]) + "\n")
        codex = root / "Codex/config.toml"
        actions.register_codex(connection["client"], codex)
        assert codex.is_file()
        card = root / "SD card"
        (card / "wiiu/environments/aroma").mkdir(parents=True)
        copied = actions.copy_sd(backend, installed, actions.sd_plan(backend, installed, card))
        assert copied["copied"] == 11
        env = dict(os.environ)
        env.pop("PYTHONPATH", None)
        env.pop("PYTHONHOME", None)
        env["PATH"] = str(Path(os.environ["SYSTEMROOT"]) / "System32")
        try:
            async def connect():
                params = StdioServerParameters(command=result["command"][0], args=["--config", str(config)], env=env)
                async with stdio_client(params) as (read, write):
                    async with ClientSession(read, write, read_timeout_seconds=30) as session:
                        await session.initialize()
                        tools = await session.list_tools()
                        assert len(tools.tools) == 12
                        capabilities = await session.call_tool("wiiu_capabilities", {})
                        assert not capabilities.is_error
                        status = await session.call_tool("wiiu_status", {})
                        assert not status.is_error and status.structured_content["sd_readable"]
            asyncio.run(connect())
        finally:
            server.close_all()
            thread.join(timeout=5)
    screenshots(gui, kit, output)
    report = {"ok": True, "source_commit": json.loads((kit / "manifest.json").read_text())["source_commit"],
              "frozen_bridge_handshake": True, "python_install_required": False,
              "sd_fixture_copied": True, "codex_fixture_registered": True, "physical_console_contacted": False}
    (output / "windows-check.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.kit.resolve(), args.output.resolve())))
