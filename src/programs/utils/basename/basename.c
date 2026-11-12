#include "../lib/common.h"

static void base(const char *path, const char *suffix)
{
    char buf[UTIL_PATH];
    snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = 0;
    const char *b = strrchr(buf, '/');
    b = (b && b[1]) ? b + 1 : (len == 1 && buf[0] == '/') ? "/" : b ? b + 1 : buf;
    char out[UTIL_PATH];
    snprintf(out, sizeof(out), "%s", b);
    if (suffix) {
        size_t ol = strlen(out), sl = strlen(suffix);
        if (sl < ol && !strcmp(out + ol - sl, suffix)) out[ol - sl] = 0;
    }
    puts(out);
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    const char *suffix = NULL;
    bool multiple = false;
    int c;
    while ((c = opts_next(&o, "as:z")) != -1) {
        if (c == 'a') multiple = true;
        else if (c == 's') { suffix = o.arg; multiple = true; }
        else if (c == 'z') continue;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("NAME [SUFFIX] | -a NAME..."); return 0; }
        else return util_bad_option(&o, c);
    }
    if (!o.count) return util_err("missing operand");
    if (!multiple) {
        base(o.operands[0], o.count > 1 ? o.operands[1] : NULL);
        return 0;
    }
    for (int i = 0; i < o.count; i++) base(o.operands[i], suffix);
    return 0;
}
