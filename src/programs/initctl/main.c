#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RUN_DIR "/run/init"
#define REQ_DIR "/run/init/req"
#define ADMIN_DIR "/etc/init/system"

static const char *const unit_dirs[] = {"/etc/init/system", "/run/init/system", "/usr/lib/init/system"};
#define UNIT_DIR_COUNT (sizeof(unit_dirs) / sizeof(unit_dirs[0]))

static const char *prog = "initctl";

struct row {
    char name[64];
    char state[16];
    char enabled[12];
    int pid;
    long since;
    char result[48];
    char path[160];
    int restarts;
    char exec[256];
    char description[96];
};

static int fail(const char *fmt, const char *arg)
{
    fprintf(stderr, "%s: ", prog);
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    return 1;
}

static void full_name(const char *in, char *out, size_t cap)
{
    if (strchr(in, '.'))
        snprintf(out, cap, "%s", in);
    else
        snprintf(out, cap, "%s.service", in);
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    size_t got;
    while (buf && (got = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += got;
        if (n + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    fclose(f);
    if (buf)
        buf[n] = 0;
    return buf;
}

static int load_rows(struct row **out)
{
    char *text = read_all(RUN_DIR "/units");
    if (!text) {
        *out = NULL;
        return -1;
    }
    int cap = 64, n = 0;
    struct row *rows = calloc((size_t)cap, sizeof(*rows));
    for (char *line = strtok(text, "\n"); line && rows; line = strtok(NULL, "\n")) {
        char *f[10];
        int k = 0;
        for (char *p = line; k < 10;) {
            f[k++] = p;
            char *tab = strchr(p, '\t');
            if (!tab)
                break;
            *tab = 0;
            p = tab + 1;
        }
        if (k < 10)
            continue;
        if (n == cap) {
            cap *= 2;
            rows = realloc(rows, (size_t)cap * sizeof(*rows));
            if (!rows)
                break;
        }
        struct row *r = &rows[n++];
        memset(r, 0, sizeof(*r));
        snprintf(r->name, sizeof(r->name), "%s", f[0]);
        snprintf(r->state, sizeof(r->state), "%s", f[1]);
        snprintf(r->enabled, sizeof(r->enabled), "%s", f[2]);
        r->pid = atoi(f[3]);
        r->since = atol(f[4]);
        snprintf(r->result, sizeof(r->result), "%s", f[5]);
        snprintf(r->path, sizeof(r->path), "%s", f[6]);
        r->restarts = atoi(f[7]);
        snprintf(r->exec, sizeof(r->exec), "%s", f[8]);
        snprintf(r->description, sizeof(r->description), "%s", f[9]);
    }
    free(text);
    *out = rows;
    return rows ? n : -1;
}

static const struct row *find_row(const struct row *rows, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(rows[i].name, name))
            return &rows[i];
    return NULL;
}

static int request(const char *text, char *answer, size_t cap)
{
    if (geteuid() != 0) {
        snprintf(answer, cap, "error: permission denied (run as root)\n");
        return -1;
    }
    char req[96], tmp[104], ans[96];
    long id = (long)getpid();
    snprintf(req, sizeof(req), REQ_DIR "/%ld.req", id);
    snprintf(tmp, sizeof(tmp), REQ_DIR "/%ld.new", id);
    snprintf(ans, sizeof(ans), REQ_DIR "/%ld.ans", id);
    unlink(ans);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        snprintf(answer, cap, "error: init is not running (%s)\n", strerror(errno));
        return -1;
    }
    (void)write(fd, text, strlen(text));
    close(fd);
    if (rename(tmp, req) < 0) {
        unlink(tmp);
        snprintf(answer, cap, "error: cannot send the request\n");
        return -1;
    }
    for (int i = 0; i < 300; i++) {
        usleep(50000);
        int a = open(ans, O_RDONLY);
        if (a < 0)
            continue;
        ssize_t n = read(a, answer, cap - 1);
        close(a);
        unlink(ans);
        answer[n > 0 ? n : 0] = 0;
        return strncmp(answer, "error", 5) == 0 ? -1 : 0;
    }
    unlink(req);
    snprintf(answer, cap, "error: init did not answer\n");
    return -1;
}

static int simple_request(const char *text)
{
    char answer[512];
    int rc = request(text, answer, sizeof(answer));
    if (rc < 0 || strcmp(answer, "ok\n") != 0)
        fputs(answer, rc < 0 ? stderr : stdout);
    return rc < 0 ? 1 : 0;
}

static int wait_settled(char **names, int count)
{
    int status = 0;
    for (int tick = 0; tick < 100; tick++) {
        struct row *rows;
        int n = load_rows(&rows);
        bool busy = false;
        for (int i = 0; i < count && n >= 0; i++) {
            char name[64];
            full_name(names[i], name, sizeof(name));
            const struct row *r = find_row(rows, n, name);
            if (r && (!strcmp(r->state, "waiting") || !strcmp(r->state, "activating") ||
                      !strcmp(r->state, "deactivating")))
                busy = true;
        }
        if (!busy || tick == 99) {
            for (int i = 0; i < count && n >= 0; i++) {
                char name[64];
                full_name(names[i], name, sizeof(name));
                const struct row *r = find_row(rows, n, name);
                if (r && !strcmp(r->state, "failed")) {
                    fprintf(stderr, "%s: %s failed: %s, see '%s status %s'\n", prog, name, r->result, prog, name);
                    status = 1;
                }
            }
            free(rows);
            return status;
        }
        free(rows);
        usleep(100000);
    }
    return status;
}

static int unit_request(const char *verb, char **names, int count, bool wait)
{
    char text[1024];
    size_t used = (size_t)snprintf(text, sizeof(text), "%s", verb);
    for (int i = 0; i < count && used < sizeof(text); i++)
        used += (size_t)snprintf(text + used, sizeof(text) - used, " %s", names[i]);
    if (used < sizeof(text))
        snprintf(text + used, sizeof(text) - used, "\n");
    int rc = simple_request(text);
    if (rc == 0 && wait)
        rc = wait_settled(names, count);
    return rc;
}

static bool find_unit_file(const char *name, char *path, size_t cap)
{
    char templ[96] = "";
    const char *at = strchr(name, '@'), *dot = strrchr(name, '.');
    if (at && dot && dot > at)
        snprintf(templ, sizeof(templ), "%.*s@%s", (int)(at - name), name, dot);
    for (size_t d = 0; d < UNIT_DIR_COUNT; d++) {
        const char *c[2] = {name, templ[0] ? templ : NULL};
        for (int k = 0; k < 2 && c[k]; k++) {
            struct stat st;
            snprintf(path, cap, "%s/%s", unit_dirs[d], c[k]);
            if (stat(path, &st) == 0 && st.st_size > 0)
                return true;
        }
    }
    return false;
}

static int wanted_by(const char *path, char out[4][64])
{
    char *text = read_all(path);
    if (!text)
        return 0;
    int n = 0;
    bool install = false;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '[') {
            install = !strncmp(line, "[Install]", 9);
            continue;
        }
        if (!install || strncmp(line, "WantedBy", 8) != 0)
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        for (char *w = strtok(eq + 1, " \t"); w && n < 4; w = strtok(NULL, " \t")) {
            if (!*w)
                continue;
            snprintf(out[n++], 64, "%s", w);
        }
        break;
    }
    free(text);
    return n;
}

static int mkdirs(const char *path)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(buf, 0755);
            *p = '/';
        }
    return mkdir(buf, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int set_enabled(char **names, int count, bool enable, bool now)
{
    if (geteuid() != 0)
        return fail("%s", "permission denied (run as root)");
    int status = 0;
    for (int i = 0; i < count; i++) {
        char name[64], path[256], targets[4][64];
        full_name(names[i], name, sizeof(name));
        if (!find_unit_file(name, path, sizeof(path))) {
            status = fail("unit %s not found", name);
            continue;
        }
        if (strchr(name, '@') && strchr(name, '@')[1] == '.') {
            status = fail("%s is a template, give an instance like name@arg.service", name);
            continue;
        }
        int n = wanted_by(path, targets);
        if (!n) {
            printf("%s has no [Install] WantedBy=, it is started by other units only\n", name);
            continue;
        }
        for (int t = 0; t < n; t++) {
            char dir[192], marker[260];
            snprintf(dir, sizeof(dir), ADMIN_DIR "/%s.wants", targets[t]);
            snprintf(marker, sizeof(marker), "%s/%s", dir, name);
            if (enable) {
                if (mkdirs(dir) < 0) {
                    status = fail("cannot create %s", dir);
                    continue;
                }
                FILE *mf = fopen(marker, "w");
                if (!mf) {
                    status = fail("cannot create %s", marker);
                    continue;
                }
                fprintf(mf, "# enabled: %s is started by %s\n", name, targets[t]);
                fclose(mf);
                printf("Enabled %s in %s\n", name, targets[t]);
            } else {
                if (unlink(marker) == 0)
                    printf("Removed %s\n", marker);
                for (size_t d = 1; d < UNIT_DIR_COUNT; d++) {
                    char other[260];
                    snprintf(other, sizeof(other), "%s/%s.wants/%s", unit_dirs[d], targets[t], name);
                    if (access(other, F_OK) == 0)
                        printf("note: %s is enabled by the system in %s, use 'mask' to block it\n", name,
                               unit_dirs[d]);
                }
            }
        }
    }
    simple_request("daemon-reload\n");
    if (now && status == 0)
        return unit_request(enable ? "start" : "stop", names, count, true);
    return status;
}

static int set_masked(char **names, int count, bool mask)
{
    if (geteuid() != 0)
        return fail("%s", "permission denied (run as root)");
    mkdirs(ADMIN_DIR);
    int status = 0;
    for (int i = 0; i < count; i++) {
        char name[64], path[192];
        full_name(names[i], name, sizeof(name));
        snprintf(path, sizeof(path), ADMIN_DIR "/%s", name);
        if (mask) {
            struct stat st;
            if (stat(path, &st) == 0 && st.st_size > 0) {
                status = fail("%s has a real unit file in " ADMIN_DIR ", remove it first", name);
                continue;
            }
            int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                status = fail("cannot create %s", path);
                continue;
            }
            close(fd);
            printf("Masked %s\n", name);
        } else {
            struct stat st;
            if (stat(path, &st) == 0 && st.st_size == 0 && unlink(path) == 0)
                printf("Unmasked %s\n", name);
        }
    }
    simple_request("daemon-reload\n");
    return status;
}

static void print_since(long seconds)
{
    if (seconds < 60)
        printf("%lds ago", seconds);
    else if (seconds < 3600)
        printf("%ldmin %lds ago", seconds / 60, seconds % 60);
    else
        printf("%ldh %ldmin ago", seconds / 3600, seconds % 3600 / 60);
}

static void tail_file(const char *path, int lines)
{
    char *text = read_all(path);
    if (!text)
        return;
    size_t n = strlen(text);
    int seen = 0;
    size_t start = n;
    while (start > 0) {
        if (text[start - 1] == '\n' && start != n && ++seen >= lines)
            break;
        start--;
    }
    fputs(text + start, stdout);
    if (n && text[n - 1] != '\n')
        putchar('\n');
    free(text);
}

static int show_status(char **names, int count)
{
    struct row *rows;
    int n = load_rows(&rows);
    if (n < 0)
        return fail("%s", "init is not running (no " RUN_DIR "/units)");
    int status = 0;
    if (!count) {
        int active = 0, failed = 0;
        for (int i = 0; i < n; i++) {
            active += !strcmp(rows[i].state, "active");
            failed += !strcmp(rows[i].state, "failed");
        }
        printf("State: %s\nUnits: %d loaded, %d active, %d failed\n", failed ? "degraded" : "running", n, active,
               failed);
        for (int i = 0; i < n; i++)
            if (!strcmp(rows[i].state, "failed"))
                printf("  failed: %s (%s)\n", rows[i].name, rows[i].result);
        free(rows);
        return failed ? 1 : 0;
    }
    for (int i = 0; i < count; i++) {
        char name[64];
        full_name(names[i], name, sizeof(name));
        const struct row *r = find_row(rows, n, name);
        if (i)
            putchar('\n');
        if (!r) {
            char path[256];
            bool exists = find_unit_file(name, path, sizeof(path));
            printf("o %s\n     Loaded: %s\n     Active: inactive (not loaded by init)\n", name,
                   exists ? path : "not-found");
            status = 3;
            continue;
        }
        bool active = !strcmp(r->state, "active");
        printf("%s %s - %s\n", active ? "*" : !strcmp(r->state, "failed") ? "x" : "o", r->name, r->description);
        printf("     Loaded: %s (%s)\n", r->path, r->enabled);
        printf("     Active: %s", r->state);
        if (strcmp(r->result, "-") != 0)
            printf(" (%s)", r->result);
        printf(" since ");
        print_since(r->since);
        putchar('\n');
        if (r->pid > 0)
            printf("   Main PID: %d\n", r->pid);
        if (strcmp(r->exec, "-") != 0)
            printf("    Command: %s\n", r->exec);
        if (r->restarts)
            printf("   Restarts: %d\n", r->restarts);
        char log[160];
        snprintf(log, sizeof(log), "/var/log/init/%s.log", r->name);
        if (access(log, R_OK) == 0) {
            putchar('\n');
            tail_file(log, 10);
        }
        if (!active)
            status = 3;
    }
    free(rows);
    return status;
}

static int list_units(bool all)
{
    struct row *rows;
    int n = load_rows(&rows);
    if (n < 0)
        return fail("%s", "init is not running (no " RUN_DIR "/units)");
    printf("%-30s %-13s %-9s %6s  %s\n", "UNIT", "STATE", "ENABLED", "PID", "DESCRIPTION");
    int shown = 0;
    for (int i = 0; i < n; i++) {
        const struct row *r = &rows[i];
        if (!all && !strcmp(r->state, "inactive") && strcmp(r->result, "success") != 0)
            continue;
        char pid[12] = "-";
        if (r->pid > 0)
            snprintf(pid, sizeof(pid), "%d", r->pid);
        printf("%-30s %-13s %-9s %6s  %s\n", r->name, r->state, r->enabled, pid, r->description);
        shown++;
    }
    printf("\n%d unit(s) listed%s.\n", shown, all ? "" : ", -a shows all");
    free(rows);
    return 0;
}

static int list_files(void)
{
    printf("%-34s %s\n", "UNIT FILE", "DIRECTORY");
    for (size_t d = 0; d < UNIT_DIR_COUNT; d++) {
        DIR *dp = opendir(unit_dirs[d]);
        if (!dp)
            continue;
        struct dirent *e;
        while ((e = readdir(dp)) != NULL) {
            size_t l = strlen(e->d_name);
            if ((l > 8 && !strcmp(e->d_name + l - 8, ".service")) || (l > 7 && !strcmp(e->d_name + l - 7, ".target")))
                printf("%-34s %s\n", e->d_name, unit_dirs[d]);
        }
        closedir(dp);
    }
    return 0;
}

static int cat_unit(char **names, int count)
{
    int status = 0;
    for (int i = 0; i < count; i++) {
        char name[64], path[256];
        full_name(names[i], name, sizeof(name));
        if (!find_unit_file(name, path, sizeof(path))) {
            status = fail("unit %s not found", name);
            continue;
        }
        printf("# %s\n", path);
        char *text = read_all(path);
        if (text) {
            fputs(text, stdout);
            free(text);
        }
    }
    return status;
}

static int show_log(char **names, int count, int lines)
{
    if (!count)
        return fail("%s", "give a unit name");
    for (int i = 0; i < count; i++) {
        char name[64], path[160];
        full_name(names[i], name, sizeof(name));
        snprintf(path, sizeof(path), "/var/log/init/%s.log", name);
        if (access(path, R_OK) != 0) {
            fail("no log for %s", name);
            continue;
        }
        tail_file(path, lines);
    }
    return 0;
}

static void usage(void)
{
    printf("usage: %s COMMAND [UNIT...]\n\n"
           "  list [-a]            loaded units (-a also inactive ones)\n"
           "  list-files           unit files on disk\n"
           "  status [UNIT...]     state of the system or of units\n"
           "  start|stop|restart UNIT...\n"
           "  kill UNIT...         send SIGKILL to the main process\n"
           "  enable|disable [--now] UNIT...\n"
           "  mask|unmask UNIT...  block a unit completely\n"
           "  cat UNIT...          show the unit file\n"
           "  log [-n N] UNIT...   last lines of the unit's output\n"
           "  is-active UNIT       exit 0 when active\n"
           "  daemon-reload        read unit files again\n"
           "  isolate TARGET       start TARGET and stop everything else\n"
           "  get-default, set-default TARGET\n"
           "  poweroff|reboot|halt\n\n"
           "Unit files: /etc/init/system (admin), /usr/lib/init/system (packages).\n",
           prog);
}

int main(int argc, char **argv)
{
    const char *slash = strrchr(argv[0], '/');
    prog = slash ? slash + 1 : argv[0];
    if (argc < 2) {
        usage();
        return 1;
    }
    const char *cmd = argv[1];
    char *names[64];
    int count = 0, lines = 30;
    bool all = false, now = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--all"))
            all = true;
        else if (!strcmp(argv[i], "--now"))
            now = true;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            lines = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-pager") || !strcmp(argv[i], "-l") || !strcmp(argv[i], "-q"))
            continue;
        else if (count < 64)
            names[count++] = argv[i];
    }
    if (!strcmp(cmd, "list") || !strcmp(cmd, "list-units"))
        return list_units(all);
    if (!strcmp(cmd, "list-files") || !strcmp(cmd, "list-unit-files"))
        return list_files();
    if (!strcmp(cmd, "status"))
        return show_status(names, count);
    if (!strcmp(cmd, "cat"))
        return cat_unit(names, count);
    if (!strcmp(cmd, "log"))
        return show_log(names, count, lines);
    if (!strcmp(cmd, "is-active") || !strcmp(cmd, "is-failed")) {
        struct row *rows;
        int n = load_rows(&rows);
        int rc = 1;
        for (int i = 0; i < count && n >= 0; i++) {
            char name[64];
            full_name(names[i], name, sizeof(name));
            const struct row *r = find_row(rows, n, name);
            const char *state = r ? r->state : "inactive";
            puts(state);
            if (!strcmp(state, cmd[3] == 'a' ? "active" : "failed"))
                rc = 0;
        }
        free(rows);
        return rc;
    }
    if (!strcmp(cmd, "start") || !strcmp(cmd, "stop") || !strcmp(cmd, "restart") || !strcmp(cmd, "kill")) {
        if (!count)
            return fail("%s", "give a unit name");
        return unit_request(cmd, names, count, strcmp(cmd, "kill") != 0);
    }
    if (!strcmp(cmd, "enable") || !strcmp(cmd, "disable")) {
        if (!count)
            return fail("%s", "give a unit name");
        return set_enabled(names, count, cmd[0] == 'e', now);
    }
    if (!strcmp(cmd, "mask") || !strcmp(cmd, "unmask")) {
        if (!count)
            return fail("%s", "give a unit name");
        return set_masked(names, count, cmd[0] == 'm');
    }
    if (!strcmp(cmd, "daemon-reload"))
        return simple_request("daemon-reload\n");
    if (!strcmp(cmd, "isolate")) {
        if (count != 1)
            return fail("%s", "give one target");
        char text[128], name[64];
        full_name(names[0], name, sizeof(name));
        snprintf(text, sizeof(text), "isolate %s\n", name);
        return simple_request(text);
    }
    if (!strcmp(cmd, "get-default")) {
        char *t = read_all("/etc/init/default.target");
        printf("%s\n", t ? strtok(t, " \t\n") : "multi-user.target");
        free(t);
        return 0;
    }
    if (!strcmp(cmd, "set-default")) {
        if (count != 1 || !strstr(names[0], ".target"))
            return fail("%s", "give one target, e.g. multi-user.target");
        char path[256];
        if (!find_unit_file(names[0], path, sizeof(path)))
            return fail("unit %s not found", names[0]);
        mkdirs("/etc/init");
        FILE *f = fopen("/etc/init/default.target", "w");
        if (!f)
            return fail("%s", "cannot write /etc/init/default.target");
        fprintf(f, "%s\n", names[0]);
        fclose(f);
        printf("Default target is %s\n", names[0]);
        return 0;
    }
    if (!strcmp(cmd, "poweroff") || !strcmp(cmd, "reboot") || !strcmp(cmd, "halt")) {
        char text[32];
        snprintf(text, sizeof(text), "%s\n", cmd);
        return simple_request(text);
    }
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help")) {
        usage();
        return 0;
    }
    return fail("unknown command '%s'", cmd);
}
