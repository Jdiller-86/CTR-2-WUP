#!/usr/bin/env python3
"""Guided desktop setup for an existing Aroma Wii U and its companion computer."""
import argparse
import importlib.util
import ipaddress
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import traceback


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Wizard:
    def __init__(self, window, kit):
        self.window, self.kit = window, kit.resolve()
        self.backend = load("wiiu_setup", self.kit / "setup.py")
        self.actions = load("wiiu_installer_actions", self.kit / "installer-actions.py")
        self.manifest = self.backend.verify(self.kit)
        self.events = queue.Queue()
        self.busy = False
        self.page = 0
        self.prepared = None
        self.connection = None
        self.plan = None
        self.card_copied = False
        base = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / ".local/share")))
        default = base / "CTR2WUP/WiiU-MCP" / self.manifest["version"]
        if (self.kit / "local/install-state.json").is_file():
            default = self.kit
        self.destination = tk.StringVar(value=str(default))
        self.sd_root = tk.StringVar()
        self.sd_root.trace_add("write", self.card_selection_changed)
        self.address = tk.StringVar()
        self.add_codex = tk.BooleanVar(value=True)
        self.status = tk.StringVar(value="Ready to install. Your Wii U can stay offline.")
        self.title = tk.StringVar()
        self.description = tk.StringVar()
        window.title("CTR2WUP · Wii U MCP Setup")
        window.geometry("980x680")
        window.minsize(860, 620)
        window.configure(background="#edf2f7")
        window.protocol("WM_DELETE_WINDOW", self.close)
        self.style()
        window.columnconfigure(1, weight=1)
        window.rowconfigure(0, weight=1)
        sidebar = tk.Frame(window, bg="#15283e", width=220)
        sidebar.grid(row=0, column=0, sticky="nsew")
        sidebar.grid_propagate(False)
        tk.Label(sidebar, text="CTR2WUP", font=("Segoe UI", 17, "bold"), fg="white", bg="#15283e").pack(anchor="w", padx=26, pady=(34, 4))
        tk.Label(sidebar, text="Wii U MCP setup", font=("Segoe UI", 11), fg="#b7c8dc", bg="#15283e").pack(anchor="w", padx=26)
        self.steps = []
        for number, text in enumerate(("Install bridge", "Prepare SD card", "Connect Codex"), 1):
            label = tk.Label(sidebar, text=str(number) + "   " + text, font=("Segoe UI", 11), fg="#91a6bd", bg="#15283e", anchor="w")
            label.pack(fill="x", padx=26, pady=(48 if number == 1 else 22, 0))
            self.steps.append(label)
        tk.Label(sidebar, text="Starter kit " + self.manifest["version"], font=("Segoe UI", 9), fg="#91a6bd", bg="#15283e").pack(side="bottom", anchor="w", padx=26, pady=26)
        main = ttk.Frame(window, padding=(32, 30), style="Background.TFrame")
        main.grid(row=0, column=1, sticky="nsew")
        main.columnconfigure(0, weight=1)
        main.rowconfigure(2, weight=1)
        ttk.Label(main, textvariable=self.title, style="Heading.TLabel").grid(row=0, column=0, sticky="w")
        intro = ttk.Label(main, textvariable=self.description, style="Intro.TLabel", wraplength=620)
        intro.grid(row=1, column=0, sticky="w", pady=(10, 22))
        viewport = ttk.Frame(main, style="Card.TFrame")
        viewport.grid(row=2, column=0, sticky="nsew")
        viewport.columnconfigure(0, weight=1)
        viewport.rowconfigure(0, weight=1)
        self.canvas = tk.Canvas(viewport, bg="white", highlightthickness=0)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(viewport, orient="vertical", command=self.canvas.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.canvas.configure(yscrollcommand=scrollbar.set)
        self.card = ttk.Frame(self.canvas, padding=24, style="Card.TFrame")
        card_window = self.canvas.create_window((0, 0), window=self.card, anchor="nw")
        self.canvas.bind("<Configure>", lambda event: self.canvas.itemconfigure(card_window, width=event.width))
        self.card.columnconfigure(0, weight=1)
        self.card.bind("<Configure>", self.card_layout)
        window.bind_all("<MouseWheel>", lambda event: self.canvas.yview_scroll(-1 if event.delta > 0 else 1, "units"))
        window.bind_all("<Button-4>", lambda event: self.canvas.yview_scroll(-1, "units"))
        window.bind_all("<Button-5>", lambda event: self.canvas.yview_scroll(1, "units"))
        self.progress = ttk.Progressbar(main, mode="indeterminate")
        self.progress.grid(row=3, column=0, sticky="ew", pady=(18, 9))
        status = ttk.Label(main, textvariable=self.status, style="Status.TLabel", wraplength=620)
        status.grid(row=4, column=0, sticky="w")
        main.bind("<Configure>", lambda event: [label.configure(wraplength=max(360, event.width - 64)) for label in (intro, status)])
        self.footer = ttk.Frame(main, style="Background.TFrame")
        self.footer.grid(row=5, column=0, sticky="ew", pady=(20, 0))
        self.footer.columnconfigure(1, weight=1)
        self.show(0)
        window.after(100, self.drain)

    def style(self):
        style = ttk.Style(self.window)
        style.theme_use("clam")
        style.configure("TLabel", font=("Segoe UI", 11), background="white", foreground="#24364b")
        style.configure("Card.TFrame", background="white")
        style.configure("Background.TFrame", background="#edf2f7")
        style.configure("Heading.TLabel", font=("Segoe UI", 23, "bold"), background="#edf2f7", foreground="#15283e")
        style.configure("Intro.TLabel", background="#edf2f7", foreground="#53667b")
        style.configure("Status.TLabel", font=("Segoe UI", 10), background="#edf2f7", foreground="#53667b")
        style.configure("Strong.TLabel", font=("Segoe UI", 12, "bold"))
        style.configure("TButton", font=("Segoe UI", 10), padding=(14, 10))
        style.configure("Accent.TButton", background="#087e83", foreground="white", borderwidth=0)
        style.map("Accent.TButton", background=[("active", "#066a70"), ("disabled", "#8fa6b0")])
        style.configure("TEntry", padding=9, font=("Segoe UI", 11))
        style.configure("TCheckbutton", background="white", font=("Segoe UI", 11))

    def label(self, text, row, *, strong=False):
        ttk.Label(self.card, text=text, style="Strong.TLabel" if strong else "TLabel", wraplength=570).grid(row=row, column=0, sticky="w", pady=(0, 16))

    def wrap_labels(self, parent, width):
        for widget in parent.winfo_children():
            if isinstance(widget, ttk.Label) and int(str(widget.cget("wraplength"))) != width:
                widget.configure(wraplength=width)

    def card_layout(self, event):
        self.wrap_labels(self.card, max(320, event.width - 48))
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def folder_field(self, variable, row, title):
        frame = ttk.Frame(self.card, style="Card.TFrame")
        frame.grid(row=row, column=0, sticky="ew", pady=(0, 20))
        frame.columnconfigure(0, weight=1)
        ttk.Entry(frame, textvariable=variable).grid(row=0, column=0, sticky="ew")
        ttk.Button(frame, text="Browse…", command=lambda: self.choose(variable, title)).grid(row=0, column=1, padx=(10, 0))

    def choose(self, variable, title):
        folder = filedialog.askdirectory(parent=self.window, title=title)
        if folder:
            variable.set(folder)
            self.plan = None
            if variable is self.sd_root:
                self.check_card()

    def card_selection_changed(self, *unused):
        self.plan = None
        if hasattr(self, "copy_button") and self.copy_button.winfo_exists():
            self.copy_button.configure(state="disabled")

    def footer_button(self, text, command, column=2, accent=True):
        ttk.Button(self.footer, text=text, command=command, style="Accent.TButton" if accent else "TButton").grid(row=0, column=column, sticky="e" if column else "w", padx=(12 if column else 0, 0))

    def show(self, page):
        if self.busy:
            return
        self.page = page
        self.canvas.yview_moveto(0)
        for widget in (*self.card.winfo_children(), *self.footer.winfo_children()):
            widget.destroy()
        for index, label in enumerate(self.steps):
            label.configure(fg="white" if index == page else "#91a6bd")
        if page == 0:
            self.title.set("Set up Wii U testing")
            self.description.set("Three steps to install the bridge, prepare your SD card, and connect local Codex.")
            self.label("Install on this computer", 0, strong=True)
            self.label("Choose a permanent folder for the bridge. You won’t need an administrator account.", 1)
            self.folder_field(self.destination, 2, "Choose the bridge installation folder")
            self.label("The Windows kit includes Python and the bridge dependencies. Your Wii U can stay offline during this step.", 3)
            self.label("Have an existing Aroma setup? Your plugins and modules will be checked in the next step.", 4)
            self.footer_button("Install & continue", self.install)
        elif page == 1:
            self.title.set("Prepare your SD card")
            self.description.set("Use the SD card from your existing Aroma installation. Keep the Wii U powered off while the card is removed.")
            self.label("Choose the SD card root", 0, strong=True)
            self.label("Select the drive or folder containing wiiu/environments/aroma.", 1)
            self.folder_field(self.sd_root, 2, "Select the SD card root")
            self.sd_summary = tk.StringVar(value="Check the card to see which files will be copied. Changed files are backed up first. Games and saves are left in place.")
            ttk.Label(self.card, textvariable=self.sd_summary, wraplength=570).grid(row=3, column=0, sticky="w", pady=(0, 18))
            controls = ttk.Frame(self.card, style="Card.TFrame")
            controls.grid(row=4, column=0, sticky="w")
            ttk.Button(controls, text="Check card", command=self.check_card).grid(row=0, column=0)
            self.copy_button = ttk.Button(controls, text="Back up & copy files", command=self.copy_card, state="disabled")
            self.copy_button.grid(row=0, column=1, padx=12)
            self.footer_button("Back", lambda: self.show(0), column=0, accent=False)
            self.footer_button("Continue" if self.card_copied else "Do this later", lambda: self.show(2))
        else:
            self.title.set("Connect Codex")
            self.description.set("Enter your Wii U’s address when you’re home. You can finish this step later.")
            self.label("Wii U IP address", 0, strong=True)
            ttk.Entry(self.card, textvariable=self.address, width=28).grid(row=1, column=0, sticky="w", pady=(0, 10))
            self.label("Find the address in Aroma’s FTPiiU settings. Both devices must be on your home network.", 2)
            ttk.Checkbutton(self.card, text="Add the bridge to local Codex automatically", variable=self.add_codex).grid(row=3, column=0, sticky="w", pady=(0, 18))
            controls = ttk.Frame(self.card, style="Card.TFrame")
            controls.grid(row=4, column=0, sticky="w", pady=(0, 16))
            ttk.Button(controls, text="Save connection", command=self.save_connection, style="Accent.TButton").grid(row=0, column=0)
            ttk.Button(controls, text="Test connection", command=self.test_connection).grid(row=0, column=1, padx=10)
            ttk.Button(controls, text="Copy settings", command=self.copy_configuration).grid(row=0, column=2)
            self.label("After saving, restart Codex to load the tools. Cloud Codex also needs a secure route to this computer.", 5)
            self.footer_button("Back", lambda: self.show(1), column=0, accent=False)
            self.footer_button("Done", self.close)

    def task(self, function, done):
        if self.busy:
            return
        self.busy = True
        self.progress.start(12)
        self.locked = []
        def lock(parent):
            for widget in parent.winfo_children():
                if isinstance(widget, (ttk.Button, ttk.Entry, ttk.Checkbutton)):
                    self.locked.append((widget, widget.cget("state")))
                    widget.configure(state="disabled")
                lock(widget)
        lock(self.card)
        lock(self.footer)

        def worker():
            try:
                self.events.put(("done", (done, function())))
            except Exception as error:
                self.events.put(("error", (str(error), traceback.format_exc())))
        threading.Thread(target=worker, daemon=True).start()

    def drain(self):
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "progress":
                    self.status.set(value)
                    continue
                self.busy = False
                self.progress.stop()
                for widget, state in self.locked:
                    widget.configure(state=state)
                if kind == "done":
                    callback, result = value
                    callback(result)
                else:
                    self.status.set(value[0])
                    messagebox.showerror("Setup needs attention", value[0], parent=self.window)
        except queue.Empty:
            pass
        self.window.after(100, self.drain)

    def install(self):
        target = Path(self.destination.get())
        self.status.set("Installing the bridge…")
        def install():
            result = self.actions.install(self.backend, self.kit, target,
                progress=lambda text: self.events.put(("progress", text)))
            try:
                self.actions.make_shortcut(Path(result["installation"]))
            except (OSError, subprocess.SubprocessError):
                result["shortcut_unavailable"] = True
            return result
        self.task(install, self.installed)

    def installed(self, result):
        self.prepared = result
        self.destination.set(result["installation"])
        config = Path(result["installation"]) / "local/wiiu.toml"
        if config.is_file():
            import tomllib
            self.address.set(tomllib.loads(config.read_text()).get("console_host", ""))
        self.status.set("Bridge installed. Prepare the card now, or continue later.")
        self.show(1)

    def check_card(self):
        if not self.sd_root.get():
            self.status.set("Choose your SD card folder first.")
            return
        card = Path(self.sd_root.get())
        self.card_selection_changed()
        self.task(lambda: self.actions.sd_plan(self.backend, self.kit, card), self.card_checked)

    def card_checked(self, plan):
        self.plan = plan
        self.sd_summary.set(f"{plan['new']} new files · {plan['replace']} files to back up and replace · {plan['same']} already up to date")
        self.copy_button.configure(state="normal")
        self.status.set("Card checked. Choose Back up & copy files to apply this plan.")

    def copy_card(self):
        if self.plan:
            plan = self.plan
            self.task(lambda: self.actions.copy_sd(self.backend, self.kit, plan), self.card_complete)

    def card_complete(self, result):
        self.card_copied = True
        self.plan = None
        self.copy_button.configure(state="disabled")
        text = f"Copied and verified {result['copied']} files. Safely eject the card, then restart Aroma."
        if result["backup"]:
            text += " Backups: " + result["backup"]
        self.sd_summary.set(text)
        self.status.set("SD card prepared. Enable FTPiiU and wiiload in Aroma; leave system-file access disabled.")
        for button in self.footer.winfo_children():
            if button.cget("text") == "Do this later":
                button.configure(text="Continue")

    def save_connection(self):
        if not self.prepared:
            self.status.set("Install the bridge first.")
            return
        try:
            ipaddress.IPv4Address(self.address.get().strip())
        except ValueError:
            self.status.set("Enter a valid Wii U IP address, or finish setup and return when you’re home.")
            return
        destination, address, register = Path(self.destination.get()), self.address.get().strip(), self.add_codex.get()
        def save():
            connection = self.actions.configure(self.backend, destination, address)
            if register:
                codex = Path(os.environ.get("CODEX_HOME", str(Path.home() / ".codex"))) / "config.toml"
                self.actions.register_codex(connection["client"], codex)
            return connection
        self.task(save, self.connection_saved)

    def connection_saved(self, result):
        self.connection = result
        self.status.set("Connection saved. Restart Codex to load the Wii U tools. You can test the connection when the console is ready.")

    def test_connection(self):
        if not self.connection:
            self.status.set("Save the connection settings first.")
            return
        self.status.set("Checking FTPiiU and the SD card…")
        self.task(lambda: self.actions.test_connection(self.connection["command"], self.connection["configuration"]),
                  lambda result: self.status.set("FTPiiU connected · SD card readable. Restart Codex if you haven’t already."))

    def copy_configuration(self):
        path = Path(self.destination.get()) / "local/mcp-server.toml"
        if not path.is_file():
            self.status.set("Save connection settings before copying the configuration.")
            return
        self.window.clipboard_clear()
        self.window.clipboard_append(path.read_text())
        self.status.set("Configuration copied. You can paste it into your MCP client’s settings.")

    def close(self):
        if self.busy:
            self.status.set("Please let the current operation finish before closing setup.")
            return
        self.window.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--preview-page", type=int, choices=(1, 2, 3))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    kit = args.kit or (Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).parent)
    window = tk.Tk()
    try:
        if not all((kit / name).is_file() for name in ("manifest.json", "setup.py", "installer-actions.py")):
            raise ValueError("Choose Extract All on the downloaded ZIP, then open the installer from the extracted folder.")
        wizard = Wizard(window, kit)
        if args.preview_page:
            wizard.show(args.preview_page - 1)
        if args.self_test:
            sizes = []
            for page in range(3):
                wizard.show(page)
                window.update()
                assert wizard.title.get() and wizard.card.winfo_width() > 400
                for widget in wizard.card.winfo_children():
                    assert widget.winfo_y() + widget.winfo_height() <= wizard.card.winfo_height(), "Setup content is clipped"
                sizes.append({"page": page + 1, "width": wizard.card.winfo_width(), "height": wizard.card.winfo_height()})
            report = {"ok": True, "pages": sizes, "console_contacted": False}
            if args.report:
                args.report.write_text(json.dumps(report, indent=2) + "\n")
            window.destroy()
            return 0
        window.mainloop()
    except Exception as error:
        if args.self_test:
            if args.report:
                args.report.write_text(json.dumps({"ok": False, "error": str(error)}) + "\n")
            window.destroy()
            return 1
        messagebox.showerror("Cannot open setup", str(error), parent=window)
        window.destroy()
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
