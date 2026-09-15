#!/usr/bin/env python3
"""
PureC OS — Build & Setup TUI
Usage:
    ./purec.py                  interactive menu
    ./purec.py setup            clone all external repos
    ./purec.py build            build everything (kernel + programs + iso)
    ./purec.py build kernel     build kernel only
    ./purec.py build programs   build ring-3 programs
    ./purec.py build iso        build ISO image
    ./purec.py run              launch in QEMU
    ./purec.py clean            remove build artefacts
    ./purec.py status           show which repos are present
"""

import curses
import os
import subprocess
import sys
import textwrap
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional

# ─────────────────────────── project root ────────────────────────────────────
ROOT = os.path.dirname(os.path.abspath(__file__))
BIN  = os.path.join(ROOT, "bin")
ISO  = os.path.join(BIN, "purec_limine.iso")

# ────────────────────────── external repos ───────────────────────────────────
@dataclass
class Repo:
    name:    str
    url:     str
    dest:    str          # relative to ROOT
    check:   str          # file that proves the repo is present
    desc:    str

REPOS: List[Repo] = [
    Repo("libxcrypt",       "https://github.com/PureC-OS/libxcrypt.git",
         "libxcrypt",       "libxcrypt/src/sha512.c",
         "SHA-512 / \$6\$ password hashing"),
    Repo("PureC-OS-ACPI",   "https://github.com/PureC-OS/PureC-OS-ACPI.git",
         "acpi",            "acpi/src/acpi.c",
         "ACPI subsystem (RSDP/XSDT/FADT/DSDT, shutdown/reboot)"),
    Repo("PureC-TCC",       "https://github.com/PureC-OS/PureC-TCC.git",
         "tcc",             "tcc",
         "C compiler port for Ring-3 programs"),
    Repo("PureC-OS-Userspace","https://github.com/PureC-OS/PureC-OS-Userspace.git",
         "userspace",       "userspace",
         "Desktop environment, applications, Ring-3 libraries"),
    Repo("PureC-notepad-OS","https://github.com/PureC-OS/PureC-notepad-OS.git",
         "purec-notepad-os","purec-notepad-os/Makefile",
         "PureC Notepad with Syscall Lib and GUI Lib"),
]

# ─────────────────────────── ANSI palette ────────────────────────────────────
class C:
    RESET  = "\033[0m"
    BOLD   = "\033[1m"
    DIM    = "\033[2m"
    # Catppuccin Mocha-ish
    MAUVE  = "\033[38;2;203;166;247m"
    BLUE   = "\033[38;2;137;180;250m"
    GREEN  = "\033[38;2;166;227;161m"
    YELLOW = "\033[38;2;249;226;175m"
    RED    = "\033[38;2;243;139;168m"
    PEACH  = "\033[38;2;250;179;135m"
    TEXT   = "\033[38;2;205;214;244m"
    SUBTEXT= "\033[38;2;166;173;200m"
    BASE   = "\033[48;2;30;30;46m"
    SURFACE= "\033[48;2;49;50;68m"

def clr(color: str, text: str) -> str:
    return f"{color}{text}{C.RESET}"

# ─────────────────────────── ASCII logo ──────────────────────────────────────
LOGO = [
    r" ____                  ____   ___  ____  ",
    r"|  _ \ _   _ _ __ ___ / ___| / _ \/ ___| ",
    r"| |_) | | | | '__/ _ \ |    | | | \___ \ ",
    r"|  __/| |_| | | |  __/ |___  |_| |___) |",
    r"|_|    \__,_|_|  \___|\____|\___/|____/ ",
]

# ──────────────────────────── helpers ────────────────────────────────────────
def _abs(rel: str) -> str:
    return os.path.join(ROOT, rel)

def _present(repo: Repo) -> bool:
    return os.path.exists(_abs(repo.check))

def _print_header():
    print()
    for line in LOGO:
        print(clr(C.MAUVE, C.BOLD + line))
    print(clr(C.SUBTEXT, "  64-bit OS written in C and x86_64 assembly"))
    print()

def _box(title: str, lines: List[str], width: int = 62):
    tl, tr, bl, br, h, v = "╭", "╮", "╰", "╯", "─", "│"
    inner = width - 2
    title_str = f" {title} "
    pad = inner - len(title_str)
    top = tl + title_str + h * pad + tr
    bot = bl + h * inner + br
    print(clr(C.BLUE, top))
    for l in lines:
        line = l[:inner].ljust(inner)
        print(clr(C.BLUE, v) + " " + clr(C.TEXT, line[:-1]) + clr(C.BLUE, v))
    print(clr(C.BLUE, bot))

def _run(cmd: List[str], cwd: str = ROOT) -> int:
    print(clr(C.DIM + C.SUBTEXT, "  $ " + " ".join(cmd)))
    result = subprocess.run(cmd, cwd=cwd)
    return result.returncode

def _step(label: str):
    print(f"\n{clr(C.MAUVE, '●')} {clr(C.BOLD + C.TEXT, label)}")

def _ok(msg: str):
    print(f"  {clr(C.GREEN, '✓')} {clr(C.TEXT, msg)}")

def _warn(msg: str):
    print(f"  {clr(C.YELLOW, '⚠')} {clr(C.YELLOW, msg)}")

def _err(msg: str):
    print(f"  {clr(C.RED, '✗')} {clr(C.RED, msg)}")

def _die(msg: str):
    _err(msg)
    sys.exit(1)

# ──────────────────────────── commands ───────────────────────────────────────

def cmd_status():
    _print_header()
    rows = []
    for r in REPOS:
        icon = clr(C.GREEN, "✓ present ") if _present(r) else clr(C.RED, "✗ missing ")
        rows.append(f"{icon} {r.name:<26} {clr(C.SUBTEXT, r.desc)}")
    _box("Dependency Status", rows, width=72)
    iso_status = clr(C.GREEN, "✓ " + ISO) if os.path.isfile(ISO) else clr(C.RED, "✗ not built")
    print(f"\n  ISO: {iso_status}\n")


def cmd_setup(only: Optional[str] = None):
    _print_header()
    _step("Fetching external repositories")
    ok_count = 0
    for r in REPOS:
        if only and r.name.lower() != only.lower():
            continue
        dest_abs = _abs(r.dest)
        if _present(r):
            _ok(f"{r.name} — already present, skipping")
            ok_count += 1
            continue
        print(f"\n  {clr(C.BLUE, '↓')} Cloning {clr(C.MAUVE, r.name)} …")
        rc = _run(["git", "clone", "--depth=1", r.url, dest_abs])
        if rc != 0:
            _err(f"Failed to clone {r.name}")
        else:
            _ok(f"{r.name} cloned → {r.dest}/")
            ok_count += 1
    print()
    _ok(f"Done — {ok_count}/{len(REPOS)} repos ready") if not only else None


def _make(*args: str) -> int:
    return _run(["make", "-C", ROOT, "--no-print-directory", *args])


def cmd_build(target: str = "all"):
    _print_header()
    targets_map = {
        "all":      [],
        "kernel":   ["kernel"],
        "programs": ["programs"],
        "iso":      ["iso"],
        "libs":     ["libraries"],
        "notepad":  ["notepad"],
        "hexedit":  ["hexedit"],
        "userspace":["userspace"],
    }
    if target not in targets_map:
        _die(f"Unknown build target '{target}'. Choose: {', '.join(targets_map)}")

    _step(f"Building: {target}")
    rc = _make(*targets_map[target])
    print()
    if rc == 0:
        _ok("Build succeeded!")
        if target in ("all", "iso") and os.path.isfile(ISO):
            size_mb = os.path.getsize(ISO) / 1_048_576
            _ok(f"ISO ready: {ISO}  ({size_mb:.1f} MB)")
    else:
        _err(f"Build failed (exit {rc})")
    return rc


def cmd_clean():
    _print_header()
    _step("Cleaning build artefacts")
    _make("clean")
    _ok("Done")


def cmd_run():
    _print_header()
    if not os.path.isfile(ISO):
        _warn("ISO not found — building first…")
        rc = cmd_build("all")
        if rc != 0:
            _die("Build failed, cannot run")

    # Try QEMU
    qemu = "qemu-system-x86_64"
    if subprocess.run(["which", qemu], capture_output=True).returncode == 0:
        _step("Launching in QEMU")
        _run([
            qemu,
            "-cdrom", ISO,
            "-m", "512M",
            "-enable-kvm",
            "-serial", "stdio",
            "-vga", "std",
            "-boot", "d",
        ])
    else:
        _warn("QEMU not found. Install qemu-system-x86 or open the ISO in VirtualBox:")
        print(f"  {clr(C.PEACH, ISO)}")


# ──────────────────────── interactive TUI menu ───────────────────────────────

@dataclass
class MenuItem:
    key:   str
    label: str
    desc:  str
    action: Callable

def _interactive_menu():
    items: List[MenuItem] = [
        MenuItem("1", "Setup / Fetch deps",   "Clone all external repositories",          lambda: cmd_setup()),
        MenuItem("2", "Build all",            "Build kernel + programs + ISO",            lambda: cmd_build("all")),
        MenuItem("3", "Build kernel",         "Build kernel only",                        lambda: cmd_build("kernel")),
        MenuItem("4", "Build programs",       "Build Ring-3 user programs",               lambda: cmd_build("programs")),
        MenuItem("5", "Build ISO",            "Assemble the bootable ISO image",          lambda: cmd_build("iso")),
        MenuItem("6", "Run (QEMU)",           "Launch PureC OS in QEMU",                  lambda: cmd_run()),
        MenuItem("7", "Dependency status",    "Show which external repos are present",    lambda: cmd_status()),
        MenuItem("8", "Clean",                "Remove all build artefacts",               lambda: cmd_clean()),
        MenuItem("q", "Quit",                 "",                                         lambda: sys.exit(0)),
    ]

    while True:
        os.system("clear")
        _print_header()

        key_w  = 4
        lab_w  = 24
        desc_w = 40
        sep = clr(C.BLUE, "│")

        header = (clr(C.SURFACE, " ") +
                  clr(C.BOLD + C.MAUVE, " Key ") + sep +
                  clr(C.BOLD + C.MAUVE, f" {'Action':<{lab_w}}") + sep +
                  clr(C.BOLD + C.MAUVE, f" {'Description':<{desc_w}}"))
        print(header)
        print(clr(C.BLUE, "─" * (key_w + lab_w + desc_w + 6)))

        for it in items:
            key_str  = clr(C.YELLOW, f" [{it.key}]")
            lab_str  = f" {clr(C.TEXT, it.label):<{lab_w + 10}}"
            desc_str = f" {clr(C.SUBTEXT, it.desc)}"
            print(f"{key_str} {sep}{lab_str}{sep}{desc_str}")

        print(clr(C.BLUE, "─" * (key_w + lab_w + desc_w + 6)))
        print()

        try:
            choice = input(clr(C.MAUVE, "  ❯ ") + clr(C.TEXT, "Enter choice: ")).strip().lower()
        except (KeyboardInterrupt, EOFError):
            print()
            sys.exit(0)

        matched = [it for it in items if it.key == choice]
        if not matched:
            _warn(f"Unknown option '{choice}'")
            time.sleep(1)
            continue

        print()
        matched[0].action()
        print()
        input(clr(C.SUBTEXT, "  Press Enter to return to menu…"))


# ──────────────────────────── entry point ────────────────────────────────────

def main():
    args = sys.argv[1:]

    if not args:
        _interactive_menu()
        return

    cmd = args[0].lower()

    if cmd == "setup":
        cmd_setup(args[1] if len(args) > 1 else None)
    elif cmd == "build":
        target = args[1] if len(args) > 1 else "all"
        sys.exit(cmd_build(target))
    elif cmd == "run":
        cmd_run()
    elif cmd == "clean":
        cmd_clean()
    elif cmd == "status":
        cmd_status()
    elif cmd in ("-h", "--help", "help"):
        print(__doc__)
    else:
        print(clr(C.RED, f"Unknown command: {cmd}"))
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()

