// PureC hosted crt0: entry point for standard C programs (TCC port and
// friends). NOT linked into libpurec.a on purpose: every native program
// defines its own _start, and an archived _start could shadow or clash.
// Ported programs link bin/lib/crt0.o explicitly and write a normal
// int main(int argc, char *argv[]).
//
// Entry contract: rsp points at a fake return address, followed by argc,
// argv[], NULL, envp[], NULL. Processes created by execve carry the real
// vectors there; processes created by the legacy spawn syscall have
// argc == 0 and fall back to pc_get_command_line (256 bytes max, split
// on whitespace with double-quote grouping) and the kernel environment.

#include "include/purec.h"
#include "include/hosted/stdio.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRT0_MAX_ARGS 64
#define CRT0_CMDLINE 256
#define CRT0_ENV_COUNT 16
#define CRT0_ENV_NAME 32
#define CRT0_ENV_VALUE 128

int main(int argc, char *argv[]);
void exit(int status) __attribute__((noreturn));

extern char **environ;

extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));

/* Identifies this executable to __cxa_atexit (C++ static destructors). */
void *__dso_handle __attribute__((visibility("hidden")));

/* Set once the .init_array constructors have run, so wrappers can skip a second run. */
int __purec_init_array_done;

static void crt0_run_constructors(void) {
    if (__purec_init_array_done)
        return;
    __purec_init_array_done = 1;
    void (**entry)(void) = __init_array_start;
    void (**end)(void) = __init_array_end;
    if (!entry || !end)
        return;
    for (; entry < end; entry++)
        (*entry)();
}

static char crt0_cmdline[CRT0_CMDLINE];
static char *crt0_argv[CRT0_MAX_ARGS];
static char crt0_program[64];
static char crt0_env_storage[CRT0_ENV_COUNT][CRT0_ENV_NAME + CRT0_ENV_VALUE + 2];
static char *crt0_environ[CRT0_ENV_COUNT + 1];

static void crt0_build_environ(void) {
    static struct process_environment_variable list[CRT0_ENV_COUNT];
    int32_t listed = pc_listenv(list, CRT0_ENV_COUNT);
    uint32_t count = 0;
    for (int32_t i = 0; i < listed && count < CRT0_ENV_COUNT; i++) {
        if (!list[i].used) continue;
        uint32_t n = 0;
        for (uint32_t k = 0; list[i].name[k] && n + 1 < sizeof(crt0_env_storage[count]); k++)
            crt0_env_storage[count][n++] = list[i].name[k];
        crt0_env_storage[count][n++] = '=';
        for (uint32_t k = 0; list[i].value[k] && n + 1 < sizeof(crt0_env_storage[count]); k++)
            crt0_env_storage[count][n++] = list[i].value[k];
        crt0_env_storage[count][n] = '\0';
        crt0_environ[count] = crt0_env_storage[count];
        count++;
    }
    crt0_environ[count] = 0;
    environ = crt0_environ;
}

__asm__(
    ".text\n"
    ".global _start\n"
    "_start:\n"
    "    xorl %ebp, %ebp\n"
    "    leaq 8(%rsp), %rdi\n"
    "    andq $-16, %rsp\n"
    "    call crt0_main\n"
    "    hlt\n");

extern char __tdata_start[] __attribute__((weak));
extern char __tdata_end[] __attribute__((weak));
extern char __tbss_end[] __attribute__((weak));
extern char __tls_align[] __attribute__((weak));
void __tls_init_main(const void *image, size_t filesz, size_t memsz,
                     size_t alignment);

static void crt0_init_tls(void) {
    if (!__tdata_start || !__tbss_end)
        return;
    size_t filesz = (size_t)(__tdata_end - __tdata_start);
    size_t memsz = (size_t)(__tbss_end - __tdata_start);
    if (!memsz)
        return;
    __tls_init_main(__tdata_start, filesz, memsz, (size_t)__tls_align);
}

void crt0_main(uint64_t *frame) {
    int argc = 0;
    crt0_init_tls();
    if (frame[0] > 0) {
        char **vector = (char **)(frame + 1);
        int count = (int)frame[0];
        environ = vector + count + 1;
        crt0_run_constructors();
        int status = main(count, vector);
        exit(status);
    }
    crt0_build_environ();
    if (pc_get_process_name(crt0_program, 32) >= 0
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
            char quote = 0;
            if (crt0_cmdline[i] == '"' || crt0_cmdline[i] == '\'') { quote = crt0_cmdline[i]; i++; }
            crt0_argv[argc++] = &crt0_cmdline[i];
            while (crt0_cmdline[i]
                   && ((quote && crt0_cmdline[i] != quote)
                       || (!quote && crt0_cmdline[i] != ' ' && crt0_cmdline[i] != '\t'
                           && crt0_cmdline[i] != '\n' && crt0_cmdline[i] != '\r'))) i++;
            if (crt0_cmdline[i]) crt0_cmdline[i++] = '\0';
        }
    }
    crt0_argv[argc] = 0;
    crt0_run_constructors();
    int status = main(argc, crt0_argv);
    exit(status);
}
