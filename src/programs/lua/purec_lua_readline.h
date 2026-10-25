#pragma once
// PureC OS REPL input for the Lua port.
//
// The PureC terminal echoes shell input itself (terminal/window.c prints
// every GUI key event), but a child process reading raw SYS_GETCHAR gets
// no echo: upstream lua_readline (fputs prompt + fgets) therefore reads
// blindly, and backspace lands in the buffer literally, corrupting the
// line (visible as mysterious syntax errors).
//
// This header redefines lua_readline to purec_lua_readline(), an echoing
// line reader (printable echo + backspace erase, mirroring the shell).
// It is force-included into the Lua translation units via
// -include (see src/programs/Makefile, LUA_OBJECTS PRIVATE_CFLAGS),
// so lua.c takes this branch instead of its fgets default. Only lua.c
// references lua_readline; other TUs just gain an unused declaration.

typedef struct lua_State lua_State;

int purec_lua_readline(lua_State *L, char *b, const char *prompt);

#define lua_readline(L, b, p) (purec_lua_readline((L), (b), (p)))
// Upstream defaults (kept: no history on PureC OS).
#define lua_initreadline(L) ((void)(L))
#define lua_saveline(L, line) ((void)(L), (void)(line))
#define lua_freeline(L, b) ((void)(L), (void)(b))
