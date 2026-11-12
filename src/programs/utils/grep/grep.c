#include "../lib/common.h"
#include <ctype.h>
#include <regex.h>

static bool o_invert, o_count, o_list, o_list_missing, o_number, o_only, o_quiet, o_silent;
static bool o_word, o_line, o_fixed, o_icase, o_recursive, o_byte;
static int o_names = -1;
static long o_max = -1;
static long o_after, o_before;
static bool color;

static char **patterns;
static int npatterns;
static regex_t *compiled;
static bool any_match;
static bool had_error;

static void add_pattern(const char *text)
{
    const char *start = text;
    for (;;) {
        const char *nl = strchr(start, '\n');
        size_t len = nl ? (size_t)(nl - start) : strlen(start);
        patterns = realloc(patterns, sizeof(char *) * (size_t)(npatterns + 1));
        patterns[npatterns] = malloc(len + 1);
        memcpy(patterns[npatterns], start, len);
        patterns[npatterns][len] = 0;
        npatterns++;
        if (!nl) break;
        start = nl + 1;
    }
}

static bool word_char(int c) { return isalnum(c) || c == '_'; }

static bool fixed_find(const char *line, const char *pat, size_t from, size_t *so, size_t *eo)
{
    size_t plen = strlen(pat), llen = strlen(line);
    if (plen > llen) return false;
    for (size_t i = from; i + plen <= llen; i++) {
        bool ok = true;
        for (size_t k = 0; k < plen; k++) {
            char a = line[i + k], b = pat[k];
            if (o_icase ? tolower((unsigned char)a) != tolower((unsigned char)b) : a != b) { ok = false; break; }
        }
        if (ok) { *so = i; *eo = i + plen; return true; }
    }
    return false;
}

static bool find_one(int pi, const char *line, size_t from, size_t *so, size_t *eo)
{
    size_t start = from;
    size_t llen = strlen(line);
    while (start <= llen) {
        size_t s, e;
        if (o_fixed) {
            if (!fixed_find(line, patterns[pi], start, &s, &e)) return false;
        } else {
            regmatch_t m[1];
            int flags = start > 0 ? REG_NOTBOL : 0;
            if (regexec(&compiled[pi], line + start, 1, m, flags) != 0) return false;
            s = start + (size_t)m[0].rm_so;
            e = start + (size_t)m[0].rm_eo;
        }
        bool ok = true;
        if (o_line) ok = s == 0 && e == llen;
        else if (o_word) ok = (s == 0 || !word_char((unsigned char)line[s - 1])) &&
                              (e >= llen || !word_char((unsigned char)line[e])) && e > s;
        if (ok) { *so = s; *eo = e; return true; }
        if (o_line) return false;
        start = s + 1;
    }
    return false;
}

static bool find_any(const char *line, size_t from, size_t *so, size_t *eo)
{
    bool found = false;
    size_t bs = 0, be = 0;
    for (int i = 0; i < npatterns; i++) {
        size_t s, e;
        if (find_one(i, line, from, &s, &e)) {
            if (!found || s < bs || (s == bs && e > be)) { bs = s; be = e; found = true; }
        }
    }
    if (found) { *so = bs; *eo = be; }
    return found;
}

static void print_prefix(const char *name, long lineno, char sep, long offset)
{
    if (o_names > 0) {
        if (color) printf(C_MAGENTA "%s" C_CYAN "%c" C_RESET, name, sep);
        else printf("%s%c", name, sep);
    }
    if (o_number) {
        if (color) printf(C_GREEN "%ld" C_CYAN "%c" C_RESET, lineno, sep);
        else printf("%ld%c", lineno, sep);
    }
    if (o_byte) {
        if (color) printf(C_GREEN "%ld" C_CYAN "%c" C_RESET, offset, sep);
        else printf("%ld%c", offset, sep);
    }
}

static void print_line(const char *line, bool matched)
{
    if (!color || !matched || o_invert) {
        fputs(line, stdout);
        putchar('\n');
        return;
    }
    size_t pos = 0, so, eo;
    size_t len = strlen(line);
    while (pos <= len && find_any(line, pos, &so, &eo)) {
        if (eo == so) {
            if (so >= len) break;
            fwrite(line + pos, 1, so + 1 - pos, stdout);
            pos = so + 1;
            continue;
        }
        fwrite(line + pos, 1, so - pos, stdout);
        printf(C_BOLD C_RED "%.*s" C_RESET, (int)(eo - so), line + so);
        pos = eo;
    }
    fputs(line + pos, stdout);
    putchar('\n');
}

struct ring {
    char **lines;
    long *numbers;
    long *offsets;
    int head, count;
};

static void grep_stream(FILE *in, const char *name)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    long lineno = 0, matches = 0, offset = 0;
    long after_left = 0;
    long last_printed = 0;
    struct ring ring = {0};
    if (o_before > 0) {
        ring.lines = calloc((size_t)o_before, sizeof(char *));
        ring.numbers = calloc((size_t)o_before, sizeof(long));
        ring.offsets = calloc((size_t)o_before, sizeof(long));
    }
    bool context = (o_before > 0 || o_after > 0) && !o_count && !o_list && !o_list_missing && !o_quiet && !o_only;
    while ((len = util_getline(&line, &cap, in)) >= 0) {
        lineno++;
        long line_offset = offset;
        offset += len;
        if (len > 0 && line[len - 1] == '\n') line[--len] = 0;
        if (len > 0 && line[len - 1] == '\r') line[--len] = 0;
        size_t so, eo;
        bool hit = find_any(line, 0, &so, &eo);
        bool selected = hit != o_invert;
        if (selected) {
            matches++;
            any_match = true;
            if (o_quiet) break;
            if (o_list || o_list_missing) break;
            if (!o_count) {
                if (context) {
                    long first_ctx = lineno - ring.count;
                    if (last_printed && first_ctx > last_printed + 1) {
                        if (color) puts(C_CYAN "--" C_RESET);
                        else puts("--");
                    }
                    for (int i = 0; i < ring.count; i++) {
                        int idx = (ring.head + i) % (int)o_before;
                        print_prefix(name, ring.numbers[idx], '-', ring.offsets[idx]);
                        fputs(ring.lines[idx], stdout);
                        putchar('\n');
                    }
                    ring.count = 0;
                }
                if (o_only && !o_invert) {
                    size_t pos = 0;
                    size_t llen = strlen(line);
                    while (pos <= llen && find_any(line, pos, &so, &eo)) {
                        if (eo == so) { pos = so + 1; continue; }
                        print_prefix(name, lineno, ':', line_offset + (long)so);
                        if (color) printf(C_BOLD C_RED "%.*s" C_RESET "\n", (int)(eo - so), line + so);
                        else printf("%.*s\n", (int)(eo - so), line + so);
                        pos = eo;
                    }
                } else {
                    print_prefix(name, lineno, ':', line_offset);
                    print_line(line, hit);
                }
                last_printed = lineno;
                after_left = o_after;
            }
            if (o_max >= 0 && matches >= o_max) break;
        } else if (context) {
            if (after_left > 0) {
                print_prefix(name, lineno, '-', line_offset);
                fputs(line, stdout);
                putchar('\n');
                after_left--;
                last_printed = lineno;
            } else if (o_before > 0) {
                int idx;
                if (ring.count < o_before) idx = (ring.head + ring.count++) % (int)o_before;
                else { idx = ring.head; ring.head = (ring.head + 1) % (int)o_before; }
                free(ring.lines[idx]);
                ring.lines[idx] = strdup(line);
                ring.numbers[idx] = lineno;
                ring.offsets[idx] = line_offset;
            }
        }
    }
    if (o_count) {
        if (o_names > 0) {
            if (color) printf(C_MAGENTA "%s" C_CYAN ":" C_RESET, name);
            else printf("%s:", name);
        }
        printf("%ld\n", matches);
    }
    if (o_list && matches) {
        if (color) printf(C_MAGENTA "%s" C_RESET "\n", name);
        else printf("%s\n", name);
    }
    if (o_list_missing && !matches) printf("%s\n", name);
    if (ring.lines) {
        for (long i = 0; i < o_before; i++) free(ring.lines[i]);
        free(ring.lines);
        free(ring.numbers);
        free(ring.offsets);
    }
    free(line);
}

static bool looks_binary(FILE *f)
{
    unsigned char buf[512];
    size_t n = fread(buf, 1, sizeof(buf), f);
    rewind(f);
    for (size_t i = 0; i < n; i++)
        if (buf[i] == 0) return true;
    return false;
}

static void grep_path(const char *path, bool top)
{
    struct stat st;
    if (strcmp(path, "-") && stat(path, &st) != 0) {
        if (!o_silent) util_err("%s: %s", path, strerror(errno));
        had_error = true;
        return;
    }
    if (strcmp(path, "-") && S_ISDIR(st.st_mode)) {
        if (!o_recursive) {
            if (!o_silent) util_err("%s: Is a directory", path);
            return;
        }
        DIR *dir = opendir(path);
        if (!dir) {
            if (!o_silent) util_err("%s: %s", path, strerror(errno));
            had_error = true;
            return;
        }
        struct dirent *d;
        char **names = NULL;
        int n = 0;
        while ((d = readdir(dir))) {
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
            names = realloc(names, sizeof(char *) * (size_t)(n + 1));
            names[n++] = strdup(d->d_name);
        }
        closedir(dir);
        for (int i = 0; i < n; i++) {
            char full[UTIL_PATH];
            util_join(full, sizeof(full), path, names[i]);
            grep_path(full, false);
            free(names[i]);
        }
        free(names);
        return;
    }
    (void)top;
    FILE *f = util_fopen(path);
    if (!f) {
        if (!o_silent) util_err("%s: %s", path, strerror(errno));
        had_error = true;
        return;
    }
    const char *label = strcmp(path, "-") ? path : "(standard input)";
    if (f != stdin && looks_binary(f) && !o_count && !o_list && !o_list_missing && !o_quiet) {
        bool saved_any = any_match;
        char *line = NULL;
        size_t cap = 0;
        bool hit = false;
        while (util_getline(&line, &cap, f) >= 0) {
            size_t len = strlen(line);
            if (len && line[len - 1] == '\n') line[len - 1] = 0;
            size_t so, eo;
            if (find_any(line, 0, &so, &eo) != o_invert) { hit = true; break; }
        }
        free(line);
        if (hit) {
            printf("Binary file %s matches\n", label);
            any_match = true;
        } else any_match = saved_any;
        util_fclose(f);
        return;
    }
    grep_stream(f, label);
    util_fclose(f);
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    bool extended = !strcmp(prog, "egrep");
    if (!strcmp(prog, "fgrep")) o_fixed = true;
    const char *color_when = NULL;
    bool have_e = false;
    struct opts o;
    opts_begin(&o, argc, argv);
    int c;
    while ((c = opts_next(&o, "e:f:ivclLnohHqsrRwxEFGPm:A:B:C:bZ#")) != -1) {
        switch (c) {
        case 'e': add_pattern(o.arg); have_e = true; break;
        case 'f': {
            FILE *pf = util_fopen(o.arg);
            if (!pf) return util_perr(o.arg), 2;
            char *line = NULL;
            size_t cap = 0;
            ssize_t len;
            while ((len = util_getline(&line, &cap, pf)) >= 0) {
                if (len && line[len - 1] == '\n') line[len - 1] = 0;
                add_pattern(line);
            }
            free(line);
            util_fclose(pf);
            have_e = true;
            break;
        }
        case 'i': o_icase = true; break;
        case 'v': o_invert = true; break;
        case 'c': o_count = true; break;
        case 'l': o_list = true; break;
        case 'L': if (o.name) {
                if (opts_long(&o, "color") || opts_long(&o, "colour")) color_when = o.value ? o.value : "always";
                else if (opts_long(&o, "ignore-case")) o_icase = true;
                else if (opts_long(&o, "invert-match")) o_invert = true;
                else if (opts_long(&o, "count")) o_count = true;
                else if (opts_long(&o, "line-number")) o_number = true;
                else if (opts_long(&o, "only-matching")) o_only = true;
                else if (opts_long(&o, "recursive")) o_recursive = true;
                else if (opts_long(&o, "word-regexp")) o_word = true;
                else if (opts_long(&o, "line-regexp")) o_line = true;
                else if (opts_long(&o, "quiet") || opts_long(&o, "silent")) o_quiet = true;
                else if (opts_long(&o, "files-with-matches")) o_list = true;
                else if (opts_long(&o, "files-without-match")) o_list_missing = true;
                else if (opts_long(&o, "extended-regexp")) extended = true;
                else if (opts_long(&o, "fixed-strings")) o_fixed = true;
                else if (opts_long(&o, "no-filename")) o_names = 0;
                else if (opts_long(&o, "with-filename")) o_names = 1;
                else if (opts_long(&o, "help")) {
                    util_usage("[OPTION]... PATTERN [FILE]...");
                    return 0;
                } else return util_bad_option(&o, c);
            } else o_list_missing = true;
            break;
        case 'n': o_number = true; break;
        case 'o': o_only = true; break;
        case 'h': o_names = 0; break;
        case 'H': o_names = 1; break;
        case 'q': o_quiet = true; break;
        case 's': o_silent = true; break;
        case 'r': case 'R': o_recursive = true; break;
        case 'w': o_word = true; break;
        case 'x': o_line = true; break;
        case 'E': case 'P': extended = true; o_fixed = false; break;
        case 'F': o_fixed = true; break;
        case 'G': extended = false; o_fixed = false; break;
        case 'b': o_byte = true; break;
        case 'Z': break;
        case 'm': o_max = atol(o.arg); break;
        case 'A': o_after = atol(o.arg); break;
        case 'B': o_before = atol(o.arg); break;
        case 'C': o_after = o_before = atol(o.arg); break;
        case '#': o_after = o_before = atol(o.arg); break;
        default: return util_bad_option(&o, c);
        }
    }
    int first_file = 0;
    if (!have_e) {
        if (!o.count) {
            fprintf(stderr, "Usage: %s [OPTION]... PATTERN [FILE]...\nTry 'man grep' for more information.\n", prog);
            return 2;
        }
        add_pattern(o.operands[0]);
        first_file = 1;
    }
    if (!o_fixed) {
        compiled = calloc((size_t)npatterns, sizeof(regex_t));
        int flags = (extended ? REG_EXTENDED : 0) | (o_icase ? REG_ICASE : 0);
        for (int i = 0; i < npatterns; i++) {
            int err = regcomp(&compiled[i], patterns[i], flags);
            if (err) {
                char msg[128];
                regerror(err, &compiled[i], msg, sizeof(msg));
                util_err("%s: %s", patterns[i], msg);
                return 2;
            }
        }
    }
    color = util_color(1, color_when);
    int nfiles = o.count - first_file;
    if (o_names < 0) o_names = (nfiles > 1 || o_recursive) ? 1 : 0;
    if (nfiles == 0) {
        if (o_recursive) grep_path(".", true);
        else grep_path("-", true);
    }
    for (int i = first_file; i < o.count; i++) {
        grep_path(o.operands[i], true);
        if (o_quiet && any_match) break;
    }
    fflush(stdout);
    if (had_error && !(o_quiet && any_match)) return 2;
    return any_match ? 0 : 1;
}
