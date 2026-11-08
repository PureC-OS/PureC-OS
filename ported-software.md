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
| Vim | 9.2 (patch 1252) | Vim license | `vim/` (external checkout), `mk/vim/` | Userspace | `/usr/bin/vim`, `/etc/vimrc` (optional) | Builds and links (tiny features); **not run on PureC OS yet** |
| MicroPython | ~1.30-preview | MIT | `src/programs/micropython/` | Userspace | `/usr/bin/micropython` (optional) | Working (REPL); no floats |
| TinyCC (PureC-TCC) | 0.9.28rc | LGPL-2.1 | `tcc/` | Userspace | `/usr/bin/tcc`, `/lib/tcc/*` (optional) | Working (compile & run); self-hosting: rebuilds itself inside the OS from sources fetched with curl |
| SDL2 | 2.33.0 | zlib | `SDL/` | Userspace (lib) | `/usr/bin/sdltest` (optional) | Working; software rendering only |
| SDL2_mixer | n/a — in-house shim | n/a | `src/libsdl2_mixer/` | Userspace (lib) | via consumers | Working (channels + OPL hook) |
| Chocolate Doom | 3.1.1 | GPL-2.0 | `chocolate-doom/`, `src/chocolate/` | Userspace | `/usr/bin/doom` (optional) | Working; Doom only, no netplay |
| GNU Make | 4.4.1 | GPL-3.0-or-later | `gnumake/`, `src/make/` | Userspace | `/usr/bin/make` (optional) | Ported and starts (`make --version`); Makefile runs not yet verified; `-j1` only |
| Freedoom | 0.13.0 | BSD-3-Clause (assets) | `freedoom/`, `assets/doom/` | Data only | `/usr/share/doom/freedoom1.wad` (optional) | Working (Phase 1) |
| doomgeneric | unspecified | GPL-2.0 | `src/doom/` (+ external clone) | Userspace | Not shipped | Fallback driver; build skipped without sources |
| wolfSSL | 5.9.4 | GPLv2/3 or commercial | `wolfssl/` | Kernel module + lib | `/lib/modules/wolf.*` | Userspace TLS in use by `httpsget`, curl and NetSurf (verified in the VM); kernel module buildable, no kernel consumer |
| curl | 8.23.1-DEV | curl (MIT-style) | `curl/`, `mk/curl/` | Userspace | `/usr/bin/curl` (optional) | Working: HTTP and HTTPS (wolfSSL), gzip; IPv4 only |
| NetSurf | submodule `netsurf/` (no release tag) | GPL-2.0 | `netsurf/`, `mk/netsurf/` | Userspace | `/usr/bin/netsurf` + `/usr/local/share/netsurf/*` (optional) | Working: framebuffer frontend in a PureC window, HTTPS, images, Cyrillic; no JavaScript, no SVG |
| NetSurf libraries (libcss, libdom, libhubbub, libparserutils, libwapcaplet, libnsgif, libnsbmp, libnsutils, libnslog, libnsfb, libutf8proc) | pinned in `mk/netsurf/versions.txt` | MIT | `netsurf-libs/` (fetched, git-ignored) | Userspace (static libs) | linked into `/usr/bin/netsurf` | Working (built by their own buildsystem into `bin/ns-stage`) |
| zlib | 1.3.1 | zlib | `netsurf-libs/zlib/` | Userspace (static lib) | linked into curl and NetSurf | Working |
| libpng | 1.6.47 | libpng | `netsurf-libs/libpng/` | Userspace (static lib) | linked into NetSurf | Working |
| libjpeg-turbo | 2.1.5.1 (no SIMD) | BSD / IJG / zlib | `netsurf-libs/libjpeg-turbo/`, `mk/netsurf/jpeg/` | Userspace (static lib) | linked into NetSurf | Working |
| expat | 2.6.4 | MIT | `netsurf-libs/libexpat/`, `mk/netsurf/expat/` | Userspace (static lib) | linked into NetSurf (libdom XML binding) | Working |
| GNU Unifont (glyph data only) | 16.0.01 | GPL-2.0+ with font exception / OFL-1.1 | `netsurf-libs/unifont.hex` | Data (compiled into NetSurf) | inside `/usr/bin/netsurf` | Cyrillic block U+0400–U+04FF of the NetSurf built-in font |
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

## Vim

- **Location**: sources are an external checkout in `vim/` (not patched);
  everything PureC-specific lives in `mk/vim/`: `auto/config.h` (hand-written
  replacement for `configure`), `auto/osdef.h` (empty), `auto/pathdef.c`,
  `purec_compat.c`, plus `rootfs/etc/vimrc` (system vimrc).
- **Scope**: `FEAT_TINY` with the builtin `xterm` termcap (no terminfo or
  curses library: `HAVE_TGETENT` is not defined). `TERM=xterm` is set by
  `terminal` and `sh`.
- **Build**: `src/programs/Makefile` (`VIM_SOURCES`), enabled when
  `vim/src/vim.h` exists; compiled without `-g` (~1.8 MB ELF) → `bin/programs/vim`.
- **Needs from the OS**: ANSI/VT console parser (`src/drivers/display/console_vt.c`),
  kernel tty (`src/kernel/tty/`, `SYS_TTY_READ/IOCTL`, `SYS_POLL`), libc
  `termios`/`ioctl`/`poll`/`select` (`src/libc/tty.c`), `bsearch`, `chmod`, `rmdir`.
- **Verification**: the same sources and `config.h` build and run on a Linux
  host; the output Vim produces in a pty was fed through `console_vt` and
  matched against the `pyte` terminal emulator on 8 editing scenarios
  (scrolling, paging, line insert/delete, search, visual mode, undo). The binary
  has **not** been run on PureC OS yet.
- **Known gaps**: no alternate screen (`?1049`), no `SIGWINCH`, no Ctrl-C
  signal in raw mode, no `:!` / shell integration verified, no syntax
  highlighting or `:help` (tiny build, no runtime files).

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
- **Self-hosting (verified in the VM)**: curl downloaded 21 TinyCC source files
  (1,208,412 bytes) from the `PureC-OS/PureC-TCC` fork over HTTPS into
  `/work/tcc`; the OS's own `tcc` built them into `/work/tcc2`
  (`tcc version 0.9.28rc`); `tcc2` compiled `ex1.c` ("Hello World") and
  rebuilt `tcc.c` into `/work/tcc3` of the same size. The command is
  `tcc -DTCC_TARGET_X86_64 -DTCC_TARGET_PUREC -I/work/tcc -o /work/tcc2 /work/tcc/tcc.c`
  with `mk/tcc/config.h` next to the sources. Not yet done inside the OS:
  building `libtcc1.a` and `/lib/tcc/include` from the downloaded sources
  (they still come from the host build), and driving the build with `make`.
  Requirements this exposed in the hosted libc headers: `stdint.h` must exist,
  and headers included through `../` need classic include guards (TinyCC
  compares `#pragma once` files by path string).

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
- **Userspace TLS build** (`WOLF_PUREC_TLS` block in `user_settings.h`, not
  used by the kernel module): TLS 1.2/1.3 client (`ssl.c`, `internal.c`,
  `keys.c`, `tls.c`, `tls13.c`, `wolfio.c`), ECDHE and RSA, AES-GCM,
  ChaCha20-Poly1305, SNI, ALPN, plus what curl's wolfSSL backend needs:
  `OPENSSL_EXTRA`, `HAVE_EX_DATA`, session cache, file system access (CA
  bundle loading), built-in socket I/O, `logging.c` and `pkcs12.c`.
  `wc_port.c` is built with renamed `gmtime`/`gmtime_r` so the hosted libc
  versions are used.
- **Status**: the kernel module still builds but has no consumer. The
  userspace library is used by `/usr/bin/httpsget` (the first consumer, which
  installs its own I/O callbacks), by curl (TLS backend) and, through curl, by
  NetSurf. Verified in the VM: TLS 1.3 handshakes with certificate chain
  verification against `/etc/ssl/cert.pem` (118 root certificates).

## curl

- **Location**: `curl/` (git submodule, `https://curl.se/` tree); PureC build
  configuration in `mk/curl/` (`curl_config.h`, `list.mk`).
- **Upstream**: curl **8.23.1-DEV** (`curl/include/curl/curlver.h`), curl
  license (MIT-style).
- **Scope**: the full `curl` tool and `libcurl` for HTTP, HTTPS and `file:`
  with proxy support, cookies, MIME/form API, IPv4 only. TLS backend:
  wolfSSL (`USE_WOLFSSL`), CA bundle `/etc/ssl/cert.pem`. FTP, SMTP, IMAP,
  POP3, LDAP, DICT, GOPHER, TELNET, TFTP, RTSP, SMB, MQTT, NTLM, Kerberos and
  HTTP/3 are disabled in `curl_config.h`. gzip decoding uses zlib
  (`-DHAVE_LIBZ`) when `netsurf-libs/zlib` has been fetched.
- **Build**: `src/programs/Makefile` builds `bin/programs/curl` (skipped when
  `curl/lib/curl_setup.h` is absent). Source lists come from curl's own
  `Makefile.inc` through `mk/curl/list.mk`, minus the Windows/OpenSSL/other-
  backend files. The library objects are also archived as
  `bin/ns-stage/lib/libcurl.a` for NetSurf (`make -C mk/netsurf curl`).
- **Ships as**: optional `/usr/bin/curl` (manifest); the CA bundle is
  `/etc/ssl/cert.pem`.
- **Verification (VM)**: `curl https://google.com`, `curl -I` and plain HTTP
  work; curl was used inside the OS to download the TinyCC sources (see
  TinyCC). Name resolution goes through `getaddrinfo` (one IPv4 address) and the
  `SYS_DNS_RESOLVE` syscall; the threaded resolver is not usable because
  `pthread_create` is a stub returning `EAGAIN`.
- **Configuration pitfalls**: do not define `CURL_CA_PATH` (curl treats it as
  a directory and certificate loading fails with error 77), do not define
  `CURL_DISABLE_PROXY` (NetSurf calls `CURLOPT_PROXY` with NULL on every
  request and the request fails before it is sent), and define feature macros
  only when enabled (curl tests them with `#ifdef`).

## NetSurf

- **Location**: `netsurf/` (git submodule, upstream NetSurf tree); PureC glue
  in `mk/netsurf/`.
- **Upstream**: NetSurf from the upstream git head (no release tag); GPL-2.0.
- **Scope**: the **framebuffer** frontend (internal bitmap font, no FreeType),
  JavaScript (Duktape) disabled, libcurl as the fetcher, PNG/JPEG/GIF/BMP
  decoders, libdom with the expat XML binding. SVG, WebP, JPEG XL, PDF export
  and libnspsl are off.
- **PureC glue** (`mk/netsurf/`):
  - `libnsfb/purec.c` — a new `libnsfb` surface that draws into a PureC GUI
    window (`pg_window_center`, `pg_window_blit`, `pg_window_poll_event`) and
    translates keys, mouse, wheel, close and window resize into `libnsfb`
    events. `patch-libnsfb.sh` copies it into `netsurf-libs/libnsfb` and adds
    `NSFB_SURFACE_PUREC`.
  - `netsurf.ld`, `ns_start.c` — linker script with `__init_array_*` and a
    `main` wrapper that runs constructors (`libnsfb` registers surfaces from
    constructors); `libnsfb` is linked with `--whole-archive`.
  - `gen-glyph-data.py` — extends the built-in NetSurf font with the Cyrillic
    block from GNU Unifont without modifying the submodule.
  - `Choices` — default options (`ca_bundle:/etc/ssl/cert.pem`, 900x650
    window).
- **Build**: `./mk/netsurf/fetch.sh` once (downloads the libraries into
  `netsurf-libs/`), then `make -C mk/netsurf browser`. This runs, in order:
  `deps` (zlib, libpng, libjpeg, expat, libcurl into `bin/ns-stage`),
  `build-libs.sh` (NetSurf libraries through their buildsystem),
  `build-netsurf.sh` (NetSurf core with its own Makefile and our
  cross-compiler; its final link step is not used) and a manual `ld` link into
  `bin/programs/netsurf` (about 4.5 MB).
- **Ships as**: optional `/usr/bin/netsurf` plus `/usr/local/share/netsurf/`
  (`Messages`, `default.css`, `internal.css`, `quirks.css`, `adblock.css`,
  `welcome.html`, `credits.html`, `licence.html`, `netsurf.png`,
  `favicon.png`, `Choices`). Resource files reach the system through the
  installer, so a changed manifest needs a reinstall.
- **Verification (VM)**: HTTPS pages load; `en.wikipedia.org/wiki/Main_Page`
  in about 1.7 s with a JPEG, tables, colors and links; Cyrillic text renders
  (regular, bold, italic); the window can be maximized and the page re-lays
  out. Exit with `Esc` while the window has focus.
- **Known limitations**: no JavaScript; no SVG (`libsvgtiny` is fetched but
  not built), so logos and icons on many sites are empty boxes; glyphs only
  for Latin and Cyrillic (other scripts show boxes; CJK would need FreeType);
  `libcss` has no CSS custom properties (`var(--x)`), so sites built on them
  such as GitHub lose their layout; `regcomp`/`regexec` in the libc are
  stubs (only "save page complete" uses regex). A URL must be typed after
  clicking the address bar (keys go to the focused widget); `netsurf <url>`
  from a shell avoids that.
- **Performance note**: the first run was unusable because the old hosted
  `malloc`/`free` were O(n) and O(n^2); the allocator in `src/libc/malloc.c`
  replaced them (this speeds up every program).

## NetSurf support libraries

- **Location**: sources are fetched, not committed: `mk/netsurf/fetch.sh`
  clones them into `netsurf-libs/` (listed in `.gitignore`); revisions are
  recorded in `mk/netsurf/versions.txt`. Build outputs go to `bin/ns-stage`
  (`lib/`, `include/`, `lib/pkgconfig/`), the same layout the NetSurf build
  expects from an installed prefix.
- **NetSurf's own libraries** (git://git.netsurf-browser.org, MIT): libwapcaplet,
  libparserutils (built with `-DWITHOUT_ICONV_FILTER`), libhubbub, libcss,
  libdom, libnsgif, libnsbmp, libnsutils, libnslog, libnsfb, libutf8proc,
  built by `mk/netsurf/build-libs.sh` with their `netsurf-buildsystem`.
  `libnspsl` and `libsvgtiny` are fetched but not built.
- **Third-party libraries** built by `mk/netsurf/Makefile`: zlib 1.3.1,
  libpng 1.6.47 (`PNG_INTEL_SSE_OPT=0`, prebuilt `pnglibconf.h`),
  libjpeg-turbo **2.1.5.1** (single 8-bit precision, no SIMD; the 3.x series
  builds 8/12/16-bit variants through CMake and was not used), expat 2.6.4
  (`mk/netsurf/expat/expat_config.h`, configured to read entropy from
  `/dev/urandom`).
- **libc support added for these ports**: `iconv`, `pread`/`pwrite`,
  `scandir`/`alphasort`, `dirfd`, `fstatat`, `unlinkat`, `strchrnul`, regex
  stubs and the headers `iconv.h`, `stdint.h`, `limits.h`, `endian.h`,
  `regex.h`, `sys/param.h`, `sys/un.h` (see `CHANGELOG.md`).

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

## GNU Make

- **Location**: unpacked upstream tarball at `gnumake/` (not tracked by git,
  like the other third-party checkouts); port build hooks in `src/make/`
  (`Makefile`, hand-written `config.h`, `shim.c`).
- **Upstream**: GNU Make **4.4.1**, GPL-3.0-or-later.
- **Scope**: the full `src/*.c` tree except the platform ports (Amiga, DOS,
  VMS, Windows). `glob` and `fnmatch` come from make's bundled `lib/`;
  `find_in_given_path` is provided by `src/make/shim.c`. Disabled: jobserver
  (only `-j1` works until `pselect` exists), `load`, archive (`.a`) members,
  and output sync.
- **Code changes**: none in the upstream sources. Everything lives in
  `src/make/config.h` and `src/make/shim.c`.
- **Process model**: recipes run through `posix_spawn` (`USE_POSIX_SPAWN`)
  and the PureC `sh` (`/usr/bin/sh`, also shipped as `/bin/sh`). This relies on
  the kernel `fork`/`execve`/`waitpid`, pipes, `dup2`, signals and per-process
  working directory added with the process-model rework, plus the libc
  additions in `src/libc/posix.c` (`umask`, `mkstemp`, `getpwnam`, `uname`,
  `getrlimit`, `stpcpy`, `mempcpy`, `asprintf`, `st_ino`/`st_dev`, …).
- **Build**: `mk/main.mk` `programs` → `make -C src/make`; links `libpurefs.a
  libpurec.a` → `bin/programs/make`. Skipped if `gnumake/src/main.c` is
  missing. Standalone target: `make gnumake`.
- **Ships as**: optional `/usr/bin/make`.
- **Verification**: tested on VirtualBox; `make --version` works and make
  parses Makefiles (it reports `missing separator` on a recipe indented with
  spaces). Running a full recipe is not yet confirmed. Recipe lines must start
  with a Tab (the bundled `nano` does not insert one; use `printf` or
  `.RECIPEPREFIX`).
- **Limits**: no jobserver or `-j` greater than 1; recipe utilities
  (`rm -f`, `mkdir -p`, `cp`, `mv`, `touch`, `sed`, `grep`) depend on what the
  PureC system programs support.

## Build and Fetch Map

The top-level `Makefile` includes `mk/main.mk`. Relevant targets:

| Package | Target | Skipped when |
|---|---|---|
| Lua, MicroPython, TCC, Chocolate Doom, GNU Make | `make programs` | Sources missing |
| SDL2 | `make sdl2` | `SDL/src/SDL.c` absent |
| doomgeneric | `make doomgeneric` | `doomgeneric/` (external clone) absent |
| wolfSSL | `make wolf` / `kernel` | `wolfssl/` absent |
| curl | `make programs` (`src/programs/Makefile`) | `curl/lib/curl_setup.h` absent |
| NetSurf and its libraries | `./mk/netsurf/fetch.sh`, then `make -C mk/netsurf browser` | `netsurf-libs/` not fetched (not part of `make all`) |
| ACPI | `kernel` | Subproject absent |
| Host tests | `test-wolfcrypt`, `test-accounts`, `test-hdrbg` | — |

Sources fetched by `./purec.py setup` (see `purec.py`, `REPOS` list):
wolfssl, libxcrypt, ACPI, uACPI, PureC-TCC, Userspace, Notepad.

The NetSurf libraries, zlib, libpng, libjpeg-turbo, expat and the Unifont data
are fetched by `mk/netsurf/fetch.sh` (git:// for the NetSurf libraries,
GitHub for the rest), not by `purec.py`; the clones are git-ignored and their
revisions are recorded in `mk/netsurf/versions.txt`. `curl/` and `netsurf/`
are submodules.

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
- `/usr/bin/curl` — curl tool (HTTP/HTTPS through wolfSSL); `/etc/ssl/cert.pem`
  is the CA bundle it uses
- `/usr/bin/netsurf` — NetSurf browser; `/usr/local/share/netsurf/*` —
  messages, stylesheets, welcome page and the default `Choices`
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
