#!/usr/bin/env python3
"""
PureC OS — Build & Setup TUI
Usage:
    ./purec.py                  interactive menu (arrows / hotkeys / mouse)
    ./purec.py setup [repo]     clone all (or one) external repos
    ./purec.py build [target]   build (all / kernel / programs / iso / libs / notepad / hexedit / userspace)
    ./purec.py run              launch in QEMU
    ./purec.py debug            QEMU debug / run submenu
    ./purec.py clean            remove build artefacts
    ./purec.py status           show repos, ISO and host tools
"""

import contextlib
import os
import queue
import re
import select
import shutil
import subprocess
import sys
import textwrap
import threading
import time
from dataclasses import dataclass
from typing import Callable, List, Optional, Tuple

try:
    import termios
    import tty
except ImportError:
    termios = None
    tty = None

ROOT = os.path.dirname(os.path.abspath(__file__))
BIN  = os.path.join(ROOT, "bin")
ISO  = os.path.join(BIN, "purec_limine.iso")
LAST_LOG = os.path.join(BIN, "purec-last.log")

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
    ALT_ON      = "\033[?1049h"
    ALT_OFF     = "\033[?1049l"
    MOUSE_ON    = "\033[?1000h\033[?1006h"
    MOUSE_OFF   = "\033[?1006l\033[?1000l"

ANSI_RE = re.compile(r"\033\[[0-9;?<]*[a-zA-Z]")

def c(col, text): return f"{col}{text}{C.RST}"
def vlen(s: str) -> int: return len(ANSI_RE.sub("", s))
def pad(s: str, n: int) -> str: return s + " " * max(0, n - vlen(s))
def clip(s: str, n: int) -> str:
    if n <= 0: return ""
    return s if len(s) <= n else s[:max(0, n - 1)] + "…"

def _rgb(r, g, b): return f"\033[38;2;{r};{g};{b}m"

def gradient(i: int, n: int) -> str:
    t = i / max(1, n - 1)
    a, b = (203, 166, 247), (137, 220, 235)
    return _rgb(*[int(a[k] + (b[k] - a[k]) * t) for k in range(3)])

def tw() -> int: return shutil.get_terminal_size((80, 24)).columns
def th() -> int: return shutil.get_terminal_size((80, 24)).lines

def is_tty() -> bool:
    return bool(termios) and sys.stdin.isatty() and sys.stdout.isatty()

def fmt_time(sec: float) -> str:
    s = int(sec)
    return f"{s // 60}:{s % 60:02d}"

def ago(ts: float) -> str:
    d = max(0, time.time() - ts)
    if d < 60:    return "just now"
    if d < 3600:  return f"{int(d // 60)} min ago"
    if d < 86400: return f"{int(d // 3600)} h ago"
    return f"{int(d // 86400)} d ago"

def bar(pct: int, width: int = 16, col: str = C.MAUVE) -> str:
    pct = max(0, min(100, pct))
    f = int(width * pct / 100)
    return c(col, "█" * f) + c(C.OVERLAY, "░" * (width - f))

@dataclass
class Repo:
    name:  str
    url:   str
    dest:  str
    check: str
    desc:  str

REPOS: List[Repo] = [
    Repo("wolfssl",            "https://github.com/PureC-OS/wolfssl.git",
         "wolfssl",            "wolfssl/wolfcrypt/src/aes.c",
         "wolfCrypt / TLS stack"),
    Repo("libxcrypt",          "https://github.com/PureC-OS/libxcrypt.git",
         "libxcrypt",          "libxcrypt/src/sha512.c",
         "SHA-512 / $6$ password hashing"),
    Repo("PureC-OS-ACPI",      "https://github.com/PureC-OS/PureC-OS-ACPI.git",
         "acpi",               "acpi/src/acpi.c",
         "ACPI (shutdown / reboot)"),
    Repo("PureC-OS-UACPI",     "https://github.com/uACPI/uACPI.git",
         "acpi/AML/uACPI",      "acpi/AML/uACPI/source",
         "uACPI AML interpreter"),
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

def _deps_ready() -> Tuple[int, int]:
    return sum(_present(r) for r in REPOS), len(REPOS)

LOGO = [
    r" ____                  ____   ___  ____  ",
    r"|  _ \ _   _ _ __ ___ / ___| / _ \/ ___| ",
    r"| |_) | | | | '__/ _ \ |    | | | \___ \ ",
    r"|  __/| |_| | | |  __/ |___  |_| |___) |",
    r"|_|    \__,_|_|  \___|\____|\___/|____/ ",
]
SUBTITLE = "64-bit OS written in C and x86_64 assembly"

def _rule(width: int) -> str:
    return c(C.OVERLAY, "  " + "─" * max(4, width - 4))

def _header_lines(logo: bool = True) -> List[str]:
    w = min(tw(), 110)
    out = [""]
    if logo and w >= 48:
        for i, line in enumerate(LOGO):
            out.append("  " + c(C.BOLD + gradient(i, len(LOGO)), line))
        out.append("  " + c(C.SUB, clip(SUBTITLE, w - 4)))
    else:
        out.append("  " + c(C.BOLD + C.MAUVE, "◆ PureC OS") + c(C.SUB, "  Build & Setup"))
    out.append(_rule(w))
    return out

def _header():
    print("\n".join(_header_lines()))
    print()

SPINNER_FRAMES = ["⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"]

_NOISE = re.compile(
    r"^\s*(make\[|\bNothing to be done\b|make:.*Entering|make:.*Leaving)",
    re.IGNORECASE
)
_PROGRESS = re.compile(
    r"(?:\[\s*(\d{1,3})%\]|(?:Receiving objects|Resolving deltas|Compressing objects|Counting objects):\s+(\d{1,3})%)"
)

def _clean_line(raw: str) -> str:
    return ANSI_RE.sub("", raw).rstrip()

class LiveLog:
    def __init__(self, title: str):
        self.title = title
        self.rows  = max(4, min(12, th() // 3))
        self.lines: List[str] = []
        self.total = 0
        self.pct: Optional[int] = None
        self.drawn = False

    def push(self, raw: str):
        cl = _clean_line(raw)
        if not cl:
            return
        self.total += 1
        m = _PROGRESS.search(cl)
        if m:
            self.pct = int(m.group(1) or m.group(2))
        if _NOISE.match(cl):
            return
        self.lines.append(cl)
        if len(self.lines) > 400:
            del self.lines[:200]

    def draw(self, frame: str, elapsed: float, final: Optional[Tuple[bool, str]] = None):
        total = max(30, tw() - 1)
        inner = total - 4
        if final is None:
            col = C.BLUE
        else:
            col = C.GREEN if final[0] else C.RED

        t = clip(self.title, max(8, total - 24))
        left  = f"╭─ {frame} {t} "
        right = f" {fmt_time(elapsed)} ─╮"
        fill  = "─" * max(1, total - len(left) - len(right))
        out = [c(col, left) + c(col, fill) + c(C.SUB, f" {fmt_time(elapsed)} ") + c(col, "─╮")]

        visible = self.lines[-self.rows:]
        for ln in visible + [""] * (self.rows - len(visible)):
            row = clip(ln, inner).ljust(inner)
            out.append(c(col, "│") + " " + c(C.SUB, row) + " " + c(col, "│"))

        if final is None:
            lt = c(C.MAUVE, frame) + " " + c(C.TEXT, "Running…") + c(C.OVERLAY, f"  {self.total} lines")
            rt = ""
            if self.pct is not None:
                rt = bar(self.pct, 14) + c(C.TEXT, f" {self.pct:3d}%")
        else:
            icon = c(C.GREEN, "✓") if final[0] else c(C.RED, "✗")
            lt = icon + " " + c(C.GREEN if final[0] else C.RED, final[1])
            rt = c(C.OVERLAY, f"{self.total} lines")
        gap = max(1, inner - vlen(lt) - vlen(rt))
        out.append(c(col, "│") + " " + lt + " " * gap + rt + " " + c(col, "│"))
        out.append(c(col, "╰" + "─" * (total - 2) + "╯"))

        buf = ""
        if self.drawn:
            buf += f"\033[{self.rows + 3}A\r"
        buf += "".join(line + "\033[K\n" for line in out)
        sys.stdout.write(buf)
        sys.stdout.flush()
        self.drawn = True

def _run_live(cmd: List[str], title: str, cwd: str = ROOT) -> int:
    if not sys.stdout.isatty():
        return subprocess.call(cmd, cwd=cwd)

    log   = LiveLog(title)
    q: "queue.Queue[Optional[str]]" = queue.Queue()
    full: List[str] = []
    start = time.time()

    try:
        proc = subprocess.Popen(
            cmd, cwd=cwd,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1, errors="replace",
        )
    except FileNotFoundError:
        _err(f"Command not found: {cmd[0]}")
        return 127

    def reader():
        for line in proc.stdout:
            q.put(line)
        q.put(None)

    threading.Thread(target=reader, daemon=True).start()

    sys.stdout.write(C.HIDE_CURSOR)
    i = 0
    done = False
    cancelled = False
    try:
        log.draw(SPINNER_FRAMES[0], 0)
        while not done:
            try:
                item = q.get(timeout=0.08)
                while True:
                    if item is None:
                        done = True
                        break
                    full.append(item)
                    log.push(item)
                    item = q.get_nowait()
            except queue.Empty:
                pass
            i += 1
            log.draw(SPINNER_FRAMES[i % len(SPINNER_FRAMES)], time.time() - start)
        proc.wait()
    except KeyboardInterrupt:
        cancelled = True
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    elapsed = time.time() - start
    rc = 130 if cancelled else proc.returncode
    if cancelled:
        final = (False, f"Cancelled after {fmt_time(elapsed)}")
    elif rc == 0:
        final = (True, f"Success in {fmt_time(elapsed)}")
    else:
        final = (False, f"Failed (exit {rc}) after {fmt_time(elapsed)}")
    log.draw("•", elapsed, final)
    sys.stdout.write(C.SHOW_CURSOR)

    if rc != 0:
        os.makedirs(BIN, exist_ok=True)
        try:
            with open(LAST_LOG, "w") as f:
                f.writelines(full)
        except OSError:
            pass
        tail = [_clean_line(x) for x in full if _clean_line(x)][-18:]
        if tail:
            print(f"\n  {c(C.RED, 'Last output:')}")
            for ln in tail:
                print("  " + c(C.OVERLAY, "│ ") + c(C.SUB, clip(ln, tw() - 8)))
            print(f"  {c(C.OVERLAY, 'Full log: ' + LAST_LOG)}")
    sys.stdout.flush()
    return rc

def _run_git_clone(url: str, dest: str, name: str) -> int:
    print(f"\n  {c(C.BLUE, '↓')}  Cloning {c(C.MAUVE + C.BOLD, name)}")
    cmd = ["git", "clone", "--depth=1", "--progress", url, dest]
    return _run_live(cmd, f"git clone {name}")

def _make_live(title: str, *args: str) -> int:
    return _run_live(["make", "-C", ROOT, *args], title)

def _ok(msg):   print(f"  {c(C.GREEN,  '✓')}  {c(C.TEXT, msg)}")
def _warn(msg): print(f"  {c(C.YELLOW, '⚠')}  {c(C.YELLOW, msg)}")
def _err(msg):  print(f"  {c(C.RED,    '✗')}  {c(C.RED, msg)}")
def _info(msg): print(f"  {c(C.SKY,    'ℹ')}  {c(C.SUB, msg)}")
def _step(msg): print(f"\n  {c(C.MAUVE, '●')}  {c(C.BOLD + C.TEXT, msg)}\n")
def _die(msg):  _err(msg); print(C.SHOW_CURSOR); sys.exit(1)

_MOUSE_RE = re.compile(r"\033\[<(\d+);(\d+);(\d+)([Mm])")
_KEYS = {
    "\033[A": "up",   "\033OA": "up",
    "\033[B": "down", "\033OB": "down",
    "\033[H": "home", "\033[1~": "home", "\033OH": "home", "\033[5~": "home",
    "\033[F": "end",  "\033[4~": "end",  "\033OF": "end",  "\033[6~": "end",
    "\r": "enter", "\n": "enter",
    "\033": "esc", "\x03": "esc", "\x04": "esc",
}

def _read_raw(timeout: Optional[float]) -> Optional[str]:
    fd = sys.stdin.fileno()
    r, _, _ = select.select([fd], [], [], timeout)
    if not r:
        return None
    return os.read(fd, 64).decode(errors="ignore")

def _decode(data: str) -> Tuple:
    m = _MOUSE_RE.match(data)
    if m:
        b, y, t = int(m.group(1)), int(m.group(3)), m.group(4)
        if b == 64: return ("wheel", -1)
        if b == 65: return ("wheel", 1)
        if b == 0 and t == "M": return ("click", y)
        return ("none",)
    if data in _KEYS:
        return (_KEYS[data],)
    if data.startswith("\033"):
        return ("none",)
    return ("char", data[0].lower())

@contextlib.contextmanager
def _raw(alt: bool = False, mouse: bool = False):
    fd  = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        sys.stdout.write(C.HIDE_CURSOR + (C.ALT_ON if alt else "") + (C.MOUSE_ON if mouse else ""))
        sys.stdout.flush()
        yield
    finally:
        sys.stdout.write((C.MOUSE_OFF if mouse else "") + (C.ALT_OFF if alt else "") + C.SHOW_CURSOR)
        sys.stdout.flush()
        termios.tcsetattr(fd, termios.TCSADRAIN, old)

def _pause(msg: str = "Press any key to continue…"):
    if not is_tty():
        return
    print("\n  " + c(C.SUB, msg), end="", flush=True)
    try:
        with _raw():
            _read_raw(None)
    except KeyboardInterrupt:
        pass
    print()

def _confirm(question: str, default: bool = False) -> bool:
    hint = "Y/n" if default else "y/N"
    print(f"\n  {c(C.YELLOW, '?')}  {c(C.TEXT, question)} {c(C.SUB, '[' + hint + ']')} ", end="", flush=True)
    if not is_tty():
        ans = sys.stdin.readline().strip().lower()
        return default if not ans else ans.startswith("y")
    try:
        with _raw():
            while True:
                d = _read_raw(None)
                k = _decode(d or "")
                if k[0] == "enter":
                    print(c(C.GREEN if default else C.RED, "yes" if default else "no"))
                    return default
                if k[0] == "esc":
                    print(c(C.RED, "no"))
                    return False
                if k[0] == "char" and k[1] in "yn":
                    print(c(C.GREEN, "yes") if k[1] == "y" else c(C.RED, "no"))
                    return k[1] == "y"
    except KeyboardInterrupt:
        print()
        return False

@dataclass
class MenuItem:
    key:    str
    label:  str
    desc:   str
    action: Optional[Callable]
    icon:   str = "•"
    danger: bool = False

def _fallback_menu(title: str, items: List[MenuItem]) -> Optional[MenuItem]:
    print(f"\n  {title}")
    for it in items:
        print(f"   [{it.key}] {it.label}  {it.desc}")
    try:
        ans = input("  Choice: ").strip().lower()
    except (KeyboardInterrupt, EOFError):
        return None
    return next((it for it in items if it.key == ans), None)

def _build_menu_screen(title, items, idx, accent, status, show_logo, show_detail, rows_out):
    w, h = tw(), th()
    cw   = max(30, min(w - 4, 100))
    wide = cw >= 72
    lines = _header_lines(show_logo)
    if status:
        lines.append("  " + status())
    lines.append("")
    lines.append("  " + c(C.BOLD + accent, title))
    lines.append("")

    lw = min(max(len(it.label) for it in items) + 4, 28)
    first = len(lines) + 1
    for n, it in enumerate(items):
        sel = n == idx
        bg  = C.SURF if sel else ""
        mark = (accent + "▌") if sel else " "
        kcol = accent if sel else (C.RED if it.danger else C.YELLOW)
        lcol = C.BOLD + C.TEXT if sel else (C.RED if it.danger else C.TEXT)
        segs = [(mark, ""), (kcol, f" [{it.key}] "), (lcol, f"{it.icon} " + clip(it.label, lw - 2).ljust(lw - 2))]
        if wide and it.desc:
            segs.append((C.OVERLAY, "│ "))
            segs.append((C.SUB, clip(it.desc, cw - lw - 12)))
        row = ""
        used = 0
        for col, text in segs:
            row += bg + col + text + C.RST
            used += len(text)
        row += bg + " " * max(0, cw - used) + C.RST
        lines.append("  " + row)
        rows_out.append(first + n)

    if show_detail and not wide and items[idx].desc:
        lines.append("")
        wrapped = textwrap.wrap(items[idx].desc, max(10, cw - 6)) or [""]
        lines.append("  " + c(accent, "╭" + "─" * (cw - 2) + "╮"))
        for ln in wrapped[:3]:
            lines.append("  " + c(accent, "│ ") + c(C.TEXT, ln.ljust(cw - 4)) + c(accent, " │"))
        lines.append("  " + c(accent, "╰" + "─" * (cw - 2) + "╯"))

    lines.append("")
    hint = "↑↓/jk move · ⏎ select · hotkeys · esc back · mouse" if w >= 60 else "↑↓ ⏎ esc"
    lines.append("  " + c(C.OVERLAY, clip(hint, w - 4)))
    return lines

def select_menu(title: str, items: List[MenuItem], accent: str = C.MAUVE,
                index: int = 0, status: Optional[Callable[[], str]] = None
                ) -> Tuple[Optional[MenuItem], int]:
    if not is_tty():
        return _fallback_menu(title, items), index

    idx = max(0, min(index, len(items) - 1))
    last_size = None
    dirty = True
    rows: List[int] = []
    with _raw(alt=True, mouse=True):
        while True:
            size = (tw(), th())
            if dirty or size != last_size:
                for logo, detail in ((True, True), (False, True), (False, False)):
                    rows = []
                    lines = _build_menu_screen(title, items, idx, accent, status, logo, detail, rows)
                    if len(lines) <= size[1]:
                        break
                lines = lines[:size[1]]
                sys.stdout.write("\033[H" + "".join(l + "\033[K\r\n" for l in lines[:-1]) + lines[-1] + "\033[K\033[J")
                sys.stdout.flush()
                last_size, dirty = size, False

            try:
                data = _read_raw(0.3)
            except KeyboardInterrupt:
                return None, idx
            if data is None:
                if status:
                    dirty = True
                continue

            k = _decode(data)
            kind = k[0]
            if kind == "esc":
                return None, idx
            elif kind in ("up", "down", "wheel"):
                step = -1 if kind == "up" else 1 if kind == "down" else k[1]
                idx = (idx + step) % len(items)
                dirty = True
            elif kind == "home":
                idx, dirty = 0, True
            elif kind == "end":
                idx, dirty = len(items) - 1, True
            elif kind == "enter":
                return items[idx], idx
            elif kind == "click":
                if k[1] in rows:
                    idx = rows.index(k[1])
                    return items[idx], idx
            elif kind == "char":
                ch = k[1]
                hit = next((n for n, it in enumerate(items) if it.key == ch), None)
                if hit is not None:
                    return items[hit], hit
                if ch == "j":
                    idx, dirty = (idx + 1) % len(items), True
                elif ch == "k":
                    idx, dirty = (idx - 1) % len(items), True

def _menu_loop(title: str, items: List[MenuItem], accent: str = C.MAUVE,
               status: Optional[Callable[[], str]] = None, on_back_exit: bool = False):
    idx = 0
    while True:
        item, idx = select_menu(title, items, accent, idx, status)
        if item is None or item.action is None:
            if on_back_exit:
                print(C.SHOW_CURSOR)
                sys.exit(0)
            return
        os.system("clear")
        try:
            item.action()
        except KeyboardInterrupt:
            print(C.SHOW_CURSOR)
            _warn("Interrupted")
        _pause("Press any key to return to the menu…")

def _box(title: str, rows: List[str], col: str = C.BLUE):
    total = max(30, min(tw() - 2, 104))
    inner = total - 4
    ttl = f" {title} "
    print("  " + c(col, "╭─" + ttl + "─" * max(1, total - len(ttl) - 3) + "╮"))
    for r in rows:
        print("  " + c(col, "│") + " " + pad(r, inner) + " " + c(col, "│"))
    print("  " + c(col, "╰" + "─" * (total - 2) + "╯"))

HOST_TOOLS = [
    ("make", "build driver"),
    ("gcc", "C compiler"),
    ("nasm", "assembler"),
    ("git", "repo fetching"),
    ("xorriso", "ISO creation"),
    ("qemu-system-x86_64", "emulator"),
]

def cmd_status():
    _header()
    have, total = _deps_ready()
    w = min(tw() - 2, 104) - 4
    wide = w >= 66

    rows = []
    for r in REPOS:
        p = _present(r)
        icon = c(C.GREEN, "✓") if p else c(C.RED, "✗")
        name = c(C.MAUVE if p else C.RED, f"{r.name:<20}")
        row = f"{icon} {name}"
        if wide:
            row += " " + c(C.SUB, f"{r.desc:<32}") + " " + c(C.OVERLAY, clip(r.dest, max(8, w - 58)))
        else:
            row += " " + c(C.SUB, clip(r.desc, max(4, w - 24)))
        rows.append(row)
    rows.append("")
    pct = int(100 * have / total)
    rows.append(bar(pct, 24, C.GREEN if have == total else C.YELLOW) + c(C.TEXT, f"  {have}/{total} ready"))
    _box("Dependencies", rows, C.GREEN if have == total else C.YELLOW)
    print()

    trows = []
    for tool, desc in HOST_TOOLS:
        path = shutil.which(tool)
        icon = c(C.GREEN, "✓") if path else c(C.RED, "✗")
        trows.append(f"{icon} {c(C.TEXT, f'{tool:<20}')} {c(C.SUB, desc if wide else '')}")
    kvm = os.path.exists("/dev/kvm") and os.access("/dev/kvm", os.R_OK | os.W_OK)
    kvm_icon = c(C.GREEN, "✓") if kvm else c(C.YELLOW, "⚠")
    kvm_note = "hardware acceleration" if kvm else "unavailable — QEMU falls back to TCG"
    trows.append(f"{kvm_icon} {c(C.TEXT, '/dev/kvm'.ljust(20))} {c(C.SUB, kvm_note)}")
    _box("Host tools", trows, C.BLUE)
    print()

    if os.path.isfile(ISO):
        st = os.stat(ISO)
        _ok(f"ISO: {ISO}  ({st.st_size / 1_048_576:.1f} MB, built {ago(st.st_mtime)})")
    else:
        _warn("ISO: not built yet")
    print()

def cmd_setup(only: Optional[str] = None):
    _header()
    targets = [r for r in REPOS if not only or r.name.lower() == only.lower()]
    if only and not targets:
        _die(f"Unknown repo '{only}'. Choose: {', '.join(r.name for r in REPOS)}")
    _step("Fetching external repositories")
    total = ok_count = 0
    for r in targets:
        total += 1
        if _present(r):
            _ok(f"{r.name} — already present")
            ok_count += 1
            continue
        dest_abs = os.path.join(ROOT, r.dest)
        if r.dest == os.path.join("acpi", "AML", "uACPI"):
            rc = _run_live(
                ["git", "-C", os.path.join(ROOT, "acpi"),
                 "submodule", "update", "--init", "--depth", "1", "AML/uACPI"],
                "git submodule update AML/uACPI",
            )
            if rc != 0:
                if os.path.isdir(dest_abs) and not os.listdir(dest_abs):
                    os.rmdir(dest_abs)
                rc = _run_git_clone(r.url, dest_abs, r.name)
        else:
            rc = _run_git_clone(r.url, dest_abs, r.name)
        if rc != 0:
            _err(f"Failed to clone {r.name}")
        else:
            _ok(f"{r.name} → {r.dest}/")
            ok_count += 1
    print()
    (_ok if ok_count == total else _warn)(f"Done — {ok_count}/{total} repos ready")
    print()

def cmd_setup_pick():
    items = []
    for n, r in enumerate(REPOS, 1):
        p = _present(r)
        items.append(MenuItem(str(n), r.name, f"{r.desc}  ({'present' if p else 'missing'})",
                              (lambda name=r.name: cmd_setup(name)),
                              icon="✓" if p else "✗"))
    items.append(MenuItem("b", "Back", "", None, icon="←"))
    _menu_loop("Fetch a single repository", items, C.BLUE)

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
    missing = [r.name for r in REPOS if not _present(r)]
    if missing:
        _warn(f"Missing deps: {', '.join(missing)} — run setup if the build fails")
    _step(title)
    rc = _make_live(title, *args)
    print()
    if rc == 0:
        _ok("Build succeeded!")
        if target in ("all", "iso") and os.path.isfile(ISO):
            _ok(f"ISO: {ISO}  ({os.path.getsize(ISO) / 1_048_576:.1f} MB)")
    else:
        _err(f"Build failed (exit {rc})")
    print()
    return rc

def cmd_build_pick():
    items = []
    for n, (name, (_, title)) in enumerate(TARGETS.items(), 1):
        items.append(MenuItem(str(n), name, title, (lambda t=name: cmd_build(t)), icon="⚒"))
    items.append(MenuItem("b", "Back", "", None, icon="←"))
    _menu_loop("Choose build target", items, C.GREEN)

def cmd_clean():
    _header()
    if is_tty() and not _confirm("Remove all build artefacts?"):
        _info("Cancelled")
        return
    _step("Cleaning build artefacts")
    rc = _make_live("make clean", "clean")
    (_ok if rc == 0 else _err)("Done" if rc == 0 else f"Clean failed (exit {rc})")
    print()

LOG_FILE = os.path.join(BIN, "qemu-debug.log")
GPU_LOG_FILE = os.path.join(BIN, "qemu-gpu-debug.log")

def _qemu_ok() -> bool:
    if shutil.which("qemu-system-x86_64"):
        return True
    _err("qemu-system-x86_64 not found in PATH")
    return False

def _ensure_iso() -> bool:
    if os.path.isfile(ISO):
        return True
    _warn("ISO not found — building first…")
    return cmd_build("all") == 0

def _ready() -> bool:
    return _qemu_ok() and _ensure_iso()

def _qemu_base() -> List[str]:
    cmd = ["qemu-system-x86_64", "-cdrom", ISO, "-m", "512M", "-boot", "d"]
    if os.access("/dev/kvm", os.R_OK | os.W_OK):
        cmd.append("-enable-kvm")
    else:
        _warn("/dev/kvm unavailable — running without acceleration (slow)")
    return cmd

def _qemu_logged(label: str, vga: str, serial: str, mon_name: str):
    os.makedirs(BIN, exist_ok=True)
    int_log  = serial.replace(".log", "-internal.log")
    mon_sock = os.path.join(BIN, mon_name)
    try:
        if os.path.exists(mon_sock): os.unlink(mon_sock)
    except OSError:
        pass
    _step(label)
    _info(f"Serial : {serial}")
    _info(f"QEMU   : {int_log}")
    _info(f"Monitor: {mon_sock}")
    _info(f"hang?    socat - UNIX-CONNECT:{mon_sock}   then: info registers / info status")
    print()
    _run_live(
        _qemu_base() + [
            "-vga", vga,
            "-serial", f"file:{serial}",
            "-monitor", f"unix:{mon_sock},server,nowait",
            "-D", int_log,
            "-d", "int,cpu_reset,guest_errors",
        ],
        label,
    )
    if os.path.isfile(serial):
        _ok(f"Serial log: {serial}")

def qemu_graphics():
    _header()
    if not _ready(): return
    _step("QEMU — graphical mode")
    _run_live(_qemu_base() + ["-vga", "std", "-serial", "stdio"], "QEMU (graphics)")

def qemu_nographic():
    _header()
    if not _ready(): return
    _step("QEMU — no graphics")
    _info("Ctrl-A then X exits QEMU\n")
    subprocess.run(_qemu_base() + ["-nographic"])

def qemu_log_console():
    _header()
    if not _ready(): return
    _step("QEMU — graphics + live serial log")
    _run_live(_qemu_base() + ["-vga", "std", "-serial", "stdio"], "QEMU (serial → console)")

def qemu_log_file():
    _header()
    if not _ready(): return
    _qemu_logged("QEMU — graphics + logs → file", "std", LOG_FILE, "qemu-mon.sock")

def qemu_gpu_graphics():
    _header()
    if not _ready(): return
    _step("QEMU — GPU (vmware-svga) graphical mode")
    _run_live(_qemu_base() + ["-vga", "vmware", "-serial", "stdio"], "QEMU GPU (graphics)")

def qemu_gpu_log_file():
    _header()
    if not _ready(): return
    _qemu_logged("QEMU — GPU (vmware-svga) + logs → file", "vmware", GPU_LOG_FILE, "qemu-gpu-mon.sock")

def cmd_debug():
    items = [
        MenuItem("1", "Run with graphics",    "Normal QEMU window, serial → console",         qemu_graphics,    icon="▶"),
        MenuItem("2", "Run without graphics", "No window — serial in terminal (nographic)",   qemu_nographic,   icon="▢"),
        MenuItem("3", "Log → console",        "Graphics + live serial log in the TUI box",     qemu_log_console, icon="≡"),
        MenuItem("4", "Log → file",           "Graphics + serial & QEMU log → bin/*.log",      qemu_log_file,    icon="▤"),
        MenuItem("5", "GPU graphics",         "QEMU -vga vmware, serial → console",           qemu_gpu_graphics, icon="◈"),
        MenuItem("6", "GPU log → file",       "QEMU -vga vmware, logs → bin/qemu-gpu-*.log",   qemu_gpu_log_file, icon="▤"),
        MenuItem("b", "Back",                 "",                                              None,             icon="←"),
    ]
    _menu_loop("Debug / Run options", items, C.PEACH)

def cmd_run():
    qemu_graphics()

def _status_line() -> str:
    have, total = _deps_ready()
    parts = [c(C.GREEN if have == total else C.YELLOW, "●") + c(C.SUB, f" deps {have}/{total}")]
    if os.path.isfile(ISO):
        st = os.stat(ISO)
        parts.append(c(C.GREEN, "●") + c(C.SUB, f" ISO {st.st_size / 1_048_576:.1f} MB · {ago(st.st_mtime)}"))
    else:
        parts.append(c(C.YELLOW, "●") + c(C.SUB, " ISO not built"))
    if tw() >= 60:
        kvm = os.access("/dev/kvm", os.R_OK | os.W_OK)
        parts.append(c(C.GREEN if kvm else C.YELLOW, "●") + c(C.SUB, " KVM" if kvm else " no KVM"))
    return c(C.OVERLAY, "  │  ").join(parts)

def _menu():
    items: List[MenuItem] = [
        MenuItem("1", "Setup / Fetch deps",  "Clone all missing external repositories", lambda: cmd_setup(), icon="↓"),
        MenuItem("2", "Fetch one repo…",     "Pick a single repository to clone",        cmd_setup_pick,     icon="⇣"),
        MenuItem("3", "Build all",           "Build kernel + programs + ISO",            lambda: cmd_build("all"), icon="⚒"),
        MenuItem("4", "Build…",              "Pick a target: kernel, programs, iso, libs, apps", cmd_build_pick, icon="⚙"),
        MenuItem("5", "Run  (QEMU)",         "Launch PureC OS in QEMU",                  cmd_run,            icon="▶"),
        MenuItem("6", "Debug / Run options", "Submenu: graphics / nographic / logs",     cmd_debug,          icon="◈"),
        MenuItem("7", "Dependency status",   "Repos, host tools, ISO info",              cmd_status,         icon="≡"),
        MenuItem("8", "Clean",               "Remove all build artefacts",               cmd_clean,          icon="✕", danger=True),
        MenuItem("q", "Quit",                "",                                         None,               icon="⏻"),
    ]
    _menu_loop("Main menu", items, C.MAUVE, _status_line, on_back_exit=True)

def main():
    args = sys.argv[1:]
    if not args:
        try:
            _menu()
        except KeyboardInterrupt:
            print(C.SHOW_CURSOR)
        return
    cmd = args[0].lower()
    arg = args[1] if len(args) > 1 else None
    if   cmd == "setup":   cmd_setup(arg)
    elif cmd == "build":   sys.exit(cmd_build(arg or "all"))
    elif cmd == "run":     cmd_run()
    elif cmd == "debug":   cmd_debug()
    elif cmd == "clean":   cmd_clean()
    elif cmd == "status":  cmd_status()
    elif cmd in ("-h", "--help", "help"): print(__doc__)
    else:
        print(c(C.RED, f"Unknown command: {cmd}")); print(__doc__); sys.exit(1)

if __name__ == "__main__":
    main()
