#include "../lib/common.h"
#include <time.h>
#include <utime.h>

static bool parse_stamp(const char *text, time_t *out)
{
    int len = (int)strlen(text);
    char digits[16];
    int seconds = 0;
    const char *dot = strchr(text, '.');
    if (dot) {
        seconds = atoi(dot + 1);
        len = (int)(dot - text);
    }
    if (len != 8 && len != 10 && len != 12) return false;
    memcpy(digits, text, (size_t)len);
    digits[len] = 0;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    time_t now = time(NULL);
    struct tm *cur = localtime(&now);
    int pos = 0;
    if (len == 12) {
        tm.tm_year = (digits[0] - '0') * 1000 + (digits[1] - '0') * 100 + (digits[2] - '0') * 10 + (digits[3] - '0') - 1900;
        pos = 4;
    } else if (len == 10) {
        int yy = (digits[0] - '0') * 10 + (digits[1] - '0');
        tm.tm_year = yy < 69 ? yy + 100 : yy;
        pos = 2;
    } else tm.tm_year = cur ? cur->tm_year : 126;
    tm.tm_mon = (digits[pos] - '0') * 10 + (digits[pos + 1] - '0') - 1;
    tm.tm_mday = (digits[pos + 2] - '0') * 10 + (digits[pos + 3] - '0');
    tm.tm_hour = (digits[pos + 4] - '0') * 10 + (digits[pos + 5] - '0');
    tm.tm_min = (digits[pos + 6] - '0') * 10 + (digits[pos + 7] - '0');
    tm.tm_sec = seconds;
    *out = mktime(&tm);
    return true;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool no_create = false, only_a = false, only_m = false;
    time_t stamp = time(NULL);
    const char *ref = NULL;
    int c;
    while ((c = opts_next(&o, "camt:r:d:")) != -1) {
        switch (c) {
        case 'c': no_create = true; break;
        case 'a': only_a = true; break;
        case 'm': only_m = true; break;
        case 't': if (!parse_stamp(o.arg, &stamp)) return util_err("invalid date format '%s'", o.arg); break;
        case 'd': {
            char *end;
            long v = strtol(o.arg[0] == '@' ? o.arg + 1 : o.arg, &end, 10);
            if (*end) return util_err("invalid date format '%s' (use @SECONDS)", o.arg);
            stamp = (time_t)v;
            break;
        }
        case 'r': ref = o.arg; break;
        case 'L':
            if (opts_long(&o, "no-create")) { no_create = true; break; }
            if (opts_long(&o, "help")) { util_usage("[-cam] [-t STAMP] [-r FILE] [-d @SECONDS] FILE..."); return 0; }
            return util_bad_option(&o, c);
        default: return util_bad_option(&o, c);
        }
    }
    if (!o.count) return util_err("missing file operand");
    time_t ref_a = stamp, ref_m = stamp;
    if (ref) {
        struct stat st;
        if (stat(ref, &st) != 0) return util_perr(ref);
        ref_a = st.st_atime;
        ref_m = st.st_mtime;
    }
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        const char *path = o.operands[i];
        struct stat st;
        bool exists = stat(path, &st) == 0;
        if (!exists) {
            if (no_create) continue;
            int fd = open(path, O_WRONLY | O_CREAT, 0644);
            if (fd < 0) {
                rc = util_err("cannot touch '%s': %s", path, strerror(errno));
                continue;
            }
            close(fd);
            if (stat(path, &st) != 0) memset(&st, 0, sizeof(st));
        }
        struct utimbuf times;
        times.actime = (only_m && !only_a) ? st.st_atime : ref_a;
        times.modtime = (only_a && !only_m) ? st.st_mtime : ref_m;
        if (utime(path, &times) != 0) rc = util_err("setting times of '%s': %s", path, strerror(errno));
    }
    return rc;
}
