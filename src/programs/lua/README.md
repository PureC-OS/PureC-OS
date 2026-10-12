# Lua 5.4 for PureC OS (port)

Upstream: Lua 5.4.8 (https://www.lua.org/ftp/lua-5.4.8.tar.gz), MIT license.
Vendored under `src/programs/lua/` (core + libs + `lua.c` REPL).

## v1 status: REPL without `math`

Core, `base`, `package`, `coroutine`, `table`, `io`, `os` (partial),
`string`, `utf8`, `debug` work. `math` library is excluded: it needs
full libm (`sin/cos/log/exp/...`), which lands with soft-fp later.

What the v1 libc already covers (added for this port):
- `locale.h` (C locale, `decimal_point "."`), `sig_atomic_t`,
  `SEEK_*`, `_IONBF/_IOLBF/_IOFBF` in `stdio.h`
- `freopen/tmpfile/tmpnam` (stubs: no temp files yet), `system()`
  (no shell: `NULL` probe → 0, else −1), `strcoll` (= `strcmp`),
  `difftime`, `floor/ceil/trunc/fmod/modf/frexp/sqrt/pow`
- `pow` is integer-exact (`2^10` ok) + `x^0.5` via `sqrt`;
  general fractional `pow` returns NaN (documented limitation)

Graceful degradations (same as Lua's ISO C fallbacks):
- `io.popen` → "popen not supported", `io.tmpfile` → nil+error,
  `os.execute` → error, `os.tmpname` → error

## Build

`make programs` builds `bin/programs/lua` (hosted `main()` via `crt0.o`,
`-DLUA_ANSI`, no `LUA_USE_POSIX`/`LUA_USE_DLOPEN`).

## Run

In PureC OS terminal: `/bin/program/lua` (REPL), or
`/bin/program/lua script.lua`. `os.exit`, `print`, files via `io`,
`os.time/clock/date`, `getenv` work.

## REPL input echo (PureC quirk)

The PureC terminal echoes shell input itself, but a child reading raw
`SYS_GETCHAR` gets no echo, and backspace would land in the buffer
literally. So the REPL does not use upstream `fgets`-based
`lua_readline`; instead `purec_lua_readline.[ch]` (wired via `-include`,
see `src/programs/Makefile`) provide an echoing line reader that
mirrors the shell: printable echo, backspace erase, CR/LF handling,
`LUA_MAXINPUT` cap. Exit with `os.exit()` or by closing the terminal
(`SYS_GETCHAR` never returns EOF, so there is no Ctrl-D).

## v2 roadmap

- Full `lmathlib` (soft-fp `sin/cos/tan/asin/acos/atan/log/exp/...`)
- General fractional `pow` via `exp/log`
- `tmpfile/tmpnam` on VFS, `os.execute` via spawner (if ever)
