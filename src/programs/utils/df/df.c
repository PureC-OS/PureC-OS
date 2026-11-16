#include "../lib/common.h"
#include "libc/include/purec.h"
#include <sys/statvfs.h>

struct fsrow {
    char name[48];
    char type[12];
    unsigned long long size, used, avail, files, ffree;
    const char *mount;
};

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

static bool partition_number(const char *disk, uint64_t lba, uint32_t *number)
{
    static uint8_t sec[512], table[512];
    if (pc_disk_io(disk, 1, sec, 1, false) != 1 || memcmp(sec, "EFI PART", 8) != 0)
        return false;
    uint64_t entries = get64(sec + 72);
    uint32_t count = get32(sec + 80);
    if (get32(sec + 84) != 128 || count > 128)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        if (i % 4 == 0 && pc_disk_io(disk, entries + i / 4, table, 1, false) != 1)
            return false;
        if (get64(table + (i % 4) * 128 + 32) == lba) {
            *number = i + 1;
            return true;
        }
    }
    return false;
}

static bool root_row(struct fsrow *r)
{
    memset(r, 0, sizeof(*r));
    r->mount = "/";
    struct ext2_super_info sb;
    if (pc_ext2_super(&sb) == 0 && sb.block_size) {
        char dev[40] = "";
        snprintf(r->type, sizeof(r->type), "ext2");
        if (pc_get_root_device(dev, sizeof(dev)) < 0 || !dev[0])
            snprintf(dev, sizeof(dev), "disk");
        const char *bare = strncmp(dev, "/dev/", 5) == 0 ? dev + 5 : dev;
        uint32_t number;
        if (partition_number(bare, sb.partition_lba, &number))
            snprintf(r->name, sizeof(r->name), "/dev/%s%u", bare, number);
        else
            snprintf(r->name, sizeof(r->name), "/dev/%s", bare);
        unsigned long long bs = sb.block_size;
        r->size = (unsigned long long)sb.total_blocks * bs;
        r->avail = (unsigned long long)sb.free_blocks * bs;
        r->used = r->size - r->avail;
        r->files = sb.total_inodes;
        r->ffree = sb.free_inodes;
        return true;
    }
    struct memory_monitor_info m;
    if (!pc_memory_info(&m))
        return false;
    snprintf(r->name, sizeof(r->name), "rootfs");
    snprintf(r->type, sizeof(r->type), "ramfs");
    r->size = m.total_bytes;
    r->used = m.used_bytes;
    r->avail = m.total_bytes > m.used_bytes ? m.total_bytes - m.used_bytes : 0;
    return true;
}

static void print_bar(unsigned pct, const char *pc)
{
    int width = 40, fill = (int)(pct * (unsigned)width / 100);
    printf("%*s%s", 14, "", pc);
    for (int i = 0; i < width; i++)
        fputs(i < fill ? "\xe2\x96\x88" : C_GRAY "\xe2\x96\x91", stdout);
    printf(C_RESET "\n");
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    struct opts o;
    opts_begin(&o, argc, argv);
    bool human = false, inodes = false, show_type = false;
    int c;
    while ((c = opts_next(&o, "hHikT")) != -1) {
        if (c == 'h' || c == 'H') human = true;
        else if (c == 'i') inodes = true;
        else if (c == 'T') show_type = true;
        else if (c == 'k') continue;
        else if (c == 'L' && opts_long(&o, "help")) { util_usage("[-hiT] [FILE]..."); return 0; }
        else return util_bad_option(&o, c);
    }
    for (int i = 0; i < o.count; i++) {
        struct stat st;
        if (stat(o.operands[i], &st) != 0) return util_perr(o.operands[i]);
        if (!strncmp(o.operands[i], "/boot", 5) && (o.operands[i][5] == 0 || o.operands[i][5] == '/'))
            return util_err("%s: the EFI partition (FAT) has no usage data yet", o.operands[i]);
    }
    struct fsrow r;
    if (!root_row(&r)) return util_err("cannot read the root file system");
    bool color = util_color(1, NULL);
    if (inodes) {
        if (!r.files) return util_err("%s keeps no inode counts", r.name);
        unsigned long long used = r.files - r.ffree;
        printf("%-14s %10s %10s %10s %5s %s\n", "Filesystem", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
        printf("%-14s %10llu %10llu %10llu %4llu%% %s\n", r.name, r.files, used, r.ffree,
               r.files ? used * 100 / r.files : 0, r.mount);
        return 0;
    }
    unsigned pct = r.size ? (unsigned)((r.used * 100 + r.size - 1) / r.size) : 0;
    printf("%-14s %s%9s %9s %9s %5s  %s\n", "Filesystem", show_type ? "Type   " : "", human ? "Size" : "1K-blocks",
           "Used", "Avail", "Use%", "Mounted on");
    char a[32], b[32], d[32], t[16] = "";
    if (human) {
        util_human(a, sizeof(a), r.size);
        util_human(b, sizeof(b), r.used);
        util_human(d, sizeof(d), r.avail);
    } else {
        snprintf(a, sizeof(a), "%llu", r.size / 1024);
        snprintf(b, sizeof(b), "%llu", r.used / 1024);
        snprintf(d, sizeof(d), "%llu", r.avail / 1024);
    }
    if (show_type) snprintf(t, sizeof(t), "%-7s", r.type);
    const char *pc = !color ? "" : pct >= 90 ? C_BOLD C_RED : pct >= 70 ? C_YELLOW : C_GREEN;
    printf("%-14s %s%9s %9s %9s %s%4u%%%s  %s\n", r.name, t, a, b, d, pc, pct, color ? C_RESET : "", r.mount);
    if (color && isatty(1)) print_bar(pct, pc);
    return 0;
}
