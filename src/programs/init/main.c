#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "libc/include/purec.h"

#define MAX_UNITS 96
#define MAX_DEPS 16
#define MAX_ARGS 24
#define MAX_ENV 12
#define NAME_LEN 64
#define CMD_LEN 256
#define TICK_US 100000
#define STOP_TIMEOUT_MS 5000
#define BURST_WINDOW_MS 10000
#define BURST_LIMIT 5
#define BURST_PAUSE_MS 10000
#define LOG_LIMIT (256 * 1024)
#define RUN_DIR "/run/init"
#define REQ_DIR "/run/init/req"

static const char *const unit_dirs[] = {"/etc/init/system", "/run/init/system", "/usr/lib/init/system"};
#define UNIT_DIR_COUNT (sizeof(unit_dirs) / sizeof(unit_dirs[0]))

enum kind { K_SERVICE, K_TARGET };
enum svc_type { T_SIMPLE, T_ONESHOT };
enum restart { R_NO, R_ALWAYS, R_ON_FAILURE };
enum state { S_INACTIVE, S_WAITING, S_ACTIVATING, S_ACTIVE, S_DEACTIVATING, S_FAILED, S_RESTARTING };

struct unit {
    char name[NAME_LEN];
    char path[160];
    char description[96];
    enum kind kind;
    bool loaded;
    bool masked;
    char requires[MAX_DEPS][NAME_LEN];
    int requires_n;
    char wants[MAX_DEPS * 2][NAME_LEN];
    int wants_n;
    char after[MAX_DEPS][NAME_LEN];
    int after_n;
    char before[MAX_DEPS][NAME_LEN];
    int before_n;
    char conflicts[4][NAME_LEN];
    int conflicts_n;
    char wanted_by[4][NAME_LEN];
    int wanted_by_n;
    char condition_path[128];
    enum svc_type type;
    enum restart restart;
    int restart_ms;
    bool remain;
    char exec_pre[CMD_LEN];
    char exec[CMD_LEN];
    char exec_post[CMD_LEN];
    char exec_stop[CMD_LEN];
    char user[32];
    char workdir[128];
    char output[160];
    char env[MAX_ENV][96];
    int env_n;

    enum state state;
    bool wanted;
    bool stop_requested;
    bool restart_pending;
    int phase;
    pid_t pid;
    int exit_status;
    char result[48];
    long long since_ms;
    long long started_ms;
    long long deadline_ms;
    long long burst_start_ms;
    int burst_count;
    int restarts;
};

static struct unit units[MAX_UNITS];
static int unit_count;
static char default_target[NAME_LEN] = "multi-user.target";
static int shutdown_mode;
static long long shutdown_deadline;
static bool status_dirty = true;

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void console(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof(line))
        n = sizeof(line) - 1;
    syslog(LOG_INFO, "%s", line);
    int fd = open("/dev/console", O_WRONLY);
    if (fd >= 0) {
        (void)write(fd, line, (size_t)n);
        (void)write(fd, "\n", 1);
        close(fd);
    }
}

static const char *state_name(enum state s)
{
    static const char *const names[] = {"inactive", "waiting", "activating", "active", "deactivating", "failed",
                                        "auto-restart"};
    return names[s];
}

static void set_state(struct unit *u, enum state s)
{
    if (u->state != s) {
        u->state = s;
        u->since_ms = now_ms();
        status_dirty = true;
    }
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    return s;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

static bool valid_unit_name(const char *name)
{
    if (!*name || strlen(name) >= NAME_LEN || strchr(name, '/') || name[0] == '.')
        return false;
    return ends_with(name, ".service") || ends_with(name, ".target");
}

static struct unit *find_unit(const char *name)
{
    for (int i = 0; i < unit_count; i++)
        if (!strcmp(units[i].name, name))
            return &units[i];
    return NULL;
}

static void add_word(char list[][NAME_LEN], int *n, int cap, const char *word)
{
    for (int i = 0; i < *n; i++)
        if (!strcmp(list[i], word))
            return;
    if (*n < cap && strlen(word) < NAME_LEN)
        snprintf(list[(*n)++], NAME_LEN, "%s", word);
}

static void add_words(char list[][NAME_LEN], int *n, int cap, char *value)
{
    for (char *w = strtok(value, " \t"); w; w = strtok(NULL, " \t"))
        add_word(list, n, cap, w);
}

static void instance_of(const char *name, char *templ, size_t tcap, char *inst, size_t icap)
{
    templ[0] = inst[0] = 0;
    const char *at = strchr(name, '@');
    const char *dot = strrchr(name, '.');
    if (!at || !dot || dot < at)
        return;
    snprintf(templ, tcap, "%.*s@%s", (int)(at - name), name, dot);
    snprintf(inst, icap, "%.*s", (int)(dot - at - 1), at + 1);
}

static void expand(const char *in, char *out, size_t cap, const char *inst, const char *name)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < cap; p++) {
        if (p[0] == '%' && (p[1] == 'i' || p[1] == 'I' || p[1] == 'n' || p[1] == '%')) {
            const char *rep = p[1] == 'n' ? name : p[1] == '%' ? "%" : inst;
            for (; *rep && n + 1 < cap; rep++)
                out[n++] = *rep;
            p++;
            continue;
        }
        out[n++] = *p;
    }
    out[n] = 0;
}

static int parse_ms(const char *v)
{
    char *end;
    double x = strtod(v, &end);
    if (!strcmp(end, "ms"))
        return (int)x;
    if (!strcmp(end, "min"))
        return (int)(x * 60000);
    return (int)(x * 1000);
}

static bool read_file(const char *path, char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    size_t total = 0;
    ssize_t n;
    while (total + 1 < cap && (n = read(fd, buf + total, cap - 1 - total)) > 0)
        total += (size_t)n;
    close(fd);
    buf[total] = 0;
    return true;
}

static void parse_unit(struct unit *u, char *text, const char *inst)
{
    enum { NONE, UNIT, SERVICE, INSTALL } section = NONE;
    for (char *line = text; line && *line;) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
        char *s = trim(line);
        if (*s == '[') {
            if (!strncmp(s, "[Unit]", 6))
                section = UNIT;
            else if (!strncmp(s, "[Service]", 9))
                section = SERVICE;
            else if (!strncmp(s, "[Install]", 9))
                section = INSTALL;
            else
                section = NONE;
        } else if (*s && *s != '#' && *s != ';') {
            char *eq = strchr(s, '=');
            if (eq) {
                *eq = 0;
                char *key = trim(s);
                char raw[CMD_LEN], value[CMD_LEN];
                snprintf(raw, sizeof(raw), "%s", trim(eq + 1));
                expand(raw, value, sizeof(value), inst, u->name);
                if (section == UNIT) {
                    if (!strcmp(key, "Description"))
                        snprintf(u->description, sizeof(u->description), "%s", value);
                    else if (!strcmp(key, "Requires"))
                        add_words(u->requires, &u->requires_n, MAX_DEPS, value);
                    else if (!strcmp(key, "Wants"))
                        add_words(u->wants, &u->wants_n, MAX_DEPS * 2, value);
                    else if (!strcmp(key, "After"))
                        add_words(u->after, &u->after_n, MAX_DEPS, value);
                    else if (!strcmp(key, "Before"))
                        add_words(u->before, &u->before_n, MAX_DEPS, value);
                    else if (!strcmp(key, "Conflicts"))
                        add_words(u->conflicts, &u->conflicts_n, 4, value);
                    else if (!strcmp(key, "ConditionPathExists"))
                        snprintf(u->condition_path, sizeof(u->condition_path), "%s", value);
                } else if (section == SERVICE) {
                    if (!strcmp(key, "Type"))
                        u->type = !strcmp(value, "oneshot") ? T_ONESHOT : T_SIMPLE;
                    else if (!strcmp(key, "ExecStart"))
                        snprintf(u->exec, sizeof(u->exec), "%s", value);
                    else if (!strcmp(key, "ExecStartPre"))
                        snprintf(u->exec_pre, sizeof(u->exec_pre), "%s", value);
                    else if (!strcmp(key, "ExecStartPost"))
                        snprintf(u->exec_post, sizeof(u->exec_post), "%s", value);
                    else if (!strcmp(key, "ExecStop"))
                        snprintf(u->exec_stop, sizeof(u->exec_stop), "%s", value);
                    else if (!strcmp(key, "Restart"))
                        u->restart = !strcmp(value, "always") ? R_ALWAYS
                                     : !strcmp(value, "on-failure") ? R_ON_FAILURE
                                                                     : R_NO;
                    else if (!strcmp(key, "RestartSec"))
                        u->restart_ms = parse_ms(value);
                    else if (!strcmp(key, "RemainAfterExit"))
                        u->remain = !strcmp(value, "yes") || !strcmp(value, "true") || !strcmp(value, "1");
                    else if (!strcmp(key, "User"))
                        snprintf(u->user, sizeof(u->user), "%s", value);
                    else if (!strcmp(key, "WorkingDirectory"))
                        snprintf(u->workdir, sizeof(u->workdir), "%s", value);
                    else if (!strcmp(key, "StandardOutput"))
                        snprintf(u->output, sizeof(u->output), "%s", value);
                    else if (!strcmp(key, "Environment") && u->env_n < MAX_ENV)
                        snprintf(u->env[u->env_n++], sizeof(u->env[0]), "%s", value);
                } else if (section == INSTALL) {
                    if (!strcmp(key, "WantedBy"))
                        add_words(u->wanted_by, &u->wanted_by_n, 4, value);
                }
            }
        }
        line = nl ? nl + 1 : NULL;
    }
}

static struct unit *load_unit(const char *name);

static void load_wants_dir(struct unit *u, const char *suffix, bool requires)
{
    for (size_t d = 0; d < UNIT_DIR_COUNT; d++) {
        char dir[200];
        snprintf(dir, sizeof(dir), "%s/%s%s", unit_dirs[d], u->name, suffix);
        DIR *dp = opendir(dir);
        if (!dp)
            continue;
        struct dirent *e;
        while ((e = readdir(dp)) != NULL) {
            if (!valid_unit_name(e->d_name))
                continue;
            if (requires)
                add_word(u->requires, &u->requires_n, MAX_DEPS, e->d_name);
            else
                add_word(u->wants, &u->wants_n, MAX_DEPS * 2, e->d_name);
        }
        closedir(dp);
    }
}

static void fill_unit(struct unit *u)
{
    char templ[NAME_LEN], inst[NAME_LEN];
    instance_of(u->name, templ, sizeof(templ), inst, sizeof(inst));
    static char text[8192];
    for (size_t d = 0; d < UNIT_DIR_COUNT && !u->loaded; d++) {
        const char *candidates[2] = {u->name, templ[0] ? templ : NULL};
        for (int c = 0; c < 2 && candidates[c] && !u->loaded; c++) {
            char path[200];
            snprintf(path, sizeof(path), "%s/%s", unit_dirs[d], candidates[c]);
            struct stat st;
            if (stat(path, &st) != 0)
                continue;
            snprintf(u->path, sizeof(u->path), "%s", path);
            u->loaded = true;
            if (st.st_size == 0)
                u->masked = true;
            else if (read_file(path, text, sizeof(text)))
                parse_unit(u, text, inst);
        }
    }
    load_wants_dir(u, ".wants", false);
    load_wants_dir(u, ".requires", true);
    if (u->kind == K_TARGET) {
        for (int i = 0; i < u->wants_n; i++)
            add_word(u->after, &u->after_n, MAX_DEPS, u->wants[i]);
        for (int i = 0; i < u->requires_n; i++)
            add_word(u->after, &u->after_n, MAX_DEPS, u->requires[i]);
    }
}

static void reset_config(struct unit *u)
{
    char name[NAME_LEN];
    snprintf(name, sizeof(name), "%s", u->name);
    size_t from = offsetof(struct unit, path), to = offsetof(struct unit, state);
    memset((char *)u + from, 0, to - from);
    u->kind = ends_with(name, ".target") ? K_TARGET : K_SERVICE;
    u->restart_ms = 100;
}

static struct unit *load_unit(const char *name)
{
    struct unit *u = find_unit(name);
    if (u)
        return u;
    if (!valid_unit_name(name) || unit_count >= MAX_UNITS)
        return NULL;
    u = &units[unit_count++];
    memset(u, 0, sizeof(*u));
    snprintf(u->name, sizeof(u->name), "%s", name);
    u->pid = -1;
    reset_config(u);
    fill_unit(u);
    status_dirty = true;
    return u;
}

static void load_closure(const char *root)
{
    if (!load_unit(root))
        return;
    for (int i = 0; i < unit_count; i++) {
        struct unit *u = &units[i];
        for (int k = 0; k < u->requires_n; k++)
            load_unit(u->requires[k]);
        for (int k = 0; k < u->wants_n; k++)
            load_unit(u->wants[k]);
    }
}

static bool orders_before(const struct unit *a, const struct unit *b)
{
    for (int i = 0; i < b->after_n; i++)
        if (!strcmp(b->after[i], a->name))
            return true;
    for (int i = 0; i < a->before_n; i++)
        if (!strcmp(a->before[i], b->name))
            return true;
    return false;
}

static void mark_wanted(struct unit *u)
{
    if (!u || u->wanted || u->masked)
        return;
    u->wanted = true;
    u->stop_requested = false;
    if (u->state == S_INACTIVE || u->state == S_FAILED) {
        u->burst_count = 0;
        set_state(u, S_WAITING);
    }
    for (int i = 0; i < u->requires_n; i++)
        mark_wanted(load_unit(u->requires[i]));
    for (int i = 0; i < u->wants_n; i++)
        mark_wanted(load_unit(u->wants[i]));
}

static bool is_up(const struct unit *u)
{
    return u->state == S_ACTIVE || (u->state == S_INACTIVE && !strcmp(u->result, "success"));
}

static bool is_settled(const struct unit *u)
{
    return u->state == S_ACTIVE || u->state == S_FAILED || u->state == S_INACTIVE;
}

static bool split_command(char *buf, char **argv, bool *ignore_failure)
{
    int argc = 0;
    char *p = buf;
    *ignore_failure = false;
    while (*p == '-' || *p == '@' || *p == '+' || *p == '!') {
        if (*p == '-')
            *ignore_failure = true;
        p++;
    }
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        char *w = p, *o = p;
        char quote = 0;
        while (*p && (quote || (*p != ' ' && *p != '\t'))) {
            if (!quote && (*p == '"' || *p == '\'')) {
                quote = *p++;
                continue;
            }
            if (quote && *p == quote) {
                quote = 0;
                p++;
                continue;
            }
            *o++ = *p++;
        }
        if (*p)
            p++;
        *o = 0;
        argv[argc++] = w;
    }
    argv[argc] = NULL;
    return argc > 0 && argv[0][0] == '/';
}

static void open_output(const struct unit *u)
{
    const char *mode = u->output[0] ? u->output : "log";
    int fd = -1;
    if (!strcmp(mode, "null")) {
        fd = open("/dev/null", O_RDWR);
    } else if (!strcmp(mode, "tty") || !strncmp(mode, "tty:", 4)) {
        fd = open(mode[3] == ':' ? mode + 4 : "/dev/console", O_RDWR);
    } else if (!strncmp(mode, "file:", 5) || !strncmp(mode, "append:", 7)) {
        bool app = mode[0] == 'a';
        fd = open(mode + (app ? 7 : 5), O_WRONLY | O_CREAT | (app ? O_APPEND : O_TRUNC), 0640);
    } else {
        char path[160];
        mkdir("/var/log", 0755);
        mkdir("/var/log/init", 0755);
        snprintf(path, sizeof(path), "/var/log/init/%s.log", u->name);
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > LOG_LIMIT) {
            char old[176];
            snprintf(old, sizeof(old), "%s.1", path);
            rename(path, old);
        }
        fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0640);
    }
    int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd >= 0 && null_fd != 0) {
        dup2(null_fd, 0);
        close(null_fd);
    }
    if (fd < 0)
        fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2)
            close(fd);
    }
}

static pid_t spawn(const struct unit *u, const char *command, bool *ignore_failure)
{
    char buf[CMD_LEN];
    char *argv[MAX_ARGS + 1];
    snprintf(buf, sizeof(buf), "%s", command);
    if (!split_command(buf, argv, ignore_failure))
        return -2;
    const struct passwd *pw = u->user[0] ? getpwnam(u->user) : NULL;
    if (u->user[0] && !pw)
        return -3;
    pid_t pid = fork();
    if (pid != 0)
        return pid;
    setsid();
    for (int s = 1; s < 32; s++)
        signal(s, SIG_DFL);
    open_output(u);
    char *envp[MAX_ENV + 8];
    char home[160], user[64], logname[72], unit_env[96];
    int n = 0;
    envp[n++] = "PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
    envp[n++] = "TERM=xterm";
    snprintf(home, sizeof(home), "HOME=%s", pw ? pw->pw_dir : "/root");
    snprintf(user, sizeof(user), "USER=%s", pw ? pw->pw_name : "root");
    snprintf(logname, sizeof(logname), "LOGNAME=%s", pw ? pw->pw_name : "root");
    snprintf(unit_env, sizeof(unit_env), "INIT_UNIT=%s", u->name);
    envp[n++] = home;
    envp[n++] = user;
    envp[n++] = logname;
    envp[n++] = unit_env;
    for (int i = 0; i < u->env_n; i++)
        envp[n++] = (char *)u->env[i];
    envp[n] = NULL;
    if (u->workdir[0] && chdir(u->workdir) != 0)
        _exit(200);
    if (!u->workdir[0])
        (void)chdir(pw ? pw->pw_dir : "/");
    if (pw && (pc_setgid(pw->pw_gid) != 0 || pc_setuid(pw->pw_uid) != 0))
        _exit(201);
    execve(argv[0], argv, envp);
    _exit(127);
}

static bool deps_failed(struct unit *u, char *why, size_t cap)
{
    for (int i = 0; i < u->requires_n; i++) {
        struct unit *d = find_unit(u->requires[i]);
        if (!d || !d->loaded || d->masked || d->state == S_FAILED) {
            snprintf(why, cap, "dependency %s failed", u->requires[i]);
            return true;
        }
    }
    return false;
}

static bool order_ready(const struct unit *u)
{
    bool blocked = false, only_waiting = true;
    for (int i = 0; i < unit_count; i++) {
        const struct unit *o = &units[i];
        if (o == u || !o->wanted)
            continue;
        if (orders_before(o, u) && !is_settled(o)) {
            blocked = true;
            if (o->state != S_WAITING)
                only_waiting = false;
        }
    }
    if (!blocked)
        return true;
    if (only_waiting && now_ms() - u->since_ms > 5000) {
        console("[ WARN ] ordering cycle around %s, starting it anyway", u->name);
        return true;
    }
    return false;
}

static void fail(struct unit *u, const char *why)
{
    snprintf(u->result, sizeof(u->result), "%s", why);
    u->pid = -1;
    set_state(u, S_FAILED);
    console("[FAILED] %s: %s", u->description[0] ? u->description : u->name, why);
}

static void become_active(struct unit *u)
{
    snprintf(u->result, sizeof(u->result), "success");
    set_state(u, S_ACTIVE);
    if (u->kind == K_SERVICE)
        console("[  OK  ] Started %s", u->description[0] ? u->description : u->name);
    else if (!strcmp(u->name, default_target))
        console("[  OK  ] Reached target %s", u->description[0] ? u->description : u->name);
}

static void run_phase(struct unit *u)
{
    const char *cmds[3] = {u->exec_pre, u->exec, u->exec_post};
    while (u->phase < 3 && !cmds[u->phase][0])
        u->phase++;
    if (u->phase >= 3) {
        u->pid = -1;
        if (u->type == T_ONESHOT && !u->remain) {
            snprintf(u->result, sizeof(u->result), "success");
            console("[  OK  ] Finished %s", u->description[0] ? u->description : u->name);
            u->wanted = false;
            set_state(u, S_INACTIVE);
        } else {
            become_active(u);
        }
        return;
    }
    bool ignore;
    pid_t pid = spawn(u, cmds[u->phase], &ignore);
    if (pid == -2) {
        fail(u, "bad command line");
        return;
    }
    if (pid == -3) {
        fail(u, "unknown user");
        return;
    }
    if (pid < 0) {
        fail(u, "fork failed");
        return;
    }
    u->pid = pid;
    u->started_ms = now_ms();
    set_state(u, S_ACTIVATING);
    if (u->phase == 1 && u->type == T_SIMPLE) {
        u->phase = 3;
        become_active(u);
    }
}

static void start_unit(struct unit *u)
{
    char why[96];
    if (!u->loaded) {
        fail(u, "unit file not found");
        return;
    }
    if (deps_failed(u, why, sizeof(why))) {
        fail(u, why);
        return;
    }
    if (u->condition_path[0]) {
        bool neg = u->condition_path[0] == '!';
        bool there = access(u->condition_path + (neg ? 1 : 0), F_OK) == 0;
        if (there == neg) {
            snprintf(u->result, sizeof(u->result), "condition not met");
            set_state(u, S_INACTIVE);
            u->wanted = false;
            return;
        }
    }
    for (int i = 0; i < u->conflicts_n; i++) {
        struct unit *c = find_unit(u->conflicts[i]);
        if (c && c->wanted) {
            c->wanted = false;
            c->stop_requested = true;
        }
    }
    if (u->kind == K_TARGET) {
        for (int i = 0; i < u->requires_n; i++) {
            struct unit *d = find_unit(u->requires[i]);
            if (d && !is_up(d)) {
                fail(u, "required unit is not active");
                return;
            }
        }
        become_active(u);
        return;
    }
    u->phase = 0;
    run_phase(u);
}

static void kill_unit(struct unit *u, int sig)
{
    if (u->pid <= 0)
        return;
    if (kill(-u->pid, sig) != 0)
        kill(u->pid, sig);
}

static void stop_unit(struct unit *u)
{
    u->wanted = false;
    u->stop_requested = false;
    if (u->state == S_WAITING || u->state == S_RESTARTING || u->state == S_FAILED) {
        set_state(u, S_INACTIVE);
        return;
    }
    if (u->state != S_ACTIVE && u->state != S_ACTIVATING)
        return;
    if (u->kind == K_TARGET || u->pid <= 0) {
        if (u->exec_stop[0]) {
            bool ignore;
            pid_t p = spawn(u, u->exec_stop, &ignore);
            if (p > 0) {
                int st;
                long long end = now_ms() + STOP_TIMEOUT_MS;
                while (waitpid(p, &st, WNOHANG) == 0 && now_ms() < end)
                    usleep(20000);
            }
        }
        set_state(u, S_INACTIVE);
        return;
    }
    set_state(u, S_DEACTIVATING);
    u->deadline_ms = now_ms() + STOP_TIMEOUT_MS;
    kill_unit(u, SIGTERM);
    kill_unit(u, SIGCONT);
}

static void handle_exit(pid_t pid, int status)
{
    for (int i = 0; i < unit_count; i++) {
        struct unit *u = &units[i];
        if (u->pid != pid)
            continue;
        u->pid = -1;
        u->exit_status = status;
        bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        char why[48];
        if (WIFEXITED(status))
            snprintf(why, sizeof(why), "exit code %d", WEXITSTATUS(status));
        else
            snprintf(why, sizeof(why), "killed by signal %d", WTERMSIG(status));
        if (u->state == S_DEACTIVATING || !u->wanted) {
            snprintf(u->result, sizeof(u->result), "%s", ok ? "success" : why);
            set_state(u, S_INACTIVE);
            return;
        }
        if (u->state == S_ACTIVATING && u->phase < 3) {
            const char *cmds[3] = {u->exec_pre, u->exec, u->exec_post};
            bool ignore = cmds[u->phase][0] == '-';
            if (!ok && !ignore) {
                fail(u, why);
                return;
            }
            u->phase++;
            run_phase(u);
            return;
        }
        snprintf(u->result, sizeof(u->result), "%s", ok ? "success" : why);
        bool again = u->restart == R_ALWAYS || (u->restart == R_ON_FAILURE && !ok);
        if (!again) {
            if (ok) {
                set_state(u, S_INACTIVE);
                u->wanted = false;
            } else {
                fail(u, why);
            }
            return;
        }
        long long t = now_ms();
        if (t - u->burst_start_ms > BURST_WINDOW_MS) {
            u->burst_start_ms = t;
            u->burst_count = 0;
        }
        u->burst_count++;
        u->restarts++;
        u->deadline_ms = t + u->restart_ms;
        if (u->burst_count >= BURST_LIMIT) {
            console("[ WARN ] %s restarts too often (%s), pausing %d s", u->name, why, BURST_PAUSE_MS / 1000);
            u->deadline_ms = t + BURST_PAUSE_MS;
            u->burst_count = 0;
            u->burst_start_ms = t + BURST_PAUSE_MS;
        }
        set_state(u, S_RESTARTING);
        return;
    }
}

static void schedule(void)
{
    long long t = now_ms();
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < unit_count; i++) {
            struct unit *u = &units[i];
            if (u->stop_requested) {
                stop_unit(u);
                progress = true;
            }
            if (u->restart_pending && (u->state == S_INACTIVE || u->state == S_FAILED)) {
                u->restart_pending = false;
                u->wanted = false;
                mark_wanted(u);
                progress = true;
            }
            if (u->state == S_WAITING && u->wanted && !shutdown_mode && order_ready(u)) {
                start_unit(u);
                progress = true;
            }
            if (u->state == S_RESTARTING && t >= u->deadline_ms && u->wanted && !shutdown_mode) {
                u->phase = 1;
                if (u->exec_pre[0])
                    u->phase = 0;
                run_phase(u);
                progress = true;
            }
            if (u->state == S_DEACTIVATING && t >= u->deadline_ms) {
                kill_unit(u, SIGKILL);
                u->deadline_ms = t + 1000000;
            }
        }
    }
}

static int enabled_marker(const struct unit *u, char *where, size_t cap)
{
    for (int w = 0; w < u->wanted_by_n; w++)
        for (size_t d = 0; d < UNIT_DIR_COUNT; d++) {
            char path[240];
            snprintf(path, sizeof(path), "%s/%s.wants/%s", unit_dirs[d], u->wanted_by[w], u->name);
            if (access(path, F_OK) == 0) {
                if (where)
                    snprintf(where, cap, "%s", path);
                return 1;
            }
        }
    return u->wanted_by_n ? 0 : -1;
}

static void write_status(void)
{
    if (!status_dirty)
        return;
    status_dirty = false;
    char tmp[] = RUN_DIR "/units.tmp";
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    long long t = now_ms();
    char line[640];
    for (int i = 0; i < unit_count; i++) {
        const struct unit *u = &units[i];
        int en = u->masked ? 2 : enabled_marker(u, NULL, 0);
        int n = snprintf(line, sizeof(line), "%s\t%s\t%s\t%d\t%lld\t%s\t%s\t%d\t%s\t%s\n", u->name,
                         !u->loaded ? "not-found" : state_name(u->state),
                         en == 2 ? "masked" : en == 1 ? "enabled" : en == 0 ? "disabled" : "static", (int)u->pid,
                         (t - u->since_ms) / 1000, u->result[0] ? u->result : "-", u->path[0] ? u->path : "-",
                         u->restarts, u->exec[0] ? u->exec : "-", u->description[0] ? u->description : "-");
        if (n > 0)
            (void)write(fd, line, (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1));
    }
    close(fd);
    rename(tmp, RUN_DIR "/units");
}

static void reply(const char *id, const char *fmt, ...)
{
    char path[160], tmp[168], text[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    snprintf(path, sizeof(path), REQ_DIR "/%s.ans", id);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    if (n > 0)
        (void)write(fd, text, (size_t)(n < (int)sizeof(text) ? n : (int)sizeof(text) - 1));
    close(fd);
    rename(tmp, path);
}

static void begin_shutdown(int mode)
{
    if (shutdown_mode)
        return;
    shutdown_mode = mode;
    shutdown_deadline = now_ms() + STOP_TIMEOUT_MS + 1000;
    console("init: stopping services for %s", mode == 1 ? "power off" : mode == 2 ? "reboot" : "halt");
    for (int i = unit_count - 1; i >= 0; i--)
        stop_unit(&units[i]);
}

static void reload_units(void)
{
    int count = unit_count;
    for (int i = 0; i < count; i++) {
        reset_config(&units[i]);
        fill_unit(&units[i]);
    }
    for (int i = 0; i < unit_count; i++) {
        for (int k = 0; k < units[i].requires_n; k++)
            load_unit(units[i].requires[k]);
        for (int k = 0; k < units[i].wants_n; k++)
            load_unit(units[i].wants[k]);
    }
    status_dirty = true;
}

static void handle_request(const char *id, char *text)
{
    char *verb = strtok(trim(text), " \t\n");
    if (!verb) {
        reply(id, "error: empty request\n");
        return;
    }
    if (!strcmp(verb, "poweroff") || !strcmp(verb, "reboot") || !strcmp(verb, "halt")) {
        reply(id, "ok\n");
        begin_shutdown(!strcmp(verb, "poweroff") ? 1 : !strcmp(verb, "reboot") ? 2 : 3);
        return;
    }
    if (!strcmp(verb, "daemon-reload")) {
        reload_units();
        reply(id, "ok\n");
        return;
    }
    if (!strcmp(verb, "isolate")) {
        char *name = strtok(NULL, " \t\n");
        struct unit *target = name ? load_unit(name) : NULL;
        if (!target || !target->loaded) {
            reply(id, "error: %s not found\n", name ? name : "?");
            return;
        }
        for (int i = 0; i < unit_count; i++)
            units[i].wanted = false;
        load_closure(name);
        mark_wanted(target);
        for (int i = 0; i < unit_count; i++)
            if (!units[i].wanted && units[i].state != S_INACTIVE)
                units[i].stop_requested = true;
        reply(id, "ok\n");
        return;
    }
    char out[480] = "";
    size_t used = 0;
    bool error = false;
    for (char *name = strtok(NULL, " \t\n"); name; name = strtok(NULL, " \t\n")) {
        char full[NAME_LEN];
        if (!strchr(name, '.'))
            snprintf(full, sizeof(full), "%s.service", name);
        else
            snprintf(full, sizeof(full), "%s", name);
        struct unit *u = load_unit(full);
        if (!u) {
            used += (size_t)snprintf(out + used, sizeof(out) - used, "error: bad unit name %s\n", full);
            error = true;
            continue;
        }
        if (u->masked) {
            used += (size_t)snprintf(out + used, sizeof(out) - used, "error: %s is masked\n", full);
            error = true;
            continue;
        }
        if (!strcmp(verb, "start")) {
            if (!u->loaded) {
                used += (size_t)snprintf(out + used, sizeof(out) - used, "error: %s not found\n", full);
                error = true;
                continue;
            }
            for (int i = 0; i < u->requires_n; i++)
                load_closure(u->requires[i]);
            for (int i = 0; i < u->wants_n; i++)
                load_closure(u->wants[i]);
            u->wanted = false;
            mark_wanted(u);
        } else if (!strcmp(verb, "stop")) {
            u->stop_requested = true;
            for (int i = 0; i < unit_count; i++)
                for (int k = 0; k < units[i].requires_n; k++)
                    if (!strcmp(units[i].requires[k], u->name) && units[i].wanted)
                        units[i].stop_requested = true;
        } else if (!strcmp(verb, "restart")) {
            if (!u->loaded) {
                used += (size_t)snprintf(out + used, sizeof(out) - used, "error: %s not found\n", full);
                error = true;
                continue;
            }
            stop_unit(u);
            u->restart_pending = true;
        } else if (!strcmp(verb, "kill")) {
            kill_unit(u, SIGKILL);
        } else {
            reply(id, "error: unknown request %s\n", verb);
            return;
        }
        if (used >= sizeof(out))
            break;
    }
    if (!out[0])
        snprintf(out, sizeof(out), "ok\n");
    reply(id, "%s", out);
}

static void poll_requests(void)
{
    DIR *d = opendir(REQ_DIR);
    if (!d)
        return;
    struct dirent *e;
    char names[16][64];
    int count = 0;
    while ((e = readdir(d)) != NULL && count < 16) {
        size_t n = strlen(e->d_name);
        if (n > 4 && n < 60 && !strcmp(e->d_name + n - 4, ".req"))
            snprintf(names[count++], sizeof(names[0]), "%s", e->d_name);
    }
    closedir(d);
    for (int i = 0; i < count; i++) {
        char path[128], id[64], text[512];
        snprintf(path, sizeof(path), REQ_DIR "/%s", names[i]);
        struct stat st;
        bool ok = stat(path, &st) == 0 && st.st_uid == 0 && read_file(path, text, sizeof(text));
        unlink(path);
        snprintf(id, sizeof(id), "%.*s", (int)(strlen(names[i]) - 4), names[i]);
        if (!ok) {
            reply(id, "error: permission denied\n");
            continue;
        }
        handle_request(id, text);
    }
}

static void read_cmdline(void)
{
    char buffer[512];
    if (read_file("/etc/init/default.target", buffer, sizeof(buffer))) {
        char *t = trim(buffer);
        if (valid_unit_name(t))
            snprintf(default_target, sizeof(default_target), "%s", t);
    }
    if (!read_file("/proc/cmdline", buffer, sizeof(buffer)))
        return;
    for (char *w = strtok(buffer, " \t\n"); w; w = strtok(NULL, " \t\n")) {
        if (!strncmp(w, "init.unit=", 10) && valid_unit_name(w + 10))
            snprintf(default_target, sizeof(default_target), "%s", w + 10);
        else if (!strcmp(w, "single") || !strcmp(w, "1") || !strcmp(w, "rescue"))
            snprintf(default_target, sizeof(default_target), "rescue.target");
        else if (!strcmp(w, "text") || !strcmp(w, "3") || !strcmp(w, "nogui"))
            snprintf(default_target, sizeof(default_target), "multi-user.target");
    }
}

static void emergency_gettys(void)
{
    console("init: no usable unit for %s, starting getty on tty1-6", default_target);
    unit_count = 0;
    for (int i = 1; i <= 6; i++) {
        struct unit *u = &units[unit_count++];
        memset(u, 0, sizeof(*u));
        snprintf(u->name, sizeof(u->name), "getty@tty%d.service", i);
        snprintf(u->description, sizeof(u->description), "Emergency getty on tty%d", i);
        snprintf(u->exec, sizeof(u->exec), "/sbin/getty tty%d", i);
        snprintf(u->output, sizeof(u->output), "null");
        u->kind = K_SERVICE;
        u->restart = R_ALWAYS;
        u->restart_ms = 100;
        u->loaded = true;
        u->pid = -1;
        mark_wanted(u);
    }
}

static void finish_shutdown(void)
{
    for (int i = 0; i < unit_count; i++)
        kill_unit(&units[i], SIGKILL);
    kill(-1, SIGTERM);
    usleep(300000);
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    sync();
    console("init: %s", shutdown_mode == 2 ? "rebooting" : "powering off");
    if (shutdown_mode == 2)
        pc_reboot();
    else
        pc_shutdown();
    for (;;)
        pause();
}

static void on_signal(int sig)
{
    if (sig == SIGTERM || sig == SIGINT)
        shutdown_mode = shutdown_mode ? shutdown_mode : -2;
    else if (sig == SIGUSR2)
        shutdown_mode = shutdown_mode ? shutdown_mode : -1;
}

int main(void)
{
    if (getpid() != 1)
        return 1;
    openlog("init", 0, LOG_DAEMON);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGUSR2, on_signal);
#ifdef SIGPWR
    signal(SIGPWR, on_signal);
#endif
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    mkdir("/run", 0755);
    mkdir(RUN_DIR, 0755);
    mkdir(REQ_DIR, 0733);
    chmod(REQ_DIR, 01733);
    read_cmdline();
    load_closure(default_target);
    struct unit *target = find_unit(default_target);
    if (!target || !target->loaded) {
        console("init: %s not found, falling back to multi-user.target", default_target);
        snprintf(default_target, sizeof(default_target), "multi-user.target");
        load_closure(default_target);
        target = find_unit(default_target);
    }
    if (!target || !target->loaded)
        emergency_gettys();
    else
        mark_wanted(target);
    for (;;) {
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
            handle_exit(pid, status);
        if (shutdown_mode < 0) {
            int m = -shutdown_mode;
            shutdown_mode = 0;
            begin_shutdown(m);
        }
        poll_requests();
        schedule();
        write_status();
        if (shutdown_mode > 0) {
            bool busy = false;
            for (int i = 0; i < unit_count; i++)
                if (units[i].pid > 0)
                    busy = true;
            if (!busy || now_ms() > shutdown_deadline)
                finish_shutdown();
        }
        usleep(TICK_US);
    }
}
