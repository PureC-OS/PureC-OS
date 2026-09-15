#!/usr/bin/env python3
"""
PureC OS — Build & Setup TUI
Usage:
    ./purec.py                  interactive menu
    ./purec.py setup            clone all external repos
    ./purec.py build [target]   build (all / kernel / programs / iso / libs / notepad / hexedit / userspace)
    ./purec.py run              launch in QEMU
    ./purec.py clean            remove build artefacts
    ./purec.py status           show which repos are present
"""

import os
import re
import select
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from typing import Callable, List, Optional

ROOT = os.path.dirname(os.path.abspath(__file__))
BIN  = os.path.join(ROOT, "bin")
ISO  = os.path.join(BIN, "purec_limine.iso")

# ──────────────────────────────────────────────────────────────
# Catppuccin Mocha palette
# ──────────────────────────────────────────────────────────────
class C:
    RST    = "\033[0m"
    BOLD   = "\033[1m"
    DIM    = "\033[2m"
    MAUVE  = "\033[38;2;203;166;247m"
    BLUE   = "\033[38;2;137;180;250m"
    GREEN  = "\033[38;2;166;227;161m"
    YELLOW = "\033[38;2;249;226;175m"
    RED    = "\033[38;2;243;139;168m"
    PEACH  = "\033[38;2;250;179;135m"
    SKY    = "\033[38;2;137;220;235m"
    TEXT   = "\033[38;2;205;214;244m"
    SUB    = "\033[38;2;166;173;200m"
    OVERLAY= "\033[38;2;108;112;134m"
    BG     = "\033[48;2;30;30;46m"
    SURF   = "\033[48;2;49;50;68m"
    HIDE_CURSOR = "\033[?25l"
    SHOW_CURSOR = "\033[?25h"
    CLEAR_LINE  = "\033[2K\r"

def c(col, text): return f"{col}{text}{C.RST}"

# ──────────────────────────────────────────────────────────────
# Terminal width helper
# ──────────────────────────────────────────────────────────────
def tw() -> int:
    try:    return os.get_terminal_size().columns
    except: return 80

# ──────────────────────────────────────────────────────────────
# External repos
# ──────────────────────────────────────────────────────────────
@dataclass
class Repo:
    name:  str
    url:   str
    dest:  str
    check: str
    desc:  str

REPOS: List[Repo] = [
    Repo("libxcrypt",          "https://github.com/PureC-OS/libxcrypt.git",
         "libxcrypt",          "libxcrypt/src/sha512.c",
         "SHA-512 / $6$ password hashing"),
    Repo("PureC-OS-ACPI",      "https://github.com/PureC-OS/PureC-OS-ACPI.git",
         "acpi",               "acpi/src/acpi.c",
         "ACPI (shutdown / reboot)"),
    Repo("PureC-TCC",          "https://github.com/PureC-OS/PureC-TCC.git",
         "tcc",                "tcc",
         "C compiler port for Ring-3"),
    Repo("PureC-OS-Userspace", "https://github.com/PureC-OS/PureC-OS-Userspace.git",
         "userspace",          "userspace",
         "Desktop + Ring-3 apps"),
    Repo("PureC-notepad-OS",   "https://github.com/PureC-OS/PureC-notepad-OS.git",
         "purec-notepad-os",   "purec-notepad-os/Makefile",
         "Notepad with GUI & Syscall lib"),
]

def _present(r: Repo) -> bool:
    return os.path.exists(os.path.join(ROOT, r.check))

# ──────────────────────────────────────────────────────────────
# ASCII logo
# ──────────────────────────────────────────────────────────────
LOGO = [
    r" ____                  ____   ___  ____  ",
    r"|  _ \ _   _ _ __ ___ / ___| / _ \/ ___| ",
    r"| |_) | | | | '__/ _ \ |    | | | \___ \ ",
    r"|  __/| |_| | | |  __/ |___  |_| |___) |",
    r"|_|    \__,_|_|  \___|\____|\___/|____/ ",
]

def _header():
    print()
    for line in LOGO:
        print(c(C.MAUVE, C.BOLD + line))
    w = tw()
    subtitle = "  64-bit OS written in C and x86_64 assembly"
    print(c(C.SUB, subtitle))
    print(c(C.OVERLAY, "  " + "─" * (w - 4)))
    print()

# ──────────────────────────────────────────────────────────────
# Spinner
# ──────────────────────────────────────────────────────────────
SPINNER_FRAMES = ["⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"]

class Spinner:
    def __init__(self, label: str):
        self.label   = label
        self._stop   = threading.Event()
        self._thread = threading.Thread(target=self._spin, daemon=True)

    def _spin(self):
        i = 0
        print(C.HIDE_CURSOR, end="", flush=True)
        while not self._stop.is_set():
            frame = c(C.MAUVE, SPINNER_FRAMES[i % len(SPINNER_FRAMES)])
            lbl   = c(C.TEXT, self.label)
            sys.stdout.write(f"{C.CLEAR_LINE}  {frame}  {lbl}")
            sys.stdout.flush()
            time.sleep(0.08)
            i += 1

    def start(self):
        self._thread.start()
        return self

    def stop(self, ok: bool = True, note: str = ""):
        self._stop.set()
        self._thread.join()
        icon  = c(C.GREEN, "✓") if ok else c(C.RED, "✗")
        label = c(C.TEXT, self.label)
        extra = (c(C.SUB, "  " + note)) if note else ""
        sys.stdout.write(f"{C.CLEAR_LINE}  {icon}  {label}{extra}\n")
        sys.stdout.flush()
        print(C.SHOW_CURSOR, end="", flush=True)

# ──────────────────────────────────────────────────────────────
# Live-log box (shows last N lines of process output)
# ──────────────────────────────────────────────────────────────

# Noise patterns we want to hide
_NOISE = re.compile(
    r"^\s*(make\[|\bNothing to be done\b|^make:.*Entering|^make:.*Leaving"
    r"|^\s*$)", re.IGNORECASE
)

def _clean_line(raw: str) -> str:
    """Strip ANSI, trailing whitespace."""
    ansi = re.compile(r"\033\[[0-9;]*[a-zA-Z]")
    return ansi.sub("", raw).rstrip()

LOG_LINES = 6   # how many lines to show in the live box

class LiveLog:
    """Renders a fixed-height scrolling log panel in the terminal."""

    def __init__(self, title: str):
        self.title  = title
        self.lines: List[str] = []
        self._rendered = 0

    def _box_top(self):
        w   = tw() - 2
        ttl = f" {self.title} "
        bar = "─" * (w - len(ttl) - 2)
        print(c(C.BLUE, f"╭{ttl}{bar}╮"))

    def _box_bot(self):
        w = tw() - 2
        print(c(C.BLUE, f"╰{'─' * w}╯"))

    def push(self, raw: str):
        cl = _clean_line(raw)
        if not cl or _NOISE.match(cl):
            return
        # Truncate to terminal width
        w = tw() - 6
        if len(cl) > w:
            cl = cl[:w-1] + "…"
        self.lines.append(cl)
        self._redraw()

    def _redraw(self):
        visible = self.lines[-LOG_LINES:]
        # Move cursor up to overwrite previous render
        if self._rendered:
            sys.stdout.write(f"\033[{self._rendered + 2}A")

        self._box_top()
        w = tw() - 4
        for ln in visible:
            row = ln[:w].ljust(w)
            print(c(C.BLUE, "│") + " " + c(C.SUB, row) + " " + c(C.BLUE, "│"))
        # Pad empty rows
        for _ in range(LOG_LINES - len(visible)):
            print(c(C.BLUE, "│") + " " * (w + 2) + c(C.BLUE, "│"))
        self._box_bot()
        self._rendered = LOG_LINES
        sys.stdout.flush()

    def clear(self):
        if self._rendered:
            # erase the box lines
            for _ in range(self._rendered + 2):
                sys.stdout.write(f"\033[1A{C.CLEAR_LINE}")
        self._rendered = 0
        sys.stdout.flush()

# ──────────────────────────────────────────────────────────────
# Process runner with live output
# ──────────────────────────────────────────────────────────────

def _run_live(cmd: List[str], title: str, cwd: str = ROOT) -> int:
    """Run cmd, stream stdout/stderr into a LiveLog box. Return exit code."""
    log = LiveLog(title)
    # Print initial empty box
    log._redraw()

    proc = subprocess.Popen(
        cmd, cwd=cwd,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1,
    )
    for line in proc.stdout:
        log.push(line)
    proc.wait()

    # Final status line
    ok = proc.returncode == 0
    icon = c(C.GREEN, "✓") if ok else c(C.RED, "✗")
    status = c(C.GREEN, "Success") if ok else c(C.RED, f"Failed (exit {proc.returncode})")
    # Replace bottom border with status
    sys.stdout.write(f"\033[1A{C.CLEAR_LINE}")
    print(c(C.BLUE, "│") + f" {icon} {status}".ljust(tw() - 4) + " " + c(C.BLUE, "│"))
    print(c(C.BLUE, f"╰{'─' * (tw() - 2)}╯"))
    sys.stdout.flush()
    return proc.returncode


def _run_git_clone(url: str, dest: str, name: str) -> int:
    """Clone with progress shown in spinner + live log."""
    print(f"\n  {c(C.BLUE, '↓')}  Cloning {c(C.MAUVE + C.BOLD, name)}")
    cmd = ["git", "clone", "--depth=1", "--progress", url, dest]
    return _run_live(cmd, f"git clone {name}")


def _make_live(title: str, *args: str) -> int:
    cmd = ["make", "-C", ROOT, *args]
    return _run_live(cmd, title)

# ──────────────────────────────────────────────────────────────
# Feedback helpers
# ──────────────────────────────────────────────────────────────
def _ok(msg):   print(f"  {c(C.GREEN,  '✓')}  {c(C.TEXT, msg)}")
def _warn(msg): print(f"  {c(C.YELLOW, '⚠')}  {c(C.YELLOW, msg)}")
def _err(msg):  print(f"  {c(C.RED,    '✗')}  {c(C.RED, msg)}")
def _step(msg): print(f"\n  {c(C.MAUVE, '●')}  {c(C.BOLD + C.TEXT, msg)}\n")
def _die(msg):  _err(msg); print(C.SHOW_CURSOR); sys.exit(1)

# ──────────────────────────────────────────────────────────────
# Commands
# ──────────────────────────────────────────────────────────────

def cmd_status():
    _header()
    w = tw() - 2
    ttl = " Dependency Status "
    bar = "─" * (w - len(ttl) - 2)
    print(c(C.BLUE, f"╭{ttl}{bar}╮"))
    for r in REPOS:
        present = _present(r)
        icon  = c(C.GREEN, "✓ present") if present else c(C.RED,  "✗ missing")
        name  = c(C.MAUVE if present else C.RED, f"{r.name:<26}")
        desc  = c(C.SUB, r.desc)
        row   = f" {icon}  {name} {desc}"
        pad   = w - len(_clean_line(row))
        print(c(C.BLUE, "│") + row + " " * max(pad, 1) + c(C.BLUE, "│"))
    print(c(C.BLUE, f"╰{'─' * w}╯"))
    print()
    if os.path.isfile(ISO):
        mb = os.path.getsize(ISO) / 1_048_576
        _ok(f"ISO: {ISO}  ({mb:.1f} MB)")
    else:
        _warn("ISO: not built yet")
    print()


def cmd_setup(only: Optional[str] = None):
    _header()
    _step("Fetching external repositories")
    total = ok_count = 0
    for r in REPOS:
        if only and r.name.lower() != only.lower():
            continue
        total += 1
        if _present(r):
            _ok(f"{r.name} — already present")
            ok_count += 1
            continue
        dest_abs = os.path.join(ROOT, r.dest)
        rc = _run_git_clone(r.url, dest_abs, r.name)
        if rc != 0:
            _err(f"Failed to clone {r.name}")
        else:
            _ok(f"{r.name} → {r.dest}/")
            ok_count += 1
    print()
    _ok(f"Done — {ok_count}/{total} repos ready")
    print()


TARGETS = {
    "all":       ([], "Build everything"),
    "kernel":    (["kernel"], "Build kernel"),
    "programs":  (["programs"], "Build Ring-3 programs"),
    "iso":       (["iso"], "Assemble ISO image"),
    "libs":      (["libraries"], "Build libraries"),
    "notepad":   (["notepad"], "Build PureC Notepad"),
    "hexedit":   (["hexedit"], "Build HexEdit"),
    "userspace": (["userspace"], "Build Userspace"),
}

def cmd_build(target: str = "all") -> int:
    _header()
    if target not in TARGETS:
        _die(f"Unknown target '{target}'. Choose: {', '.join(TARGETS)}")
    args, title = TARGETS[target]
    _step(title)
    rc = _make_live(title, *args)
    print()
    if rc == 0:
        _ok("Build succeeded!")
        if target in ("all", "iso") and os.path.isfile(ISO):
            mb = os.path.getsize(ISO) / 1_048_576
            _ok(f"ISO: {ISO}  ({mb:.1f} MB)")
    else:
        _err(f"Build failed (exit {rc})")
    print()
    return rc


def cmd_clean():
    _header()
    _step("Cleaning build artefacts")
    _make_live("make clean", "clean")
    _ok("Done")
    print()


LOG_FILE = os.path.join(ROOT, "bin", "qemu-debug.log")

def _ensure_iso() -> bool:
    if os.path.isfile(ISO):
        return True
    _warn("ISO not found — building first…")
    return cmd_build("all") == 0

def _qemu_ok() -> bool:
    return subprocess.run(["which", "qemu-system-x86_64"],
                          capture_output=True).returncode == 0

def _qemu_base() -> List[str]:
    return ["qemu-system-x86_64", "-cdrom", ISO, "-m", "512M",
            "-enable-kvm", "-boot", "d"]

# ─── 4 debug modes ───────────────────────────────────────────────
def qemu_graphics():
    _header()
    if not _ensure_iso() or not _qemu_ok(): return
    _step("QEMU — graphical mode")
    _run_live(_qemu_base() + ["-vga", "std", "-serial", "stdio"], "QEMU (graphics)")

def qemu_nographic():
    _header()
    if not _ensure_iso() or not _qemu_ok(): return
    _step("QEMU — no graphics  (Ctrl-A X to quit)")
    print(c(C.SUB, "  Tip: Ctrl-A then X exits QEMU\n"))
    subprocess.run(_qemu_base() + ["-nographic"])

def qemu_log_console():
    _header()
    if not _ensure_iso() or not _qemu_ok(): return
    _step("QEMU — graphics + live serial log in TUI")
    _run_live(_qemu_base() + ["-vga", "std", "-serial", "stdio"],
              "QEMU (serial → console)")

def qemu_log_file():
    _header()
    if not _ensure_iso() or not _qemu_ok(): return
    os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
    int_log = LOG_FILE.replace(".log", "-internal.log")
    _step(f"QEMU — graphics + logs → file")
    print(c(C.SUB, f"  Serial : {LOG_FILE}"))
    print(c(C.SUB, f"  QEMU   : {int_log}\n"))
    _run_live(
        _qemu_base() + [
            "-vga", "std",
            "-serial", f"file:{LOG_FILE}",
            "-D", int_log,
            "-d", "int,cpu_reset,guest_errors",
        ],
        "QEMU (log → file)",
    )
    if os.path.isfile(LOG_FILE):
        _ok(f"Serial log: {LOG_FILE}")

# ─── Debug submenu ───────────────────────────────────────────────
def cmd_debug():
    while True:
        os.system("clear")
        _header()

        K, L, D = 5, 26, 42
        def _sep(): print(c(C.OVERLAY, "  " + "─" * (K + L + D + 6)))

        print(f"  {c(C.BOLD + C.PEACH, '⚙  Debug / Run options')}\n")
        head = (f"  {c(C.BOLD+C.PEACH,'Key'):<{K+14}} {c(C.BLUE,'│')} "
                f"{c(C.BOLD+C.PEACH,'Mode'):<{L+14}} {c(C.BLUE,'│')} "
                f"{c(C.BOLD+C.PEACH,'Description')}")
        print(head)
        _sep()

        debug_items = [
            ("1", "Run with graphics",    "Normal QEMU window, serial → console",   qemu_graphics),
            ("2", "Run without graphics", "No window — serial in terminal (nographic)", qemu_nographic),
            ("3", "Log → console",        "Graphics + live serial log in TUI box",  qemu_log_console),
            ("4", "Log → file",           f"Graphics + serial & QEMU log → bin/*.log", qemu_log_file),
            ("b", "Back",                 "",                                        None),
        ]
        for key, lab, desc, _ in debug_items:
            k  = c(C.YELLOW, f"[{key}]") + " " * max(0, K - len(key) - 2)
            l  = c(C.TEXT, lab)          + " " * max(0, L - len(lab))
            d  = c(C.SUB, desc)
            print(f"  {k}  {c(C.BLUE,'│')} {l}  {c(C.BLUE,'│')} {d}")
        _sep()
        print()

        try:
            choice = input(f"  {c(C.PEACH,'❯')} {c(C.TEXT,'Choice: ')}").strip().lower()
        except (KeyboardInterrupt, EOFError):
            return

        if choice == "b":
            return
        matched = [(k, l, d, fn) for k, l, d, fn in debug_items if k == choice]
        if not matched:
            _warn(f"Unknown option '{choice}'"); time.sleep(0.8); continue

        os.system("clear")
        matched[0][3]()
        try:
            input(c(C.SUB, "  Press Enter to return to debug menu…"))
        except (KeyboardInterrupt, EOFError):
            return

def cmd_run():
    qemu_graphics()

# ──────────────────────────────────────────────────────────────
# Interactive TUI menu
# ──────────────────────────────────────────────────────────────
@dataclass
class MenuItem:
    key:    str
    label:  str
    desc:   str
    action: Callable

def _menu():
    items: List[MenuItem] = [
        MenuItem("1", "Setup / Fetch deps",    "Clone all external repositories",   lambda: cmd_setup()),
        MenuItem("2", "Build all",             "Build kernel + programs + ISO",     lambda: cmd_build("all")),
        MenuItem("3", "Build kernel",          "Build kernel only",                 lambda: cmd_build("kernel")),
        MenuItem("4", "Build programs",        "Build Ring-3 user programs",        lambda: cmd_build("programs")),
        MenuItem("5", "Build ISO",             "Assemble the bootable ISO image",   lambda: cmd_build("iso")),
        MenuItem("6", "Run  (QEMU)",           "Launch PureC OS in QEMU",           lambda: cmd_run()),
        MenuItem("7", "Dependency status",     "Show which external repos present", lambda: cmd_status()),
        MenuItem("8", "Clean",                 "Remove all build artefacts",        lambda: cmd_clean()),
        MenuItem("9", "Debug / Run options",   "Submenu: graphics / nographic / logs", lambda: cmd_debug()),
        MenuItem("q", "Quit",                  "",                                  lambda: sys.exit(0)),
    ]

    while True:
        os.system("clear")
        _header()

        w = tw()
        # Column widths
        K, L, D = 5, 22, 38

        def _sep(): print(c(C.OVERLAY, "  " + "─" * (K + L + D + 6)))

        head = (f"  {c(C.BOLD + C.MAUVE, 'Key'):<{K+14}} "
                f"{c(C.BLUE, '│')} {c(C.BOLD + C.MAUVE, 'Action'):<{L+14}} "
                f"{c(C.BLUE, '│')} {c(C.BOLD + C.MAUVE, 'Description')}")
        print(head)
        _sep()

        for it in items:
            key  = c(C.YELLOW, f"[{it.key}]")
            lab  = c(C.TEXT,   it.label)
            desc = c(C.SUB,    it.desc)
            # fixed width via raw-length padding
            key_p  = key  + " " * max(0, K  - len(it.key) - 2)
            lab_p  = lab  + " " * max(0, L  - len(it.label))
            print(f"  {key_p}  {c(C.BLUE,'│')} {lab_p}  {c(C.BLUE,'│')} {desc}")

        _sep()
        print()

        try:
            choice = input(
                f"  {c(C.MAUVE,'❯')} {c(C.TEXT,'Choice: ')}"
            ).strip().lower()
        except (KeyboardInterrupt, EOFError):
            print()
            sys.exit(0)

        matched = [it for it in items if it.key == choice]
        if not matched:
            _warn(f"Unknown option '{choice}'")
            time.sleep(0.8)
            continue

        os.system("clear")
        matched[0].action()
        try:
            input(c(C.SUB, "  Press Enter to return to menu…"))
        except (KeyboardInterrupt, EOFError):
            print()
            sys.exit(0)

# ──────────────────────────────────────────────────────────────
# Entry point
# ──────────────────────────────────────────────────────────────
def main():
    args = sys.argv[1:]
    if not args:
        _menu()
        return
    cmd = args[0].lower()
    if   cmd == "setup":   cmd_setup(args[1] if len(args) > 1 else None)
    elif cmd == "build":   sys.exit(cmd_build(args[1] if len(args) > 1 else "all"))
    elif cmd == "run":     cmd_run()
    elif cmd == "clean":   cmd_clean()
    elif cmd == "status":  cmd_status()
    elif cmd in ("-h","--help","help"): print(__doc__)
    else:
        print(c(C.RED, f"Unknown command: {cmd}")); print(__doc__); sys.exit(1)

if __name__ == "__main__":
    main()
