#include "../lib/common.h"
#include <time.h>

static void show_time(const char *label, time_t t, bool color)
{
    char buf[64];
    struct tm *tm = localtime(&t);
    if (tm) strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %z", tm);
    else snprintf(buf, sizeof(buf), "%ld", (long)t);
    printf("%s%s%s %s\n", color ? C_BOLD : "", label, color ? C_RESET : "", buf);
}

static const char *kind(mode_t m)
{
    if (S_ISDIR(m)) return "directory";
    if (S_ISLNK(m)) return "symbolic link";
    if (S_ISCHR(m)) return "character special file";
    if (S_ISBLK(m)) return "block special file";
    if (S_ISFIFO(m)) return "fifo";
    if (S_ISSOCK(m)) return "socket";
    return "regular file";
}

static void format_custom(const char *fmt, const char *path, const struct stat *st)
{
    char ub[64], gb[64], mode[11];
    for (const char *p = fmt; *p; p++) {
        if (*p == '\\' && p[1] == 'n') { putchar('\n'); p++; continue; }
        if (*p != '%' || !p[1]) { putchar(*p); continue; }
        p++;
        switch (*p) {
        case 'n': fputs(path, stdout); break;
        case 's': printf("%lld", (long long)st->st_size); break;
        case 'a': printf("%o", (unsigned)(st->st_mode & 07777)); break;
        case 'A': util_mode_string(st->st_mode, mode); fputs(mode, stdout); break;
        case 'F': fputs(kind(st->st_mode), stdout); break;
        case 'u': printf("%u", (unsigned)st->st_uid); break;
        case 'U': fputs(util_user(st->st_uid, ub, sizeof(ub)), stdout); break;
        case 'g': printf("%u", (unsigned)st->st_gid); break;
        case 'G': fputs(util_group(st->st_gid, gb, sizeof(gb)), stdout); break;
        case 'i': printf("%lu", (unsigned long)st->st_ino); break;
        case 'h': printf("%lu", (unsigned long)st->st_nlink); break;
        case 'X': printf("%ld", (long)st->st_atime); break;
        case 'Y': printf("%ld", (long)st->st_mtime); break;
        case 'Z': printf("%ld", (long)st->st_ctime); break;
        case '%': putchar('%'); break;
        default: putchar('%'); putchar(*p);
        }
    }
    putchar('\n');
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    const char *format = NULL;
    bool terse = false;
    int c;
    while ((c = opts_next(&o, "c:tL")) != -1) {
        if (c == 'c') format = o.arg;
        else if (c == 't') terse = true;
        else if (c == 'L' && !o.name) continue;
        else if (c == 'L' && opts_long(&o, "format")) format = o.value;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-t] [-c FORMAT] FILE..."); return 0; }
        else return util_bad_option(&o, c);
    }
    if (!o.count) return util_err("missing operand");
    bool color = util_color(1, NULL);
    int rc = 0;
    for (int i = 0; i < o.count; i++) {
        struct stat st;
        if (stat(o.operands[i], &st) != 0) {
            rc = util_err("cannot stat '%s': %s", o.operands[i], strerror(errno));
            continue;
        }
        if (format) { format_custom(format, o.operands[i], &st); continue; }
        if (terse) {
            printf("%s %lld %o %u %u %lu %ld %ld %ld\n", o.operands[i], (long long)st.st_size, (unsigned)st.st_mode,
                   (unsigned)st.st_uid, (unsigned)st.st_gid, (unsigned long)st.st_ino, (long)st.st_atime, (long)st.st_mtime, (long)st.st_ctime);
            continue;
        }
        char mode[11], ub[64], gb[64], hb[32];
        util_mode_string(st.st_mode, mode);
        util_human(hb, sizeof(hb), (unsigned long long)st.st_size);
        printf("%s  File:%s %s\n", color ? C_BOLD : "", color ? C_RESET : "", o.operands[i]);
        printf("%s  Size:%s %-12lld %s(%s)%s  %s\n", color ? C_BOLD : "", color ? C_RESET : "", (long long)st.st_size,
               color ? C_GRAY : "", hb, color ? C_RESET : "", kind(st.st_mode));
        printf("%s Inode:%s %-12lu %sLinks:%s %lu\n", color ? C_BOLD : "", color ? C_RESET : "", (unsigned long)st.st_ino,
               color ? C_BOLD : "", color ? C_RESET : "", (unsigned long)(st.st_nlink ? st.st_nlink : 1));
        printf("%sAccess:%s (%04o/%s)  %sUid:%s (%5u/%8s)  %sGid:%s (%5u/%8s)\n", color ? C_BOLD : "", color ? C_RESET : "",
               (unsigned)(st.st_mode & 07777), mode, color ? C_BOLD : "", color ? C_RESET : "", (unsigned)st.st_uid,
               util_user(st.st_uid, ub, sizeof(ub)), color ? C_BOLD : "", color ? C_RESET : "", (unsigned)st.st_gid,
               util_group(st.st_gid, gb, sizeof(gb)));
        show_time("Access:", st.st_atime, color);
        show_time("Modify:", st.st_mtime, color);
        show_time("Change:", st.st_ctime, color);
    }
    return rc;
}
