#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define INITTAB_PATH "/etc/inittab"
#define MAX_ENTRIES 24
#define MAX_ARGS 12
#define FAST_RESPAWN_MS 2000
#define FAST_RESPAWN_LIMIT 5
#define RESPAWN_PAUSE_MS 10000

enum action { ACT_SYSINIT, ACT_WAIT, ACT_ONCE, ACT_RESPAWN, ACT_GRAPHICAL };

struct entry {
    char id[16];
    enum action action;
    char command[160];
    pid_t pid;
    bool done;
    int fast_exits;
    long long started_ms;
    long long hold_until_ms;
};

static struct entry entries[MAX_ENTRIES];
static int entry_count;
static bool text_mode;

static const char default_inittab[] =
    "tty1:respawn:/sbin/getty tty1\n"
    "tty2:respawn:/sbin/getty tty2\n"
    "tty3:respawn:/sbin/getty tty3\n"
    "tty4:respawn:/sbin/getty tty4\n"
    "tty5:respawn:/sbin/getty tty5\n"
    "tty6:respawn:/sbin/getty tty6\n";

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void say(const char *text)
{
    (void)write(1, text, strlen(text));
}

static bool parse_action(const char *name, enum action *out)
{
    static const struct { const char *name; enum action action; } table[] = {
        {"sysinit", ACT_SYSINIT}, {"wait", ACT_WAIT}, {"once", ACT_ONCE},
        {"respawn", ACT_RESPAWN}, {"graphical", ACT_GRAPHICAL},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (strcmp(name, table[i].name) == 0) {
            *out = table[i].action;
            return true;
        }
    return false;
}

static void copy_field(char *dst, size_t cap, const char *from, const char *to)
{
    size_t n = (size_t)(to - from);
    while (n && (*from == ' ' || *from == '\t')) {
        from++;
        n--;
    }
    while (n && (from[n - 1] == ' ' || from[n - 1] == '\t' || from[n - 1] == '\r'))
        n--;
    if (n >= cap)
        n = cap - 1;
    memcpy(dst, from, n);
    dst[n] = 0;
}

static void parse_line(const char *line, const char *end)
{
    if (entry_count >= MAX_ENTRIES)
        return;
    while (line < end && (*line == ' ' || *line == '\t'))
        line++;
    if (line >= end || *line == '#')
        return;
    const char *c1 = memchr(line, ':', (size_t)(end - line));
    if (!c1)
        return;
    const char *c2 = memchr(c1 + 1, ':', (size_t)(end - c1 - 1));
    if (!c2)
        return;
    struct entry *e = &entries[entry_count];
    memset(e, 0, sizeof(*e));
    char action[16];
    copy_field(e->id, sizeof(e->id), line, c1);
    copy_field(action, sizeof(action), c1 + 1, c2);
    copy_field(e->command, sizeof(e->command), c2 + 1, end);
    if (!e->command[0] || !parse_action(action, &e->action)) {
        say("init: bad inittab line ignored\n");
        return;
    }
    e->pid = -1;
    entry_count++;
}

static void parse_table(const char *text, size_t length)
{
    const char *p = text, *end = text + length;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *stop = nl ? nl : end;
        parse_line(p, stop);
        p = stop + 1;
    }
}

static void load_inittab(void)
{
    static char buffer[4096];
    int fd = open(INITTAB_PATH, O_RDONLY);
    if (fd >= 0) {
        ssize_t total = 0, n;
        while (total < (ssize_t)sizeof(buffer) - 1 &&
               (n = read(fd, buffer + total, sizeof(buffer) - 1 - (size_t)total)) > 0)
            total += n;
        close(fd);
        if (total > 0)
            parse_table(buffer, (size_t)total);
    }
    if (!entry_count)
        parse_table(default_inittab, sizeof(default_inittab) - 1);
}

static bool word_in(const char *haystack, const char *word)
{
    size_t n = strlen(word);
    for (const char *p = haystack; (p = strstr(p, word)); p += n) {
        bool left = p == haystack || p[-1] == ' ';
        bool right = p[n] == 0 || p[n] == ' ' || p[n] == '\n';
        if (left && right)
            return true;
    }
    return false;
}

static void read_cmdline(void)
{
    char buffer[256] = {0};
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0)
        return;
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n <= 0)
        return;
    buffer[n] = 0;
    text_mode = word_in(buffer, "text") || word_in(buffer, "single") ||
                word_in(buffer, "nogui") || word_in(buffer, "3");
}

static pid_t start(struct entry *e)
{
    pid_t pid = fork();
    if (pid != 0)
        return pid;
    static char command[160];
    char *argv[MAX_ARGS + 1];
    int argc = 0;
    strcpy(command, e->command);
    for (char *p = strtok(command, " \t"); p && argc < MAX_ARGS; p = strtok(0, " \t"))
        argv[argc++] = p;
    argv[argc] = 0;
    char *envp[] = {"PATH=/usr/bin:/bin:/usr/sbin:/sbin", "TERM=xterm", "HOME=/root", 0};
    execve(argv[0], argv, envp);
    _exit(127);
}

static void launch(struct entry *e)
{
    e->pid = start(e);
    e->started_ms = now_ms();
    if (e->pid < 0) {
        say("init: fork failed\n");
        e->hold_until_ms = now_ms() + RESPAWN_PAUSE_MS;
        return;
    }
    if (e->action == ACT_SYSINIT || e->action == ACT_WAIT) {
        int status = 0;
        while (waitpid(e->pid, &status, 0) < 0) {
        }
        e->pid = -1;
        e->done = true;
    }
}

static void child_exited(pid_t pid)
{
    for (int i = 0; i < entry_count; i++) {
        struct entry *e = &entries[i];
        if (e->pid != pid)
            continue;
        e->pid = -1;
        if (e->action != ACT_RESPAWN) {
            e->done = true;
            return;
        }
        long long t = now_ms();
        if (t - e->started_ms < FAST_RESPAWN_MS) {
            if (++e->fast_exits >= FAST_RESPAWN_LIMIT) {
                char msg[96];
                snprintf(msg, sizeof(msg), "init: '%s' respawning too fast, pausing\n", e->id);
                say(msg);
                e->hold_until_ms = t + RESPAWN_PAUSE_MS;
                e->fast_exits = 0;
            }
        } else {
            e->fast_exits = 0;
        }
        return;
    }
}

static void run_phase(enum action action)
{
    for (int i = 0; i < entry_count; i++)
        if (entries[i].action == action)
            launch(&entries[i]);
}

int main(void)
{
    if (getpid() != 1)
        return 1;
    read_cmdline();
    load_inittab();
    run_phase(ACT_SYSINIT);
    run_phase(ACT_WAIT);
    for (int i = 0; i < entry_count; i++) {
        struct entry *e = &entries[i];
        if (e->action == ACT_ONCE || e->action == ACT_RESPAWN)
            launch(e);
        else if (e->action == ACT_GRAPHICAL && !text_mode)
            launch(e);
    }
    for (;;) {
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
            child_exited(pid);
        long long t = now_ms();
        for (int i = 0; i < entry_count; i++) {
            struct entry *e = &entries[i];
            if (e->action == ACT_RESPAWN && e->pid < 0 && t >= e->hold_until_ms)
                launch(e);
        }
        usleep(200000);
    }
}
