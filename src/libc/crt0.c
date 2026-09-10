// PureC hosted crt0: entry point for standard C programs (TCC port and
// friends). NOT linked into libpurec.a on purpose: every native program
// defines its own _start, and an archived _start could shadow or clash.
// Ported programs link bin/lib/crt0.o explicitly and write a normal
// int main(int argc, char *argv[]).
//
// Command line comes from pc_get_command_line (256 bytes max); it is
// split on ASCII whitespace with double-quote grouping. Environment is
// published via environ[] (static storage, well-known variables).

#include "include/purec.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRT0_MAX_ARGS 64
#define CRT0_CMDLINE 256
#define CRT0_ENV_COUNT 16
#define CRT0_ENV_NAME 32
#define CRT0_ENV_VALUE 128

int main(int argc, char *argv[]);

char **environ;

static char crt0_cmdline[CRT0_CMDLINE];
static char *crt0_argv[CRT0_MAX_ARGS];
static char crt0_program[64];
static char crt0_env_storage[CRT0_ENV_COUNT][CRT0_ENV_NAME + CRT0_ENV_VALUE + 2];
static char *crt0_environ[CRT0_ENV_COUNT + 1];

static void crt0_build_environ(void) {
    static const char *names[] = {
        "HOME", "PWD", "USER", "SHELL", "PATH"
    };
    char value[CRT0_ENV_VALUE];
    uint32_t count = 0;
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (count >= CRT0_ENV_COUNT) break;
        if (pc_getenv(names[i], value, sizeof(value)) < 0) continue;
        uint32_t n = 0;
        while (names[i][n] && n + 1 < CRT0_ENV_NAME) {
            crt0_env_storage[count][n] = names[i][n];
            n++;
        }
        crt0_env_storage[count][n++] = '=';
        uint32_t v = 0;
        while (value[v] && n + 1 < sizeof(crt0_env_storage[count])) {
            crt0_env_storage[count][n++] = value[v++];
        }
        crt0_env_storage[count][n] = '\0';
        crt0_environ[count] = crt0_env_storage[count];
        count++;
    }
    crt0_environ[count] = 0;
    environ = crt0_environ;
}

void _start(void) {
    crt0_build_environ();
    int argc = 0;
    // argv[0] is the program name when the kernel provides it.
    if (pc_get_process_name(crt0_program, sizeof(crt0_program)) >= 0
        && crt0_program[0]) {
        crt0_argv[argc++] = crt0_program;
    }
    int32_t length = pc_get_command_line(crt0_cmdline, sizeof(crt0_cmdline));
    if (length > 0) {
        if (length > (int32_t)sizeof(crt0_cmdline) - 1)
            length = (int32_t)sizeof(crt0_cmdline) - 1;
        crt0_cmdline[length] = '\0';
        uint32_t i = 0;
        while (crt0_cmdline[i] && argc + 1 < CRT0_MAX_ARGS) {
            while (crt0_cmdline[i] == ' ' || crt0_cmdline[i] == '\t'
                   || crt0_cmdline[i] == '\n' || crt0_cmdline[i] == '\r') i++;
            if (!crt0_cmdline[i]) break;
            bool quoted = false;
            if (crt0_cmdline[i] == '"') { quoted = true; i++; }
            crt0_argv[argc++] = &crt0_cmdline[i];
            while (crt0_cmdline[i]
                   && ((quoted && crt0_cmdline[i] != '"')
                       || (!quoted && crt0_cmdline[i] != ' ' && crt0_cmdline[i] != '\t'
                           && crt0_cmdline[i] != '\n' && crt0_cmdline[i] != '\r'))) i++;
            if (crt0_cmdline[i]) crt0_cmdline[i++] = '\0';
        }
    }
    crt0_argv[argc] = 0;
    int status = main(argc, crt0_argv);
    pc_exit(status);
}
