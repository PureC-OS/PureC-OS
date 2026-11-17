#include "../lib/common.h"
#include "libc/include/purec.h"
#include <dirent.h>
#include <sys/stat.h>

#define SECTOR 512u
#define TEXT_MAX 16384
#define ENTRY_MAX 32

static const char *conf_paths[] = {"limine.conf", "EFI/BOOT/limine.conf", "boot/limine/limine.conf"};
#define CONF_COUNT (sizeof(conf_paths) / sizeof(conf_paths[0]))

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static bool all_zero(const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++)
        if (p[i])
            return false;
    return true;
}

static void uuid_text(const uint8_t *g, char *out)
{
    static const char hex[] = "0123456789abcdef";
    int n = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out[n++] = '-';
        out[n++] = hex[g[i] >> 4];
        out[n++] = hex[g[i] & 15];
    }
    out[n] = '\0';
}

struct root {
    char disk[48];
    uint64_t lba;
    char name[56];
};

static const char *short_name(const char *name)
{
    return strncmp(name, "/dev/", 5) == 0 ? name + 5 : name;
}

static bool find_disk(const char *name, struct storage_device_info *out)
{
    struct storage_device_info list[16];
    int32_t n = pc_list_disks(list, 16);
    for (int32_t i = 0; i < n; i++)
        if (!strcmp(short_name(list[i].name), short_name(name))) {
            *out = list[i];
            return true;
        }
    return false;
}

static bool partition_lba(const char *disk, uint32_t number, uint64_t *first)
{
    static uint8_t sec[SECTOR], table[SECTOR];
    if (pc_disk_io(disk, 1, sec, 1, false) != 1 || memcmp(sec, "EFI PART", 8) != 0)
        return false;
    uint64_t entries = get64(sec + 72);
    uint32_t count = get32(sec + 80);
    if (get32(sec + 84) != 128 || number == 0 || number > count)
        return false;
    if (pc_disk_io(disk, entries + (number - 1) / 4, table, 1, false) != 1)
        return false;
    const uint8_t *e = table + ((number - 1) % 4) * 128;
    if (all_zero(e, 16))
        return false;
    *first = get64(e + 32);
    return true;
}

static bool partition_by_lba(const char *disk, uint64_t lba, uint32_t *number)
{
    static uint8_t sec[SECTOR], table[SECTOR];
    if (pc_disk_io(disk, 1, sec, 1, false) != 1 || memcmp(sec, "EFI PART", 8) != 0)
        return false;
    uint64_t entries = get64(sec + 72);
    uint32_t count = get32(sec + 80);
    if (get32(sec + 84) != 128 || count > 128)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        if (i % 4 == 0 && pc_disk_io(disk, entries + i / 4, table, 1, false) != 1)
            return false;
        const uint8_t *e = table + (i % 4) * 128;
        if (!all_zero(e, 16) && get64(e + 32) == lba) {
            *number = i + 1;
            return true;
        }
    }
    return false;
}

static int resolve_root(const char *arg, struct root *r)
{
    struct storage_device_info info;
    if (arg) {
        const char *s = short_name(arg);
        size_t len = strlen(s), base = len;
        while (base && s[base - 1] >= '0' && s[base - 1] <= '9')
            base--;
        char diskname[48];
        if (base >= sizeof(diskname))
            return util_err("%s: bad device name", arg);
        memcpy(diskname, s, base);
        diskname[base] = '\0';
        if (base < len) {
            if (!find_disk(diskname, &info))
                return util_err("%s: no such disk", diskname);
            uint32_t number = (uint32_t)atoi(s + base);
            if (!partition_lba(info.name, number, &r->lba))
                return util_err("%s: no such partition", arg);
            snprintf(r->disk, sizeof(r->disk), "%s", info.name);
            snprintf(r->name, sizeof(r->name), "%s%u", info.name, number);
            return 0;
        }
        if (!find_disk(s, &info))
            return util_err("%s: no such disk", arg);
        snprintf(r->disk, sizeof(r->disk), "%s", info.name);
        snprintf(r->name, sizeof(r->name), "%s", info.name);
        r->lba = 0;
        return 0;
    }
    struct ext2_super_info sb;
    char dev[48];
    if (pc_ext2_super(&sb) < 0 || pc_get_root_device(dev, sizeof(dev)) < 0)
        return util_err("the running system is not on an ext2 disk; name the root partition with -r");
    if (!find_disk(dev, &info))
        return util_err("%s: no such disk", dev);
    snprintf(r->disk, sizeof(r->disk), "%s", info.name);
    r->lba = sb.partition_lba;
    uint32_t number;
    if (partition_by_lba(info.name, r->lba, &number))
        snprintf(r->name, sizeof(r->name), "%s%u", info.name, number);
    else
        snprintf(r->name, sizeof(r->name), "%s", info.name);
    return 0;
}

static int read_uuid(const struct root *r, uint8_t uuid[16])
{
    static uint8_t sec[SECTOR];
    if (pc_disk_io(r->disk, r->lba + 2, sec, 1, false) != 1)
        return (util_err("%s: cannot read the superblock", r->name), -1);
    if (sec[56] != 0x53 || sec[57] != 0xEF)
        return (util_err("%s: not an ext2 file system", r->name), -1);
    if (get32(sec + 76) < 1 || all_zero(sec + 104, 16))
        return 1;
    memcpy(uuid, sec + 104, 16);
    return 0;
}

static int assign_uuid(const struct root *r, uint8_t uuid[16])
{
    static uint8_t sec[SECTOR];
    if (pc_disk_io(r->disk, r->lba + 2, sec, 1, false) != 1)
        return util_err("%s: cannot read the superblock", r->name);
    if (pc_getrandom(uuid, 16) != 16)
        return util_err("no random numbers available");
    uuid[6] = (uint8_t)((uuid[6] & 0x0F) | 0x40);
    uuid[8] = (uint8_t)((uuid[8] & 0x3F) | 0x80);
    if (get32(sec + 76) < 1) {
        put32(sec + 76, 1);
        put32(sec + 84, 11);
    }
    memcpy(sec + 104, uuid, 16);
    if (pc_disk_io(r->disk, r->lba + 2, sec, 1, true) != 1)
        return util_err("%s: cannot write the superblock", r->name);
    return 0;
}

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static size_t emit(char *out, size_t cap, size_t pos, const char *s, size_t n)
{
    if (pos + n >= cap)
        return cap;
    memcpy(out + pos, s, n);
    return pos + n;
}

static int rewrite(const char *text, size_t len, const char *uuid, char *out, size_t cap)
{
    const char *lines[512];
    size_t lens[512];
    int count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            if (i == len && start == len)
                break;
            if (count >= 512)
                return -1;
            lines[count] = text + start;
            lens[count] = i - start;
            if (lens[count] && lines[count][lens[count] - 1] == '\r')
                lens[count]--;
            count++;
            start = i + 1;
        }
    }
    bool has_cmdline[ENTRY_MAX] = {false};
    int entry = 0;
    for (int i = 0; i < count; i++) {
        char line[512];
        size_t n = lens[i] < sizeof(line) - 1 ? lens[i] : sizeof(line) - 1;
        memcpy(line, lines[i], n);
        line[n] = '\0';
        if (line[0] == '/') {
            if (++entry >= ENTRY_MAX)
                return -1;
            continue;
        }
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (entry > 0 && starts_with(p, "cmdline:"))
            has_cmdline[entry] = true;
    }
    char token[64];
    snprintf(token, sizeof(token), "root=UUID=%s", uuid);
    size_t pos = 0;
    entry = 0;
    for (int i = 0; i < count; i++) {
        char line[512];
        size_t n = lens[i] < sizeof(line) - 1 ? lens[i] : sizeof(line) - 1;
        memcpy(line, lines[i], n);
        line[n] = '\0';
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (line[0] == '/') {
            entry++;
            pos = emit(out, cap, pos, line, n);
            pos = emit(out, cap, pos, "\n", 1);
            if (entry < ENTRY_MAX && !has_cmdline[entry]) {
                char add[128];
                int m = snprintf(add, sizeof(add), "    cmdline: %s\n", token);
                pos = emit(out, cap, pos, add, (size_t)m);
            }
        } else if (entry > 0 && starts_with(p, "cmdline:")) {
            char rest[400];
            const char *v = p + 8;
            while (*v == ' ')
                v++;
            size_t r = 0;
            while (*v) {
                if (starts_with(v, "root=UUID=")) {
                    while (*v && *v != ' ')
                        v++;
                    while (*v == ' ')
                        v++;
                    continue;
                }
                if (r + 1 >= sizeof(rest))
                    break;
                rest[r++] = *v++;
            }
            while (r && rest[r - 1] == ' ')
                r--;
            rest[r] = '\0';
            char fixed[512];
            int m = snprintf(fixed, sizeof(fixed), "    cmdline: %s%s%s\n", rest, r ? " " : "", token);
            pos = emit(out, cap, pos, fixed, (size_t)m);
        } else {
            pos = emit(out, cap, pos, line, n);
            pos = emit(out, cap, pos, "\n", 1);
        }
        if (pos >= cap)
            return -1;
    }
    out[pos] = '\0';
    return (int)pos;
}

static int update_file(const char *path, const char *uuid, bool dry)
{
    static char text[TEXT_MAX], out[TEXT_MAX];
    FILE *f = fopen(path, "r");
    if (!f)
        return 1;
    size_t len = fread(text, 1, sizeof(text) - 1, f);
    bool big = !feof(f);
    fclose(f);
    if (big)
        return (util_err("%s: file is too large", path), -1);
    int n = rewrite(text, len, uuid, out, sizeof(out));
    if (n < 0)
        return (util_err("%s: cannot rewrite the configuration", path), -1);
    if (dry) {
        printf("--- %s\n%s", path, out);
        return 0;
    }
    f = fopen(path, "w");
    if (!f)
        return (util_perr(path), -1);
    size_t w = fwrite(out, 1, (size_t)n, f);
    if (fclose(f) != 0 || w != (size_t)n)
        return (util_err("%s: write failed", path), -1);
    printf("updated %s\n", path);
    return 0;
}

#define KERNEL_DIR "usr/lib/kernel"

struct kconf {
    char name[64];
    char version[32];
    char resolution[32];
    char fallback_resolution[32];
    char cmdline[256];
    int timeout;
};

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    if (i)
        memmove(s, s + i, strlen(s + i) + 1);
}

static void kconf_read(const char *path, struct kconf *k)
{
    snprintf(k->name, sizeof(k->name), "PureC OS");
    k->version[0] = '\0';
    snprintf(k->resolution, sizeof(k->resolution), "1920x1080x32");
    snprintf(k->fallback_resolution, sizeof(k->fallback_resolution), "1280x800x32");
    k->cmdline[0] = '\0';
    k->timeout = 10;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char *line = NULL;
    size_t cap = 0;
    while (util_getline(&line, &cap, f) > 0) {
        trim(line);
        if (!line[0] || line[0] == '#')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = line, *value = eq + 1;
        trim(key);
        trim(value);
        if (!strcmp(key, "KERNEL_NAME"))
            snprintf(k->name, sizeof(k->name), "%s", value);
        else if (!strcmp(key, "KERNEL_VERSION"))
            snprintf(k->version, sizeof(k->version), "%s", value);
        else if (!strcmp(key, "KERNEL_RESOLUTION"))
            snprintf(k->resolution, sizeof(k->resolution), "%s", value);
        else if (!strcmp(key, "KERNEL_FALLBACK_RESOLUTION"))
            snprintf(k->fallback_resolution, sizeof(k->fallback_resolution), "%s", value);
        else if (!strcmp(key, "KERNEL_CMDLINE"))
            snprintf(k->cmdline, sizeof(k->cmdline), "%s", value);
        else if (!strcmp(key, "KERNEL_TIMEOUT"))
            k->timeout = atoi(value);
    }
    free(line);
    fclose(f);
}

static int vercmp(const char *a, const char *b)
{
    while (*a || *b) {
        while (*a && (*a < '0' || *a > '9'))
            a++;
        while (*b && (*b < '0' || *b > '9'))
            b++;
        long x = 0, y = 0;
        while (*a >= '0' && *a <= '9')
            x = x * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9')
            y = y * 10 + (*b++ - '0');
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

static bool newest_kernel(const char *dir, char *out, size_t cap)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    out[0] = '\0';
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char conf[UTIL_PATH], sub[UTIL_PATH];
        util_join(sub, sizeof(sub), dir, e->d_name);
        util_join(conf, sizeof(conf), sub, "kernel.elf");
        if (!util_exists(conf))
            continue;
        if (!out[0] || vercmp(e->d_name, out) > 0)
            snprintf(out, cap, "%s", e->d_name);
    }
    closedir(d);
    return out[0] != '\0';
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return util_perr(src);
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return util_perr(dst);
    }
    int rc = util_copy_stream(in, out);
    fclose(in);
    if (fclose(out) != 0 || rc)
        return util_err("%s: write failed", dst);
    return 0;
}

static bool same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (stat(a, &sa) || stat(b, &sb) || sa.st_size != sb.st_size)
        return false;
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    bool same = fa && fb;
    static char ba[65536], bb[65536];
    while (same) {
        size_t na = fread(ba, 1, sizeof(ba), fa), nb = fread(bb, 1, sizeof(bb), fb);
        if (na != nb || memcmp(ba, bb, na))
            same = false;
        if (na < sizeof(ba))
            break;
    }
    if (fa)
        fclose(fa);
    if (fb)
        fclose(fb);
    return same;
}

static void read_line(const char *path, char *out, size_t cap)
{
    out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    if (!fgets(out, (int)cap, f))
        out[0] = '\0';
    fclose(f);
    trim(out);
}

static int write_line(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return util_perr(path);
    fprintf(f, "%s\n", text);
    if (fclose(f) != 0)
        return util_err("%s: write failed", path);
    return 0;
}

static int update_if_changed(const char *src, const char *dst, bool dry, const char *what)
{
    if (!util_exists(src) || same_file(src, dst))
        return 0;
    if (dry) {
        printf("would install %s -> %s\n", what, dst);
        return 0;
    }
    unlink(dst);
    if (copy_file(src, dst))
        return 1;
    printf("installed %s -> %s\n", what, dst);
    return 0;
}

static int install_kernel(const char *root, const char *boot, bool dry, struct kconf *k, char *previous, size_t pcap)
{
    char base[UTIL_PATH], version[64], dir[UTIL_PATH], path[UTIL_PATH];
    util_join(base, sizeof(base), root[0] ? root : "/", KERNEL_DIR);
    if (!newest_kernel(base, version, sizeof(version)))
        return util_err("no kernel found in %s", base);
    util_join(dir, sizeof(dir), base, version);
    util_join(path, sizeof(path), dir, "kconfig");
    kconf_read(path, k);
    if (!k->version[0])
        snprintf(k->version, sizeof(k->version), "%s", version);

    char bootdir[UTIL_PATH], cur[UTIL_PATH], fb[UTIL_PATH], rel[UTIL_PATH], fbrel[UTIL_PATH], src[UTIL_PATH];
    util_join(bootdir, sizeof(bootdir), boot, "boot");
    util_join(cur, sizeof(cur), bootdir, "kernel.elf");
    util_join(fb, sizeof(fb), bootdir, "kernel-fallback.elf");
    util_join(rel, sizeof(rel), bootdir, "kernel.ver");
    util_join(fbrel, sizeof(fbrel), bootdir, "kernel-fallback.ver");
    util_join(src, sizeof(src), dir, "kernel.elf");
    if (!util_is_dir(bootdir))
        return util_err("%s: no boot directory (is the boot partition mounted?)", bootdir);

    char current[64];
    read_line(rel, current, sizeof(current));
    if (util_exists(cur) && same_file(src, cur)) {
        printf("kernel %s is already installed\n", k->version);
    } else if (dry) {
        printf("would install kernel %s -> %s (previous kernel -> %s)\n", k->version, cur, fb);
        snprintf(previous, pcap, "%s", current);
    } else {
        if (util_exists(cur)) {
            unlink(fb);
            if (rename(cur, fb) != 0) {
                if (copy_file(cur, fb))
                    return 1;
                unlink(cur);
            }
            write_line(fbrel, current[0] ? current : "unknown");
        }
        if (copy_file(src, cur)) {
            if (util_exists(fb) && rename(fb, cur) == 0)
                util_err("restored the previous kernel");
            return 1;
        }
        write_line(rel, k->version);
        printf("installed kernel %s -> %s\n", k->version, cur);
    }
    if (!previous[0])
        read_line(fbrel, previous, pcap);

    char initrd[UTIL_PATH], dst[UTIL_PATH];
    util_join(initrd, sizeof(initrd), dir, "initramfs.cpio");
    util_join(dst, sizeof(dst), bootdir, "initramfs.cpio");
    if (update_if_changed(initrd, dst, dry, "initramfs"))
        return 1;

    char mods[UTIL_PATH], target[UTIL_PATH];
    util_join(mods, sizeof(mods), dir, "modules");
    util_join(target, sizeof(target), root[0] ? root : "/", "lib/modules");
    DIR *d = opendir(mods);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.')
                continue;
            util_join(src, sizeof(src), mods, e->d_name);
            util_join(dst, sizeof(dst), target, e->d_name);
            if (update_if_changed(src, dst, dry, e->d_name)) {
                closedir(d);
                return 1;
            }
        }
        closedir(d);
    }
    return 0;
}

static int write_limine(const char *boot, const struct kconf *k, const char *previous, bool dry)
{
    char fb[UTIL_PATH], bootdir[UTIL_PATH];
    util_join(bootdir, sizeof(bootdir), boot, "boot");
    util_join(fb, sizeof(fb), bootdir, "kernel-fallback.elf");
    bool has_fallback = util_exists(fb);
    static char text[TEXT_MAX];
    size_t n = 0;
    n += (size_t)snprintf(text + n, sizeof(text) - n, "timeout: %d\nverbose: yes\n\n", k->timeout);
    n += (size_t)snprintf(text + n, sizeof(text) - n,
                          "/%s %s\n    protocol: limine\n    resolution: %s\n    kernel_path: boot():/boot/kernel.elf\n",
                          k->name, k->version, k->resolution);
    if (has_fallback)
        n += (size_t)snprintf(text + n, sizeof(text) - n, "    module_path: boot():/boot/kernel-fallback.elf\n");
    n += (size_t)snprintf(text + n, sizeof(text) - n,
                          "    module_path: boot():/EFI/BOOT/BOOTX64.EFI\n    module_path: boot():/boot/initramfs.cpio\n");
    if (k->cmdline[0])
        n += (size_t)snprintf(text + n, sizeof(text) - n, "    cmdline: %s\n", k->cmdline);
    if (has_fallback) {
        n += (size_t)snprintf(text + n, sizeof(text) - n,
                              "\n/%s%s%s (previous kernel)\n    protocol: limine\n    resolution: %s\n"
                              "    kernel_path: boot():/boot/kernel-fallback.elf\n"
                              "    module_path: boot():/EFI/BOOT/BOOTX64.EFI\n    module_path: boot():/boot/initramfs.cpio\n",
                              k->name, previous[0] ? " " : "", previous, k->fallback_resolution);
        if (k->cmdline[0])
            n += (size_t)snprintf(text + n, sizeof(text) - n, "    cmdline: %s\n", k->cmdline);
    }
    if (n >= sizeof(text))
        return util_err("limine.conf is too large");
    int done = 0;
    for (size_t i = 0; i < CONF_COUNT; i++) {
        char path[UTIL_PATH];
        util_join(path, sizeof(path), boot, conf_paths[i]);
        if (i > 0 && !util_exists(path))
            continue;
        if (dry) {
            printf("--- %s\n%s", path, text);
            done++;
            continue;
        }
        FILE *f = fopen(path, "w");
        if (!f)
            return util_perr(path);
        size_t w = fwrite(text, 1, n, f);
        if (fclose(f) != 0 || w != n)
            return util_err("%s: write failed", path);
        printf("generated %s\n", path);
        done++;
    }
    return done ? 0 : util_err("cannot write limine.conf under %s", boot);
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    const char *root_arg = NULL;
    const char *boot = NULL;
    bool assign = false, dry = false, preset = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            util_usage("[-P] [-r ROOT] [-b BOOTDIR] [-u] [-n]");
            return 0;
        } else if (!strcmp(argv[i], "-r") && i + 1 < argc) {
            root_arg = argv[++i];
        } else if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            boot = argv[++i];
        } else if (!strcmp(argv[i], "-u")) {
            assign = true;
        } else if (!strcmp(argv[i], "-n")) {
            dry = true;
        } else if (!strcmp(argv[i], "-P")) {
            preset = true;
        } else {
            return util_err("usage: mkinitcpio [-P] [-r ROOT] [-b BOOTDIR] [-u] [-n]");
        }
    }
    const char *pkg_root = getenv("PKG_ROOT");
    if (!pkg_root || !strcmp(pkg_root, "/"))
        pkg_root = "";
    char boot_buf[UTIL_PATH];
    if (!boot) {
        util_join(boot_buf, sizeof(boot_buf), pkg_root[0] ? pkg_root : "/", "boot");
        boot = boot_buf;
    }
    if (preset) {
        struct kconf k;
        char previous[64] = "";
        if (install_kernel(pkg_root, boot, dry, &k, previous, sizeof(previous)))
            return 1;
        if (write_limine(boot, &k, previous, dry))
            return 1;
        if (pkg_root[0] && !root_arg) {
            util_err("installing into %s: run mkinitcpio -r ROOT there to set root=UUID", pkg_root);
            return 1;
        }
    }
    struct root r;
    if (resolve_root(root_arg, &r))
        return 1;
    uint8_t uuid[16];
    int rc = read_uuid(&r, uuid);
    if (rc < 0)
        return 1;
    if (rc == 1) {
        if (!assign)
            return util_err("%s has no UUID; run again with -u to assign one", r.name);
        if (dry)
            return util_err("%s has no UUID; -n cannot assign one", r.name);
        if (assign_uuid(&r, uuid))
            return 1;
        printf("assigned a new UUID to %s\n", r.name);
    }
    char text[40];
    uuid_text(uuid, text);
    printf("root %s UUID=%s\n", r.name, text);
    int done = 0;
    for (size_t i = 0; i < CONF_COUNT; i++) {
        char path[UTIL_PATH];
        util_join(path, sizeof(path), boot, conf_paths[i]);
        int s = update_file(path, text, dry);
        if (s == 0)
            done++;
        else if (s < 0 || s > 1)
            return 1;
    }
    if (!done)
        return util_err("no limine.conf found under %s (is the boot partition mounted?)", boot);
    return 0;
}
