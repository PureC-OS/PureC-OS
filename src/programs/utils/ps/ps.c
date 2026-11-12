#include "../lib/common.h"
#include "libc/include/purec.h"

static const char *state_name(uint32_t s)
{
    switch (s) {
    case 1: return "S";
    case 2: return "R";
    case 3: return "Z";
    case 4: return "D";
    }
    return "?";
}

static int by_pid(const void *a, const void *b)
{
    const struct process_monitor_info *x = a, *y = b;
    return (int)x->pid - (int)y->pid;
}

static int by_cpu(const void *a, const void *b)
{
    const struct process_monitor_info *x = a, *y = b;
    if (x->cpu_percent != y->cpu_percent) return (int)y->cpu_percent - (int)x->cpu_percent;
    return (int)x->pid - (int)y->pid;
}

static int by_mem(const void *a, const void *b)
{
    const struct process_monitor_info *x = a, *y = b;
    if (x->resident_bytes != y->resident_bytes) return x->resident_bytes < y->resident_bytes ? 1 : -1;
    return (int)x->pid - (int)y->pid;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool tree = false, wide = false;
    char sort = 'p';
    int c;
    while ((c = opts_next(&o, "aexufwHlT")) != -1) {
        if (c == 'H' || c == 'T') tree = true;
        else if (c == 'w' || c == 'l') wide = true;
        else if (c == 'L' && o.name && opts_long(&o, "sort")) {
            const char *v = o.value ? o.value : "";
            sort = strstr(v, "cpu") ? 'c' : strstr(v, "mem") || strstr(v, "rss") ? 'm' : 'p';
        } else if (c == 'L' && o.name && opts_long(&o, "help")) { util_usage("[-aux] [-H] [--sort=pid|cpu|mem]"); return 0; }
        else if (c == 'L' && !o.name) wide = true;
        else if (strchr("aexuf", c)) continue;
        else return util_bad_option(&o, c);
    }
    (void)wide;
    static struct process_monitor_info list[512];
    int n = 0;
    for (;;) {
        int got = pc_process_list_page(list + n, (uint32_t)(512 - n), (uint32_t)n);
        if (got <= 0) break;
        n += got;
        if (n >= 512 || got < 64) break;
    }
    if (n <= 0) n = pc_process_list(list, 512);
    if (n <= 0) return util_err("cannot read process list");
    qsort(list, (size_t)n, sizeof(list[0]), sort == 'c' ? by_cpu : sort == 'm' ? by_mem : by_pid);
    bool color = util_color(1, NULL);
    if (color) printf(C_BOLD);
    printf("%5s %5s %-4s %4s %8s %9s  %s\n", "PID", "PPID", "STAT", "%CPU", "RSS", "TIME", "COMMAND");
    if (color) printf(C_RESET);
    int self = getpid();
    for (int i = 0; i < n; i++) {
        struct process_monitor_info *p = &list[i];
        if (p->state == 0) continue;
        char rss[32];
        util_human(rss, sizeof(rss), p->resident_bytes);
        unsigned long long t = p->runtime_ms / 1000;
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), "%llu:%02llu.%02llu", t / 60, t % 60, (p->runtime_ms % 1000) / 10);
        int depth = 0;
        if (tree) {
            uint32_t parent = p->parent_pid;
            for (int guard = 0; parent && guard < 16; guard++) {
                bool found = false;
                for (int k = 0; k < n; k++)
                    if (list[k].pid == parent) { parent = list[k].parent_pid; found = true; break; }
                if (!found) break;
                depth++;
            }
        }
        const char *hl = !color ? "" : (int)p->pid == self ? C_CYAN : p->cpu_percent >= 50 ? C_RED : p->state == 2 ? C_GREEN : "";
        printf("%s%5u %5u %-4s %4u %8s %9s  %*s%s%s\n", hl, p->pid, p->parent_pid, state_name(p->state), p->cpu_percent, rss, tbuf,
               depth * 2, "", p->name, color ? C_RESET : "");
    }
    return 0;
}
