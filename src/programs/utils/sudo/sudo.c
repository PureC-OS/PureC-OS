#include "../lib/common.h"
#include "libc/include/purec.h"
#include <termios.h>

#define SUDO_TRIES 3

static void usage(void)
{
    fprintf(stderr, "usage: sudo [-i | -s] [command [arg ...]]\n");
}

static bool read_password(const char *user, char *out, size_t cap)
{
    struct termios saved, quiet;
    bool tty = tcgetattr(0, &saved) == 0;
    if (tty) {
        quiet = saved;
        quiet.c_lflag &= ~(tcflag_t)(ECHO | ECHOE | ECHOK);
        tcsetattr(0, TCSAFLUSH, &quiet);
    }
    fprintf(stderr, "[sudo] password for %s: ", user);
    fflush(stderr);
    size_t len = 0;
    bool ok = false;
    for (;;) {
        char c;
        ssize_t got = read(0, &c, 1);
        if (got <= 0)
            break;
        if (c == '\n' || c == '\r') {
            ok = true;
            break;
        }
        if ((c == 127 || c == '\b') && len) {
            len--;
            continue;
        }
        if (len + 1 < cap)
            out[len++] = c;
    }
    out[len] = 0;
    if (tty)
        tcsetattr(0, TCSAFLUSH, &saved);
    fputc('\n', stderr);
    return ok;
}

static void wipe(char *p, size_t n)
{
    volatile char *v = p;
    while (n--)
        *v++ = 0;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    int first = 1;
    bool shell = false;
    if (argc > 1 && (strcmp(argv[1], "-i") == 0 || strcmp(argv[1], "-s") == 0)) {
        shell = true;
        first = 2;
    } else if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage();
        return 0;
    }
    if (!shell && first >= argc) {
        usage();
        return 1;
    }
    char name[64];
    util_user(getuid(), name, sizeof(name));
    char invoker[64];
    snprintf(invoker, sizeof(invoker), "%s", name);
    if (getuid() != 0) {
        int status = -1;
        for (int attempt = 0; attempt < SUDO_TRIES; attempt++) {
            char password[256];
            if (!read_password(invoker, password, sizeof(password))) {
                wipe(password, sizeof(password));
                return 1;
            }
            status = pc_sudo(password);
            wipe(password, sizeof(password));
            if (status == 0)
                break;
            if (status == -2) {
                fprintf(stderr, "%s is not in the wheel group. This incident has been logged.\n", invoker);
                return 1;
            }
            fprintf(stderr, "Sorry, try again.\n");
        }
        if (status != 0) {
            fprintf(stderr, "sudo: %d incorrect password attempts\n", SUDO_TRIES);
            return 1;
        }
    }
    setenv("SUDO_USER", invoker, 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    if (shell) {
        setenv("HOME", "/root", 1);
        if (strcmp(argv[1], "-i") == 0)
            chdir("/root");
        const char *sh = "/bin/sh";
        char *args[2] = {(char *)"sh", 0};
        execv(sh, args);
        util_err("cannot run %s: %s", sh, strerror(errno));
        return 127;
    }
    execvp(argv[first], argv + first);
    util_err("%s: %s", argv[first], strerror(errno));
    return errno == ENOENT ? 127 : 126;
}
