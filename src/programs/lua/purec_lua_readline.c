// PureC OS echoing line reader for the Lua REPL (lua_readline override).
//
// Behavior mirrors the PureC shell (terminal/window.c):
// - prompt via stdout, flushed immediately (stdout is line-buffered,
//   so every echoed char is flushed too, otherwise nothing appears
//   until Enter);
// - printable ASCII (' '..'~') is stored and echoed;
// - '\r'/'\n' ends the line: a '\n' is stored fgets-style (Lua uses it
//   to detect complete vs. continued statements) and echoed;
// - '\b'/DEL erases the last char from the buffer and from the screen
//   (the console interprets '\b' as erase, same as the shell);
// - other control chars are ignored (arrows arrive via SYS_TRY_GET_SPECIAL,
//   not via getchar, so they never reach here);
// - input is capped at LUA_MAXINPUT-1 chars plus '\n'+'\0'; extras are
//   dropped, never overflowed.
// - SYS_GETCHAR is blocking and never returns EOF on PureC OS, so this
//   always returns 1; a defensive EOF maps to 0 only when nothing was
//   typed (lets a hypothetical EOF close the REPL like Ctrl-D on Unix).
//   Exit the REPL with os.exit() or by closing the terminal window.

#include "lua.h"

#include <stdio.h>

#if !defined(LUA_MAXINPUT)
#define LUA_MAXINPUT 512
#endif

int purec_lua_readline(lua_State *L, char *b, const char *prompt) {
    size_t len = 0;
    (void)L;
    if (!b || LUA_MAXINPUT < 2)
        return 0;
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            if (len == 0)
                return 0;
            b[len] = '\0';
            return 1;
        }
        if (c == '\r' || c == '\n') {
            if (len + 2 > (size_t)LUA_MAXINPUT) {
                b[len] = '\0'; // overfull: no room for '\n', keep the line
                return 1;
            }
            b[len++] = '\n';
            b[len] = '\0';
            putchar('\n');
            fflush(stdout);
            return 1;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                putchar('\b'); // console erases the glyph (gop.c)
                fflush(stdout);
            }
            continue;
        }
        if (c < ' ' || c > '~')
            continue;
        if (len + 2 > (size_t)LUA_MAXINPUT)
            continue; // keep room for '\n' + '\0'
        b[len++] = (char)c;
        putchar(c);
        fflush(stdout);
    }
}
