#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "../accounts/accounts.h"

#define NAME_MAX_LEN 31
#define PASS_MAX_LEN 63
#define MAX_TRIES 3

static char tty_name[16] = "tty1";
static char host[64] = "purec";

static void out(const char *text)
{
    (void)write(1, text, strlen(text));
}

static void wipe(void *p, size_t n)
{
    volatile unsigned char *b = p;
    while (n--)
        *b++ = 0;
}

static void sane_termios(void)
{
    struct termios t;
    if (tcgetattr(0, &t) != 0)
        return;
    t.c_iflag = ICRNL | IXON;
    t.c_oflag = OPOST | ONLCR;
    t.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
    t.c_cc[VINTR] = 3;
    t.c_cc[VQUIT] = 28;
    t.c_cc[VERASE] = 127;
    t.c_cc[VKILL] = 21;
    t.c_cc[VEOF] = 4;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
}

static void set_echo(bool on)
{
    struct termios t;
    if (tcgetattr(0, &t) != 0)
        return;
    if (on)
        t.c_lflag |= ECHO;
    else
        t.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(0, TCSANOW, &t);
}

static bool read_line(char *buf, size_t cap)
{
    size_t n = 0;
    for (;;) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0)
            return n > 0;
        if (c == '\n' || c == '\r')
            break;
        if (n + 1 < cap)
            buf[n++] = c;
    }
    buf[n] = 0;
    return true;
}

static void print_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return;
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        (void)write(1, buf, (size_t)n);
    close(fd);
}

static void issue(void)
{
    int fd = open("/etc/issue", O_RDONLY);
    if (fd < 0) {
        char line[160];
        snprintf(line, sizeof(line), "\nPureC OS %s (%s)\n\n", host, tty_name);
        out(line);
        return;
    }
    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = 0;
    out("\n");
    for (char *p = buf; *p; p++) {
        if (p[0] == '\\' && p[1] == 'n') {
            out(host);
            p++;
        } else if (p[0] == '\\' && p[1] == 'l') {
            out(tty_name);
            p++;
        } else {
            (void)write(1, p, 1);
        }
    }
    out("\n");
}

static bool live_system(void)
{
    int fd = open("/etc/shadow", O_RDONLY);
    if (fd < 0)
        return true;
    close(fd);
    return false;
}

static void add_env(char **envp, int *n, char storage[][160], const char *key, const char *value)
{
    snprintf(storage[*n], 160, "%s=%s", key, value);
    envp[*n] = storage[*n];
    (*n)++;
}

static void start_shell(const struct user_info *who)
{
    if (pc_setgid(who->gid) != 0 || pc_setuid(who->uid) != 0) {
        out("getty: cannot switch user\n");
        exit(1);
    }
    const char *home = who->home[0] ? who->home : "/";
    if (chdir(home) != 0) {
        out("No home directory, logging in with HOME=/\n");
        home = "/";
        (void)chdir("/");
    }
    const char *shell = who->shell[0] ? who->shell : "/bin/sh";
    if (access(shell, X_OK) != 0)
        shell = "/bin/sh";
    static char storage[10][160];
    char *envp[11];
    int n = 0;
    add_env(envp, &n, storage, "HOME", home);
    add_env(envp, &n, storage, "USER", who->name);
    add_env(envp, &n, storage, "LOGNAME", who->name);
    add_env(envp, &n, storage, "SHELL", shell);
    add_env(envp, &n, storage, "PATH", who->uid == 0
            ? "/usr/bin:/bin:/usr/sbin:/sbin:/usr/games:/bin/program/system:/bin/program"
            : "/usr/bin:/bin:/usr/games:/bin/program");
    add_env(envp, &n, storage, "TERM", "xterm");
    add_env(envp, &n, storage, "TTY", tty_name);
    add_env(envp, &n, storage, "PS1", who->uid == 0
            ? "\\[\\e[1;31m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[1;34m\\]\\w\\[\\e[0m\\]# "
            : "\\[\\e[1;32m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[1;34m\\]\\w\\[\\e[0m\\]$ ");
    envp[n] = 0;
    print_file("/etc/motd");
    (void)pc_tty_ioctl(0, TTY_TIOCSPGRP_PID, (void *)(uintptr_t)getpid());
    char *argv[] = {(char *)"sh", 0};
    execve(shell, argv, envp);
    out("getty: cannot run shell\n");
    exit(1);
}

static void attach(const char *name)
{
    char path[32];
    snprintf(path, sizeof(path), "/dev/%s", name);
    int fd = open(path, O_RDWR);
    if (fd < 0)
        exit(1);
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2)
        close(fd);
}

int main(int argc, char **argv)
{
    bool noclear = false;
    const char *name = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--noclear") == 0)
            noclear = true;
        else
            name = argv[i];
    }
    if (!name)
        return 2;
    if (strncmp(name, "/dev/", 5) == 0)
        name += 5;
    snprintf(tty_name, sizeof(tty_name), "%s", name);
    attach(tty_name);
    sane_termios();
    if (gethostname(host, sizeof(host)) != 0 || !host[0])
        strcpy(host, "purec");
    if (!noclear)
        out("\033[H\033[2J");
    issue();
    bool live = live_system();
    for (int tries = 0; tries < MAX_TRIES;) {
        char user[NAME_MAX_LEN + 1];
        char pass[PASS_MAX_LEN + 1];
        char prompt[96];
        snprintf(prompt, sizeof(prompt), "%s login: ", host);
        out(prompt);
        if (!read_line(user, sizeof(user)))
            return 0;
        if (!user[0])
            continue;
        struct user_info who;
        if (live) {
            if (pc_user_lookup(0, 0, &who) != 0) {
                memset(&who, 0, sizeof(who));
                strcpy(who.name, "root");
                strcpy(who.home, "/root");
                strcpy(who.shell, "/bin/sh");
            }
            start_shell(&who);
        }
        out("Password: ");
        set_echo(false);
        bool got = read_line(pass, sizeof(pass));
        set_echo(true);
        out("\n");
        bool ok = got && account_login(user, pass, &who);
        wipe(pass, sizeof(pass));
        if (ok)
            start_shell(&who);
        sleep(2);
        out("\nLogin incorrect\n");
        tries++;
    }
    return 1;
}
