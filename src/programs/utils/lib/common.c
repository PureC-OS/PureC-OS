#include "common.h"
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

const char *prog = "util";

void util_init(int argc, char **argv)
{
    if (argc > 0 && argv[0]) prog = util_base(argv[0]);
}

int util_err(const char *fmt, ...)
{
    fflush(stdout);
    fprintf(stderr, "%s: ", prog);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return 1;
}

int util_perr(const char *what)
{
    int saved = errno;
    fflush(stdout);
    if (what) fprintf(stderr, "%s: %s: %s\n", prog, what, strerror(saved));
    else fprintf(stderr, "%s: %s\n", prog, strerror(saved));
    return 1;
}

void util_usage(const char *text)
{
    printf("Usage: %s %s\nSee 'man %s' for details.\n", prog, text, prog);
}

void opts_begin(struct opts *o, int argc, char **argv)
{
    memset(o, 0, sizeof(*o));
    o->argc = argc;
    o->argv = argv;
    o->index = 1;
    o->operands = calloc((size_t)argc + 1, sizeof(char *));
}

int opts_next(struct opts *o, const char *spec)
{
    o->arg = NULL;
    o->name = NULL;
    o->value = NULL;
    for (;;) {
        if (o->sub) {
            char *word = o->argv[o->index];
            char c = word[o->sub++];
            const char *hit = strchr(spec, c);
            if (!hit || c == ':') {
                if (!word[o->sub]) { o->index++; o->sub = 0; }
                o->name = NULL;
                o->arg = word;
                o->value = (const char *)(intptr_t)c;
                return '?';
            }
            if (hit[1] == ':') {
                if (word[o->sub]) o->arg = word + o->sub;
                else if (o->index + 1 < o->argc) o->arg = o->argv[++o->index];
                else {
                    o->index++;
                    o->sub = 0;
                    util_err("option requires an argument -- '%c'", c);
                    return ':';
                }
                o->index++;
                o->sub = 0;
                return c;
            }
            if (!word[o->sub]) { o->index++; o->sub = 0; }
            return c;
        }
        if (o->index >= o->argc) return -1;
        char *word = o->argv[o->index];
        if (o->stop || word[0] != '-' || word[1] == 0) {
            o->operands[o->count++] = word;
            o->index++;
            continue;
        }
        if (word[1] == '-' && word[2] == 0) {
            o->stop = true;
            o->index++;
            continue;
        }
        if (word[1] == '-') {
            static char name[64];
            const char *eq = strchr(word + 2, '=');
            size_t len = eq ? (size_t)(eq - word - 2) : strlen(word + 2);
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, word + 2, len);
            name[len] = 0;
            o->name = name;
            o->value = eq ? eq + 1 : NULL;
            o->arg = word;
            o->index++;
            return 'L';
        }
        if (word[1] >= '0' && word[1] <= '9' && strchr(spec, '#')) {
            o->arg = word + 1;
            o->index++;
            return '#';
        }
        o->sub = 1;
    }
}

bool opts_long(const struct opts *o, const char *name)
{
    return o->name && strcmp(o->name, name) == 0;
}

int util_bad_option(const struct opts *o, int c)
{
    if (c == 'L') util_err("unrecognized option '--%s'", o->name ? o->name : "");
    else if (c == '?') util_err("invalid option -- '%c'", (char)(intptr_t)o->value);
    fprintf(stderr, "Try 'man %s' for more information.\n", prog);
    return 2;
}

bool util_color(int fd, const char *when)
{
    if (when) {
        if (!strcmp(when, "always") || !strcmp(when, "yes") || !strcmp(when, "force")) return true;
        if (!strcmp(when, "never") || !strcmp(when, "no") || !strcmp(when, "none")) return false;
    }
    if (getenv("NO_COLOR")) return false;
    const char *term = getenv("TERM");
    if (term && !strcmp(term, "dumb")) return false;
    return isatty(fd);
}

int util_term_width(void)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 10) return ws.ws_col;
    const char *cols = getenv("COLUMNS");
    if (cols && atoi(cols) > 10) return atoi(cols);
    return 80;
}

int util_term_height(void)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 3) return ws.ws_row;
    const char *rows = getenv("LINES");
    if (rows && atoi(rows) > 3) return atoi(rows);
    return 24;
}

void util_human(char *out, size_t cap, unsigned long long bytes)
{
    static const char units[] = "BKMGTP";
    if (bytes < 1024) {
        snprintf(out, cap, "%llu", bytes);
        return;
    }
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        unit++;
    }
    if (value < 10.0) {
        unsigned long tenths = (unsigned long)(value * 10.0 + 0.999);
        if (tenths >= 100) snprintf(out, cap, "%lu%c", tenths / 10, units[unit]);
        else snprintf(out, cap, "%lu.%lu%c", tenths / 10, tenths % 10, units[unit]);
    } else {
        snprintf(out, cap, "%lu%c", (unsigned long)(value + 0.999), units[unit]);
    }
}

void util_join(char *out, size_t cap, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/') len--;
    if (len == 1 && dir[0] == '/') snprintf(out, cap, "/%s", name);
    else snprintf(out, cap, "%.*s/%s", (int)len, dir, name);
}

const char *util_base(const char *path)
{
    const char *base = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' && c[1]) base = c + 1;
    return base;
}

bool util_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool util_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

void util_mode_string(mode_t mode, char out[11])
{
    out[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l' : S_ISCHR(mode) ? 'c' : S_ISBLK(mode) ? 'b'
           : S_ISFIFO(mode) ? 'p' : S_ISSOCK(mode) ? 's' : '-';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) out[i + 1] = (mode & (0400 >> i)) ? rwx[i] : '-';
    if (mode & S_ISUID) out[3] = (mode & 0100) ? 's' : 'S';
    if (mode & S_ISGID) out[6] = (mode & 0010) ? 's' : 'S';
    if (mode & S_ISVTX) out[9] = (mode & 0001) ? 't' : 'T';
    out[10] = 0;
}

static bool parse_clause(const char *text, size_t len, unsigned int *mode, bool is_dir)
{
    unsigned int who = 0;
    size_t i = 0;
    while (i < len && strchr("ugoa", text[i])) {
        if (text[i] == 'u') who |= 04700;
        else if (text[i] == 'g') who |= 02070;
        else if (text[i] == 'o') who |= 01007;
        else who |= 07777;
        i++;
    }
    if (!who) who = 07777;
    if (i >= len) return false;
    while (i < len) {
        char op = text[i++];
        if (op != '+' && op != '-' && op != '=') return false;
        unsigned int bits = 0;
        while (i < len && !strchr("+-=", text[i])) {
            char c = text[i++];
            if (c == 'r') bits |= 0444;
            else if (c == 'w') bits |= 0222;
            else if (c == 'x') bits |= 0111;
            else if (c == 'X') { if (is_dir || (*mode & 0111)) bits |= 0111; }
            else if (c == 's') bits |= 06000;
            else if (c == 't') bits |= 01000;
            else return false;
        }
        bits &= who;
        if (op == '+') *mode |= bits;
        else if (op == '-') *mode &= ~bits;
        else *mode = (*mode & ~who) | bits;
    }
    return true;
}

bool util_parse_mode(const char *text, unsigned int *mode, bool is_dir)
{
    if (!*text) return false;
    if (text[0] >= '0' && text[0] <= '7') {
        char *end;
        unsigned long v = strtoul(text, &end, 8);
        if (*end) return false;
        *mode = (unsigned int)(v & 07777);
        return true;
    }
    const char *c = text;
    for (;;) {
        const char *comma = strchr(c, ',');
        size_t len = comma ? (size_t)(comma - c) : strlen(c);
        if (!parse_clause(c, len, mode, is_dir)) return false;
        if (!comma) return true;
        c = comma + 1;
    }
}

static bool has_ext(const char *name, const char *const *list)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return false;
    for (; *list; list++)
        if (!strcasecmp(dot + 1, *list)) return true;
    return false;
}

const char *util_name_color(const char *name, mode_t mode)
{
    static const char *const images[] = {"png", "bmp", "jpg", "jpeg", "gif", "ppm", "tga", "svg", "ico", NULL};
    static const char *const archives[] = {"tar", "gz", "tgz", "zip", "xz", "bz2", "7z", "zst", "cpio", "iso", "img", NULL};
    static const char *const audio[] = {"wav", "mp3", "ogg", "flac", "mid", NULL};
    static const char *const source[] = {"c", "h", "cpp", "hpp", "rs", "py", "lua", "s", "asm", "sh", "mk", NULL};
    if (S_ISDIR(mode)) return C_BOLD C_BLUE;
    if (S_ISLNK(mode)) return C_BOLD C_CYAN;
    if (S_ISCHR(mode) || S_ISBLK(mode)) return C_BOLD C_YELLOW;
    if (S_ISFIFO(mode) || S_ISSOCK(mode)) return C_MAGENTA;
    if (mode & 0111) return C_BOLD C_GREEN;
    if (has_ext(name, images)) return C_MAGENTA;
    if (has_ext(name, archives)) return C_BOLD C_RED;
    if (has_ext(name, audio)) return C_CYAN;
    if (has_ext(name, source)) return C_YELLOW;
    return NULL;
}

const char *util_user(uid_t uid, char *buf, size_t cap)
{
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) snprintf(buf, cap, "%s", pw->pw_name);
    else snprintf(buf, cap, "%u", (unsigned)uid);
    return buf;
}

const char *util_group(gid_t gid, char *buf, size_t cap)
{
    struct group *gr = getgrgid(gid);
    if (gr && gr->gr_name) snprintf(buf, cap, "%s", gr->gr_name);
    else snprintf(buf, cap, "%u", (unsigned)gid);
    return buf;
}

FILE *util_fopen(const char *path)
{
    if (!strcmp(path, "-")) return stdin;
    if (util_is_dir(path)) {
        errno = EISDIR;
        return NULL;
    }
    return fopen(path, "r");
}

void util_fclose(FILE *file)
{
    if (file && file != stdin) fclose(file);
}

ssize_t util_getline(char **line, size_t *cap, FILE *file)
{
    size_t len = 0;
    int c;
    if (!*line || !*cap) {
        *cap = 256;
        *line = malloc(*cap);
        if (!*line) return -1;
    }
    while ((c = fgetc(file)) != EOF) {
        if (len + 2 > *cap) {
            size_t ncap = *cap * 2;
            char *n = realloc(*line, ncap);
            if (!n) return -1;
            *line = n;
            *cap = ncap;
        }
        (*line)[len++] = (char)c;
        if (c == '\n') break;
    }
    if (!len && c == EOF) return -1;
    (*line)[len] = 0;
    return (ssize_t)len;
}

int util_copy_stream(FILE *in, FILE *out)
{
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) return -1;
    return ferror(in) ? -1 : 0;
}

bool util_parse_long(const char *text, long *out)
{
    if (!text || !*text) return false;
    char *end;
    long v = strtol(text, &end, 10);
    if (*end) return false;
    *out = v;
    return true;
}

int util_exec_wait(char **argv)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) return util_perr("fork");
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "%s: %s: %s\n", prog, argv[0], strerror(errno));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return util_perr("waitpid");
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
