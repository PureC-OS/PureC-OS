#include "../lib/common.h"
#include "libc/include/purec.h"

#define SECTOR 512u
#define MAX_DISKS 16
#define MAX_ENTRIES 128u

struct found {
    char dev[48];
    char type[8];
    char uuid[40];
    char label[40];
    char partuuid[40];
};

static void guid_text(const uint8_t *g, char *out)
{
    static const char hex[] = "0123456789abcdef";
    static const int dash[] = {4, 6, 8, 10};
    int n = 0;
    for (int i = 0; i < 16; i++) {
        for (int d = 0; d < 4; d++)
            if (dash[d] == i)
                out[n++] = '-';
        out[n++] = hex[g[i] >> 4];
        out[n++] = hex[g[i] & 15];
    }
    out[n] = '\0';
}

static void gpt_guid_text(const uint8_t *g, char *out)
{
    uint8_t m[16] = {g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6],
                     g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]};
    guid_text(m, out);
}

static bool all_zero(const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++)
        if (p[i])
            return false;
    return true;
}

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

static void trim_label(char *s)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ')
        s[--n] = '\0';
}

static bool probe(const char *disk, uint64_t lba, struct found *f)
{
    static uint8_t sec[SECTOR * 2];
    if (pc_disk_io(disk, lba, sec, 1, false) == 1) {
        if (sec[510] == 0x55 && sec[511] == 0xAA && memcmp(sec + 82, "FAT32   ", 8) == 0) {
            strcpy(f->type, "vfat");
            snprintf(f->uuid, sizeof(f->uuid), "%04X-%04X", (unsigned)(get32(sec + 67) >> 16),
                     (unsigned)(get32(sec + 67) & 0xFFFF));
            memcpy(f->label, sec + 71, 11);
            f->label[11] = '\0';
            trim_label(f->label);
            if (!strcmp(f->label, "NO NAME"))
                f->label[0] = '\0';
            return true;
        }
    }
    if (pc_disk_io(disk, lba + 2, sec, 1, false) != 1)
        return false;
    if (sec[56] != 0x53 || sec[57] != 0xEF)
        return false;
    strcpy(f->type, "ext2");
    if (get32(sec + 76) >= 1 && !all_zero(sec + 104, 16))
        guid_text(sec + 104, f->uuid);
    memcpy(f->label, sec + 120, 16);
    f->label[16] = '\0';
    return true;
}

static void show(const struct found *f, const char *only_uuid, bool *any)
{
    if (only_uuid) {
        if (f->uuid[0] && strcasecmp(f->uuid, only_uuid) == 0) {
            puts(f->dev);
            *any = true;
        }
        return;
    }
    printf("%s:", f->dev);
    if (f->label[0])
        printf(" LABEL=\"%s\"", f->label);
    if (f->uuid[0])
        printf(" UUID=\"%s\"", f->uuid);
    if (f->type[0])
        printf(" TYPE=\"%s\"", f->type);
    if (f->partuuid[0])
        printf(" PARTUUID=\"%s\"", f->partuuid);
    putchar('\n');
    *any = true;
}

static bool wanted(const char *dev, int count, char **names)
{
    if (!count)
        return true;
    const char *d = strncmp(dev, "/dev/", 5) == 0 ? dev + 5 : dev;
    for (int i = 0; i < count; i++) {
        const char *w = strncmp(names[i], "/dev/", 5) == 0 ? names[i] + 5 : names[i];
        if (!strcmp(d, w))
            return true;
    }
    return false;
}

static void scan_disk(const struct storage_device_info *info, int count, char **names, const char *only_uuid,
                      bool *any, int *denied)
{
    static uint8_t sec[SECTOR];
    int32_t rc = pc_disk_io(info->name, 1, sec, 1, false);
    if (rc == -1) {
        (*denied)++;
        return;
    }
    bool gpt = rc == 1 && memcmp(sec, "EFI PART", 8) == 0;
    struct found f;
    if (!gpt) {
        memset(&f, 0, sizeof(f));
        snprintf(f.dev, sizeof(f.dev), "%s", info->name);
        if (wanted(f.dev, count, names) && probe(info->name, 0, &f))
            show(&f, only_uuid, any);
        return;
    }
    uint64_t entries_lba = get64(sec + 72);
    uint32_t entry_count = get32(sec + 80);
    uint32_t entry_size = get32(sec + 84);
    if (entry_size != 128 || entry_count > MAX_ENTRIES)
        return;
    uint32_t per = SECTOR / 128;
    static uint8_t table[SECTOR];
    for (uint32_t i = 0; i < entry_count; i++) {
        if (i % per == 0 && pc_disk_io(info->name, entries_lba + i / per, table, 1, false) != 1)
            return;
        const uint8_t *e = table + (i % per) * 128;
        if (all_zero(e, 16))
            continue;
        memset(&f, 0, sizeof(f));
        snprintf(f.dev, sizeof(f.dev), "%s%u", info->name, i + 1);
        if (!wanted(f.dev, count, names))
            continue;
        gpt_guid_text(e + 16, f.partuuid);
        probe(info->name, get64(e + 32), &f);
        show(&f, only_uuid, any);
    }
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    const char *only_uuid = NULL;
    int first = 1;
    if (argc >= 2 && !strcmp(argv[1], "--help")) {
        util_usage("[-U UUID] [DEVICE...]");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "-U")) {
        if (argc < 3)
            return util_err("-U needs a UUID");
        only_uuid = argv[2];
        first = 3;
    }
    struct storage_device_info list[MAX_DISKS];
    int32_t n = pc_list_disks(list, MAX_DISKS);
    if (n < 0)
        return util_err("cannot enumerate disks");
    bool any = false;
    int denied = 0;
    for (int32_t i = 0; i < n; i++)
        scan_disk(&list[i], argc - first, argv + first, only_uuid, &any, &denied);
    if (denied && !any)
        return util_err("permission denied (run as root)");
    return any ? 0 : 2;
}
