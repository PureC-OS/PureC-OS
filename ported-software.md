# Ported Software From PureC OS

This document enumerates the third-party software that has been ported to
PureC OS, the location of its sources in-tree, the upstream origin and version,
the scope of each port, and its integration status. It is intended to give
contributors a factual starting point without requiring reverse-engineering of
the build system.

## Conventions

- All paths are relative to the repository root unless stated otherwise.
- "Kernel" means compiled into the kernel image or as a loadable module
  (`bin/modules/*.ko`); "userspace" means a Ring-3 ELF program or static
  library.
- Shipping status refers to `assets/manifest.txt` (the authoritative install
  manifest consumed by `mk/gen_assets.py`).
- Third-party sources are generally kept in organization-managed forks
  (`github.com/PureC-OS/*`) rather than patched in place, so upstream trees
  stay recordable and diffs remain auditable.

## Overview

| Software | Upstream version | Upstream license | In-tree location | Kernel / Userspace | Ships in rootfs | Status |
|---|---|---|---|---|---|---|
| Lua | 5.4.8 | MIT | `src/programs/lua/` | Userspace | `/usr/bin/lua` (optional) | Working (REPL); `math` lib planned |
| MicroPython | ~1.30-preview | MIT | `src/programs/micropython/` | Userspace | `/usr/bin/micropython` (optional) | Working (REPL); no floats |
| TinyCC (PureC-TCC) | 0.9.28rc | LGPL-2.1 | `tcc/` | Userspace | `/usr/bin/tcc`, `/lib/tcc/*` (optional) | Working (compile & run) |
| SDL2 | 2.33.0 | zlib | `SDL/` | Userspace (lib) | `/usr/bin/sdltest` (optional) | Working; software rendering only |
| SDL2_mixer | n/a — in-house shim | n/a | `src/libsdl2_mixer/` | Userspace (lib) | via consumers | Working (channels + OPL hook) |
| Chocolate Doom | 3.1.1 | GPL-2.0 | `chocolate-doom/`, `src/chocolate/` | Userspace | `/usr/bin/doom` (optional) | Working; Doom only, no netplay |
| Freedoom | 0.13.0 | BSD-3-Clause (assets) | `freedoom/`, `assets/doom/` | Data only | `/usr/share/doom/freedoom1.wad` (optional) | Working (Phase 1) |
| doomgeneric | unspecified | GPL-2.0 | `src/doom/` (+ external clone) | Userspace | Not shipped | Fallback driver; build skipped without sources |
| wolfSSL | 5.9.4 | GPLv2/3 or commercial | `wolfssl/` | Kernel module + lib | `/lib/modules/wolf.*` | Buildable; **no consumer yet** |
| uACPI (via PureC-OS-ACPI) | 6.1.0 (interpreter) | Apache-2.0 / MIT-class | `acpi/AML/uACPI/` | Kernel (`acpi.ko`) | `/lib/modules/acpi.*` | Interpreter integrated; AML executor path WIP |

---

## Lua 5.4

- **Location**: `src/programs/lua/` (complete vendored tree, only such location
  in the repo).
- **Upstream**: Lua 5.4.8, <https://www.lua.org/ftp/lua-5.4.8.tar.gz> (MIT).
  Version is cross-checked in `lua.h`
  (`LUA_VERSION_MAJOR "5"`, `LUA_VERSION_MINOR "4"`, `LUA_VERSION_RELEASE "8"`).
- **Scope**: The full interpreter — Lua core plus the `base`, `package`,
  `coroutine`, `table`, `io`, `os` (partial), `string`, `utf8`, and `debug`
  libraries, including the standalone REPL (`lua.c`).
- **PureC-specific glue**:
  - `linit-purec.c` — custom library initialization.
  - `purec_lua_readline.[ch]` — REPL line-editing shim; the PureC terminal
    echoes input itself, so the stock readline behavior is adapted via a
    `-include` override in `src/programs/Makefile`.
- **Deliberate limitations**: the `math` library (`lmathlib.c`) is excluded in
  v1 because it requires a complete libm. libm has since landed
  (`src/libc/libm.c`, x87 implementation); re-enabling `math` is the next step
  (see v2 roadmap in `src/programs/lua/README.md`).
- **Graceful degradation**: `io.popen`, `io.tmpfile`, `os.execute`, and
  `os.tmpname` return errors instead of performing the operation.
- **Build**: `mk/main.mk` → `programs` → `src/programs/Makefile`
  (`LUA_SOURCES` / `LUA_OBJECTS`). Built with `-DLUA_ANSI`, without
  `LUA_USE_POSIX`/`LUA_USE_DLOPEN`. Linked as a static Ring-3 ELF
  (`crt0.o + libpurec.a`, `linker-userspace.ld`) → `bin/programs/lua`.
- **Ships as**: optional `/usr/bin/lua`.
- **Verification**: REPL exercised live on the PureC Terminal, including
  `os.exit()`.

## MicroPython

- **Location**: `src/programs/micropython/` — vendored MicroPython core
  (`py/`, ~211 files including `vm.c`, `compile.c`, `emitnative.c`),
  `shared/runtime/pyexec.c`, `shared/readline`, and minimal `extmod` headers.
- **Upstream**: MicroPython, roughly **1.30-preview**
  (`py/mpconfig.h`: `MICROPY_VERSION_MAJOR 1`, `MICROPY_VERSION_MINOR 30`;
  version string carries a `-preview` suffix), (MIT).
- **Scope**: REPL plus script-from-argument execution. Port layer is in-house:
  `mpconfigport.h` (256 KiB heap, **no floating point** — same posture as Lua
  v1), `mphalport.[ch]`, `main.c`, own Makefile with the upstream qstr
  generation pipeline preserved.
- **Build**: `mk/main.mk` → `programs` → `make -C src/programs/micropython`
  (cross toolchain `x86_64-elf-gcc`); links `crt0.o + libpurec.a` via
  `linker-userspace.ld`.
- **Ships as**: optional `/usr/bin/micropython`.
- **Verification**: `fact(10) == 3628800` verified on host; boot smoke PASS
  (CHANGELOG V1.2.27).

## TinyCC (PureC-TCC)

- **Location**: `tcc/` — full TinyCC tree (x86_64/i386/ARM/RISC-V backends,
  preprocessor, driver). Fork: `https://github.com/PureC-OS/PureC-TCC.git`
  (branch `main`).
- **Upstream**: TinyCC **0.9.28rc** (recorded in `tcc/VERSION` and pinned in
  `mk/tcc/config.h`, with `TCCDIR "/lib/tcc"`).
- **Scope**: full compiler driver for the **x86_64/PureC target only**
  (`-DTCC_TARGET_X86_64 -DTCC_TARGET_PUREC`). Two upstream-style patches are
  applied at build time and are tracked in `mk/tcc/`:
  - `purec-target.patch` — adds `TARGETOS_PureC`, PureC crt linkage
    (`crt0.o`, `libpurec`, no `crtn.o`).
  - `purec-static.patch` — forces `static_link = 1`, since the PureC loader
    has no INTERP/PLT/GOT support.
  The `libtcc1.a` runtime (7 objects from `tcc/lib`) is built into `/lib/tcc`.
  `tcc/libtcc.c.orig` preserves the pre-patch source for diffing.
- **Build**: compiled by `src/programs/Makefile` into `bin/programs/tcc`;
  fetched as one of the setup-time subprojects (`tcc-fetch` via `purec.py`,
  see root `README.md`).
- **Ships as**: optional `/usr/bin/tcc` plus `/lib/tcc/libtcc1.a` and run-time
  headers under `/lib/tcc/include/`.
- **Verification**: compiles files without errors; compile-and-run from the OS
  shell works (CHANGELOG: "TCC finally works", and the `libtcc1.a` runtime fix
  in V1.2.24/26).

## SDL2

- **Location**: `SDL/` — full SDL2 source tree on branch `purec-sdl2` of
  fork `https://github.com/PureC-OS/SDL.git`.
- **Upstream**: SDL2 **2.33.0** (`SDL/include/SDL_version.h`;
  `configure.ac` sets MICRO=0), zlib license.
- **Scope**: the library, not all of it — build
  (`src/libsdl2/Makefile`) compiles `src/render/software` plus
  dummy audio/haptic/joystick/sensors/loadso/filesystem backends.
  **Software rendering only**; no OpenGL, no threads, single window, no
  resize, no stereo audio (documented in CHANGELOG V1.2.28).
- **New PureC backends** (added in-tree):
  - `SDL/src/video/purec/SDL_purecvideo.[ch]` — an SDL window maps to a
    PureGUI window; surfaces are presented via `pg_window_blit` on syscall
    `SYS_FB_BLIT` (322).
  - `SDL/src/audio/purec/SDL_purecaudio.[ch]` — output over `pa_stream`,
    22050 Hz mono 16-bit.
  - `SDL/src/timer/purec/SDL_systimer.c` — purec timer backend.
  - `SDL_config_purec.h` integration header; `SDL_DYNAMIC_API` disabled.
- **Build**: `mk/main.mk` target `sdl2` → `make -C src/libsdl2`; the target is
  skipped automatically if `SDL/src/SDL.c` is absent.
- **Outputs**: `bin/lib/libSDL2.a` and a test program (`src/libsdl2/sdltest/`).
- **Ships as**: optional `/usr/bin/sdltest`.

## SDL2_mixer (compatibility shim)

- **Location**: `src/libsdl2_mixer/` (`mixer.c`, `SDL_mixer.h`, `Makefile`).
- **Origin**: **not** an upstream port. This is an in-house compatibility
  layer (~25 API functions) providing sound channels with panning,
  post-processing hooks, and a music hook wired to the built-in OPL emulator.
- **Not supported**: MIDI/Ogg/MP3 file playback (no upstream SDL_mixer code is
  vendored).
- **Outputs**: `bin/lib/libSDL2_mixer.a`; consumed by the chocolate-doom link.

## Chocolate Doom

- **Location**: fork checkout at `chocolate-doom/`
  (`https://github.com/PureC-OS/chocolate-doom.git`, branch `master`); port
  build hooks in `src/chocolate/` (`Makefile` + generated `config.h`).
- **Upstream**: Chocolate Doom **3.1.1** (`configure.ac`), GPL-2.0.
- **Scope**: the **Doom** game only. `CHOC_SOURCES` in `src/chocolate/Makefile`
  selects `src/doom/*.c` plus the system/textscreen/opl/pcsound layers
  (textscreen minus `txt_fileselect.c`); the Heretic/Hexen/Strife ports are
  not built.
- **Code changes**: none, except a single default — `fullscreen = false`
  (800×600 window) per `src/chocolate/config.h`. Networking is disabled via
  `-DDISABLE_SDL2NET=1`.
- **Build**: `mk/main.mk` `programs` → `make -C src/chocolate` (depends on
  `sdl2`); links `libSDL2_mixer.a libSDL2.a libpureaudio.a libpuregui.a
  libpurec.a` → `bin/programs/chocolate-doom`. Skipped if the fork checkout or
  `libSDL2.a` is missing.
- **Ships as**: optional `/usr/bin/doom`. Game data comes from Freedoom
  (below).
- **Verification**: tested on VirtualBox; the game starts and is playable
  (CHANGELOG V1.2.28).

## Freedoom

- **Location**: fork checkout at `freedoom/`
  (`https://github.com/PureC-OS/freedoom.git`); the shipped artifact is the
  prebuilt WAD at `assets/doom/freedoom1.wad`.
- **Upstream**: Freedoom **0.13.0** (recorded in `freedoom/NEWS.adoc`),
  BSD-3-Clause. Freedoom is **game data, not a program** — levels, artwork,
  sounds, and music usable as a libre IWAD for Doom-compatible engines.
- **Ships as**: optional `/usr/share/doom/freedoom1.wad` (Phase 1, ~28.8 MB).
- **Provenance note**: the WAD under `assets/doom/` is not regenerated by any
  tracked build script; it is assumed to be built from the `freedoom/` tree
  out-of-band. Contributors updating the WAD should state the source revision
  in the commit message.

## doomgeneric (fallback driver)

- **Location**: port layer at `src/doom/` — `doomgeneric_purec.c` (video via a
  PureGUI window + key queue; default IWAD `/usr/share/doom/doom1.wad`),
  `i_purecsound.c`, `Makefile`, `include/` (only `SDL_mixer.h`).
- **Upstream**: [doomgeneric](https://github.com/ozkl/doomgeneric), expected
  as a clone at repo root `doomgeneric/` (recorded in `src/doom/Makefile`).
- **Status**: **built but not shipped** (`mk/main.mk` target `doomgeneric` →
  `bin/programs/doomgeneric`; absent from the manifest). Its role is to serve
  as the SDL-independent fallback Doom path (CHANGELOG V1.2.28). The build
  target currently skips with "doomgeneric sources not found" — the clone is
  **not** fetched by `purec.py` at the time of writing; confirm the intended
  source of the checkout before building this target.

## wolfSSL

- **Location**: `wolfssl/` — full upstream tree via fork
  `https://github.com/PureC-OS/wolfssl.git`.
- **Upstream**: wolfSSL **5.9.4** (`wolfssl/wolfssl/version.h`), dual
  GPLv2/GPLv3 or commercial licensing.
- **Scope**: "phase 1" wolfCrypt subset with a dedicated PureC `Makefile`
  replacing the autotools flow. Shared sources include aes, chacha,
  chacha20_poly1305, poly1305, cmac, hash, hmac, kdf, sha/sha256/sha512.
  The userspace build additionally compiles asn/ecc/rsa/signature/sp_int.
- **PureC glue**: `wolfssl/purec/` — `user_settings.h` (and
  `user_settings_host.h`) supplied via `-DWOLFSSL_USER_SETTINGS`,
  `memory_{host,kernel,userspace}.c`, `seed_purec.c`, `snprintf_purec.c`.
- **Build**: `mk/main.mk` `kernel` target runs `make -C wolfssl module lib`
  (dedicated `make wolf` target; host self-test via `make test-wolfcrypt`).
- **Outputs**: `bin/lib/libwolf.a` (Ring 3) and the kernel-module triple
  `bin/modules/wolf.{elf,ko,so}`. The module is staged into `/lib/modules`.
- **Status**: builds cleanly, but **nothing in the kernel or userland consumes
  it yet** (`src/` contains no references to wolf). Treat the integration as
  WIP; potential consumers include TLS enablement in the network stack and
  stronger cryptography alongside libpurecrypt.

## ACPI (PureC-OS-ACPI with the uACPI Interpreter)

- **Location**: `acpi/` — subproject `https://github.com/PureC-OS/PureC-OS-ACPI.git`;
  the third-party AML interpreter is nested at `acpi/AML/uACPI/`.
- **Upstream**: [uACPI](https://github.com/uACPI/uACPI), tag **6.1.0**
  (fetched by `purec.py` as "PureC-OS-UACPI"; presence check
  `acpi/AML/uACPI/source`).
- **Scope**: RSDP/XSDT/FADT parsing, `SCI_EN`, a minimal `_S5` parser with S5
  shutdown and reboot (ResetReg/KBC/CF9/triple-fault), MADT parsing, and
  battery information (`_BIF/_BIX/_BST`).
- **Limitation**: dynamic object evaluation still returns unknown values —
  the full AML executor path through uACPI is the declared next step.
- **Integration**: compiled into the kernel (`src/kernel/Makefile`, kernel
  CFLAGS) and as the relocatable module `acpi.{elf,ko}`; initialized from
  `src/drivers/power/power.c` (`power_init()`).
- **Ships as**: boot module (`/lib/modules/acpi.*`).
- **Status**: working for static tables; AML dynamic evaluation WIP.

---

## Build and Fetch Map

The top-level `Makefile` includes `mk/main.mk`. Relevant targets:

| Package | Target | Skipped when |
|---|---|---|
| Lua, MicroPython, TCC, Chocolate Doom | `make programs` | Sources missing |
| SDL2 | `make sdl2` | `SDL/src/SDL.c` absent |
| doomgeneric | `make doomgeneric` | `doomgeneric/` (external clone) absent |
| wolfSSL | `make wolf` / `kernel` | `wolfssl/` absent |
| ACPI | `kernel` | Subproject absent |
| Host tests | `test-wolfcrypt`, `test-accounts`, `test-hdrbg` | — |

Sources fetched by `./purec.py setup` (see `purec.py`, `REPOS` list):
wolfssl, libxcrypt, ACPI, uACPI, PureC-TCC, Userspace, Notepad.

**SDL, Chocolate Doom, Freedoom, and doomgeneric are not currently in the
`purec.py` fetch list** — they are organization forks expected as plain git
checkouts, and the loops are skipped silently when the directories are
missing. Version pinning for these is read from in-tree files and may drift
as the forks advance; contributors touching them should record the fork
revision explicitly.

## Shipping

Optional entries in `assets/manifest.txt` (consumed by `mk/gen_assets.py` and
the installer in `src/fs/fat/fat32.c`):

- `/usr/bin/lua` — Lua 5.4.8 interpreter
- `/usr/bin/micropython` — MicroPython REPL
- `/usr/bin/tcc` — TinyCC driver; `/lib/tcc/libtcc1.a` + headers
- `/usr/bin/sdltest` — SDL2 test program
- `/usr/bin/doom` — Chocolate Doom; `/usr/share/doom/freedoom1.wad`
- `/usr/bin/pl` — PureL (`lang/`, in-house Rust scripting language, upstream
  Rust `no_std` target — see below)
- `/usr/bin/notepad` — PureC-notepad-OS (submodule, in-house C++ editor — see
  below)
- `/lib/modules/` — `crypt.*`, `acpi.*`, `wolf.*` kernel modules
- `/etc/` — passwd/group/hostname/motd/os-release/shells/fstab seed data
  (`rootfs/etc/`)

## In-House Projects Often Collated with the Ports

For completeness, the following are **not** third-party ports but are located
adjacent to them and frequently mistaken for them:

- `lang/` — **PureL**, an in-house scripting language implemented in Rust
  (`no_std`, target `x86_64-unknown-none`, linked against
  `userspace/crates/purec`); ships as `/usr/bin/pl`.
- `libxcrypt/` — **libpurecrypt**, an in-house freestanding cryptography
  library (despite the directory name, this is **not** upstream libxcrypt).
  Implements SHA-512 and `$6$` (SHA512-crypt) password hashing,
  byte-identical to glibc `crypt("$6$")` — sources `src/sha512.c`,
  `src/crypt_sha512.c`, headers `include/sha512.h`, `include/purecrypt.h`,
  host tests in `test/`. Compiled **into the kernel image**
  (`src/kernel/Makefile`, with `-mgeneral-regs-only`) and additionally as the
  relocatable module `crypt.{elf,ko}` plus the static library
  `bin/lib/libpurecrypt.a`. Consumers: `/etc` passwd hashing via
  `src/programs/accounts`, and the entropy driver `src/drivers/random`
  (HDRBG test). Ships as `/lib/modules/crypt.*`.
- `purec-notepad-os/` — in-house C++20 Ring-3 GUI text editor (own syscall
  layer and mini GUI toolkit, no third-party code); ships as
  `/usr/bin/notepad`.
- `src/libsdl2_mixer/` — the in-house SDL2_mixer shim described above.
- `bb/`, `demo/`, `db/`, `rootfs/` are build/config assets (Limine boot
  fragment template, README screenshots, build database, `/etc` seed), not
  software.

Historical status notes for each port live in `CHANGELOG.md` (see the
V1.2.27 and V1.2.28 sections) and in `src/programs/lua/README.md`.
