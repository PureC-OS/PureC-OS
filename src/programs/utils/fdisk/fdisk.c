#include "../lib/common.h"
#include "libc/include/purec.h"
#include <time.h>

#define SECTOR 512u
#define MAX_PARTS 128u
#define ENTRY_SIZE 128u
#define ENTRY_SECTORS (MAX_PARTS * ENTRY_SIZE / SECTOR)
#define ALIGN 2048u
#define MAX_DISKS 16

struct part {
    uint8_t type[16];
    uint8_t uuid[16];
    uint64_t first;
    uint64_t last;
    uint64_t attrs;
    char name[37];
};

struct disk {
    struct storage_device_info info;
    uint64_t sectors;
    bool gpt;
    bool mbr;
    bool dirty;
    uint8_t guid[16];
    uint64_t first_usable;
    uint64_t last_usable;
    struct part parts[MAX_PARTS];
    uint8_t mbr_sector[SECTOR];
};

struct ptype {
    const char *alias;
    const char *name;
    uint8_t guid[16];
};

static const struct ptype types[] = {
    {"efi", "EFI System", {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}},
    {"linux", "Linux filesystem", {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4}},
    {"swap", "Linux swap", {0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43, 0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F}},
    {"basic", "Microsoft basic data", {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}},
    {"purec", "PureC system (legacy)", {0xAF, 0x3C, 0x4C, 0x24, 0x00, 0x00, 0x42, 0x4D, 0xA0, 0x76, 0x4D, 0x89, 0x41, 0xFF, 0x86, 0xFE}},
};
#define TYPE_COUNT (sizeof(types) / sizeof(types[0]))
#define TYPE_LINUX 1

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}

static uint32_t crc32(const uint8_t *data, uint32_t size)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; i++)
        c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
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

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (i * 8));
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (i * 8));
}

static bool zero_guid(const uint8_t *g)
{
    for (int i = 0; i < 16; i++)
        if (g[i])
            return false;
    return true;
}

static void format_guid(const uint8_t *g, char *out)
{
    snprintf(out, 37, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", g[3], g[2], g[1],
             g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

static void random_guid(uint8_t *g)
{
    if (pc_getrandom(g, 16) != 16)
        for (int i = 0; i < 16; i++)
            g[i] = (uint8_t)rand();
    g[7] = (uint8_t)((g[7] & 0x0F) | 0x40);
    g[8] = (uint8_t)((g[8] & 0x3F) | 0x80);
}

static const char *type_name(const uint8_t *g)
{
    for (uint32_t i = 0; i < TYPE_COUNT; i++)
        if (memcmp(types[i].guid, g, 16) == 0)
            return types[i].name;
    return "unknown";
}

static void size_text(uint64_t sectors, char *out, size_t cap)
{
    util_human(out, cap, (unsigned long long)(sectors * SECTOR));
}

static bool io(const struct disk *d, uint64_t lba, void *buf, uint32_t count, bool write)
{
    uint8_t *p = buf;
    while (count) {
        uint32_t n = count > DISK_IO_MAX_SECTORS ? DISK_IO_MAX_SECTORS : count;
        if (pc_disk_io(d->info.name, lba, p, n, write) != (int32_t)n)
            return false;
        lba += n;
        p += (uint64_t)n * SECTOR;
        count -= n;
    }
    return true;
}

static void reset_layout(struct disk *d)
{
    memset(d->parts, 0, sizeof(d->parts));
    random_guid(d->guid);
    d->first_usable = 2 + ENTRY_SECTORS;
    d->last_usable = d->sectors - 2 - ENTRY_SECTORS;
}

static bool parse_header(struct disk *d, const uint8_t *hdr, uint64_t lba)
{
    if (memcmp(hdr, "EFI PART", 8) != 0)
        return false;
    uint32_t size = get32(hdr + 12);
    if (size < 92 || size > SECTOR || get64(hdr + 24) != lba)
        return false;
    uint8_t copy[SECTOR];
    memcpy(copy, hdr, size);
    put32(copy + 16, 0);
    if (crc32(copy, size) != get32(hdr + 16))
        return false;
    uint64_t entries_lba = get64(hdr + 72);
    uint32_t count = get32(hdr + 80), esize = get32(hdr + 84);
    if (esize != ENTRY_SIZE || count == 0 || count > MAX_PARTS)
        return false;
    static uint8_t table[MAX_PARTS * ENTRY_SIZE];
    memset(table, 0, sizeof(table));
    if (!io(d, entries_lba, table, (count * ENTRY_SIZE + SECTOR - 1) / SECTOR, false))
        return false;
    if (crc32(table, count * ENTRY_SIZE) != get32(hdr + 88))
        return false;
    memset(d->parts, 0, sizeof(d->parts));
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = table + i * ENTRY_SIZE;
        struct part *p = &d->parts[i];
        memcpy(p->type, e, 16);
        memcpy(p->uuid, e + 16, 16);
        p->first = get64(e + 32);
        p->last = get64(e + 40);
        p->attrs = get64(e + 48);
        for (int k = 0; k < 36; k++) {
            uint16_t ch = (uint16_t)(e[56 + k * 2] | (e[57 + k * 2] << 8));
            if (!ch)
                break;
            p->name[k] = ch < 128 ? (char)ch : '?';
        }
    }
    memcpy(d->guid, hdr + 56, 16);
    d->first_usable = get64(hdr + 40);
    d->last_usable = get64(hdr + 48);
    return true;
}

static const char *short_name(const char *name)
{
    return strncmp(name, "/dev/", 5) == 0 ? name + 5 : name;
}

static bool same_disk(const char *a, const char *b)
{
    return strcmp(short_name(a), short_name(b)) == 0;
}

static bool load(struct disk *d, const char *name)
{
    struct storage_device_info list[MAX_DISKS];
    int32_t n = pc_list_disks(list, MAX_DISKS);
    bool found = false;
    for (int32_t i = 0; i < n; i++)
        if (same_disk(list[i].name, name)) {
            d->info = list[i];
            found = true;
        }
    if (!found) {
        util_err("%s: no such disk (see fdisk -l)", name);
        return false;
    }
    if (d->info.sector_size != SECTOR) {
        util_err("%s: sector size %u is not supported", name, d->info.sector_size);
        return false;
    }
    d->sectors = d->info.sector_count;
    if (d->sectors < 2 * (2 + ENTRY_SECTORS) + ALIGN) {
        util_err("%s: disk is too small", name);
        return false;
    }
    int32_t rc = pc_disk_io(d->info.name, 0, d->mbr_sector, 1, false);
    if (rc == -1) {
        util_err("%s: permission denied (run as root)", name);
        return false;
    }
    if (rc != 1) {
        util_err("%s: cannot read the disk (error %d)", name, rc);
        return false;
    }
    d->mbr = d->mbr_sector[510] == 0x55 && d->mbr_sector[511] == 0xAA;
    uint8_t hdr[SECTOR];
    d->gpt = io(d, 1, hdr, 1, false) && parse_header(d, hdr, 1);
    if (!d->gpt && io(d, d->sectors - 1, hdr, 1, false) && parse_header(d, hdr, d->sectors - 1)) {
        d->gpt = true;
        d->dirty = true;
        printf("The primary GPT is corrupt, the backup GPT is used. Write to repair it.\n");
    }
    if (!d->gpt)
        reset_layout(d);
    return true;
}

static void print_table(const struct disk *d)
{
    char size[32], guid[40];
    size_text(d->sectors, size, sizeof(size));
    printf("Disk %s: %s, %llu bytes, %llu sectors\n", d->info.name, size,
           (unsigned long long)(d->sectors * SECTOR), (unsigned long long)d->sectors);
    if (d->info.model[0])
        printf("Disk model: %s\n", d->info.model);
    printf("Units: sectors of 1 * 512 = 512 bytes\n");
    if (!d->gpt) {
        printf("Disklabel type: %s\n\n", d->mbr ? "dos (MBR, read only here)" : "none");
        return;
    }
    format_guid(d->guid, guid);
    printf("Disklabel type: gpt\nDisk identifier: %s\n", guid);
    printf("First usable sector: %llu, last usable sector: %llu\n\n", (unsigned long long)d->first_usable,
           (unsigned long long)d->last_usable);
    bool any = false;
    for (uint32_t i = 0; i < MAX_PARTS; i++) {
        const struct part *p = &d->parts[i];
        if (zero_guid(p->type))
            continue;
        if (!any)
            printf("%-10s %12s %12s %12s %8s  %s\n", "Device", "Start", "End", "Sectors", "Size", "Type");
        any = true;
        char dev[24];
        snprintf(dev, sizeof(dev), "%s%u", d->info.name, i + 1);
        size_text(p->last - p->first + 1, size, sizeof(size));
        printf("%-10s %12llu %12llu %12llu %8s  %s%s%s\n", dev, (unsigned long long)p->first,
               (unsigned long long)p->last, (unsigned long long)(p->last - p->first + 1), size, type_name(p->type),
               p->name[0] ? "  " : "", p->name);
    }
    if (!any)
        printf("No partitions.\n");
    printf("\n");
}

static bool ask(const char *prompt, char *out, size_t cap)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(out, (int)cap, stdin))
        return false;
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
        out[--n] = 0;
    char *s = out;
    while (*s == ' ')
        s++;
    if (s != out)
        memmove(out, s, strlen(s) + 1);
    return true;
}

static bool overlaps(const struct disk *d, uint64_t first, uint64_t last, int skip)
{
    for (uint32_t i = 0; i < MAX_PARTS; i++) {
        if ((int)i == skip || zero_guid(d->parts[i].type))
            continue;
        if (first <= d->parts[i].last && last >= d->parts[i].first)
            return true;
    }
    return false;
}

static uint64_t align_up(uint64_t v)
{
    return (v + ALIGN - 1) / ALIGN * ALIGN;
}

static bool free_gap(const struct disk *d, uint64_t *start, uint64_t *end)
{
    uint64_t pos = align_up(d->first_usable);
    while (pos <= d->last_usable) {
        int hit = -1;
        for (uint32_t i = 0; i < MAX_PARTS; i++)
            if (!zero_guid(d->parts[i].type) && pos >= d->parts[i].first && pos <= d->parts[i].last)
                hit = (int)i;
        if (hit >= 0) {
            pos = align_up(d->parts[hit].last + 1);
            continue;
        }
        uint64_t stop = d->last_usable;
        for (uint32_t i = 0; i < MAX_PARTS; i++)
            if (!zero_guid(d->parts[i].type) && d->parts[i].first > pos && d->parts[i].first - 1 < stop)
                stop = d->parts[i].first - 1;
        *start = pos;
        *end = stop;
        return true;
    }
    return false;
}

static bool parse_number(const char *s, uint64_t *out)
{
    if (!*s)
        return false;
    char *end;
    unsigned long long v = strtoull(s, &end, 10);
    if (*end)
        return false;
    *out = v;
    return true;
}

static bool parse_last(const char *s, uint64_t first, uint64_t *out)
{
    if (*s != '+')
        return parse_number(s, out);
    char *end;
    unsigned long long v = strtoull(s + 1, &end, 10);
    if (!v)
        return false;
    uint64_t mult = 1;
    switch (*end) {
    case 0: mult = 1; break;
    case 'K': case 'k': mult = 1024 / SECTOR; break;
    case 'M': case 'm': mult = 1024ULL * 1024 / SECTOR; break;
    case 'G': case 'g': mult = 1024ULL * 1024 * 1024 / SECTOR; break;
    case 'T': case 't': mult = 1024ULL * 1024 * 1024 * 1024 / SECTOR; break;
    default: return false;
    }
    if (*end && end[1] && !(end[1] == 'i' || end[1] == 'B' || end[1] == 'b'))
        return false;
    *out = first + v * mult - 1;
    return true;
}

static int pick_type(const char *s)
{
    uint64_t n;
    if (parse_number(s, &n))
        return n >= 1 && n <= TYPE_COUNT ? (int)n - 1 : -1;
    for (uint32_t i = 0; i < TYPE_COUNT; i++)
        if (strcasecmp(s, types[i].alias) == 0)
            return (int)i;
    return -1;
}

static void list_types(void)
{
    for (uint32_t i = 0; i < TYPE_COUNT; i++) {
        char guid[40];
        format_guid(types[i].guid, guid);
        printf("  %u %-6s %-24s %s\n", i + 1, types[i].alias, types[i].name, guid);
    }
}

static void cmd_new(struct disk *d)
{
    int slot = -1;
    for (uint32_t i = 0; i < MAX_PARTS; i++)
        if (zero_guid(d->parts[i].type)) {
            slot = (int)i;
            break;
        }
    uint64_t gap_start, gap_end;
    if (slot < 0 || !free_gap(d, &gap_start, &gap_end)) {
        printf("No free sectors available.\n");
        return;
    }
    char line[64], prompt[160];
    snprintf(prompt, sizeof(prompt), "Partition number (1-%u, default %d): ", MAX_PARTS, slot + 1);
    if (!ask(prompt, line, sizeof(line)))
        return;
    uint64_t num = (uint64_t)slot + 1;
    if (line[0] && (!parse_number(line, &num) || num < 1 || num > MAX_PARTS)) {
        printf("Value out of range.\n");
        return;
    }
    if (!zero_guid(d->parts[num - 1].type)) {
        printf("Partition %llu is already defined. Delete it before re-adding it.\n", (unsigned long long)num);
        return;
    }
    snprintf(prompt, sizeof(prompt), "First sector (%llu-%llu, default %llu): ", (unsigned long long)d->first_usable,
             (unsigned long long)d->last_usable, (unsigned long long)gap_start);
    if (!ask(prompt, line, sizeof(line)))
        return;
    uint64_t first = gap_start;
    if (line[0] && !parse_number(line, &first)) {
        printf("Invalid value.\n");
        return;
    }
    if (first < d->first_usable || first > d->last_usable || overlaps(d, first, first, -1)) {
        printf("Sector %llu is already allocated or out of range.\n", (unsigned long long)first);
        return;
    }
    uint64_t limit = d->last_usable;
    for (uint32_t i = 0; i < MAX_PARTS; i++)
        if (!zero_guid(d->parts[i].type) && d->parts[i].first > first && d->parts[i].first - 1 < limit)
            limit = d->parts[i].first - 1;
    snprintf(prompt, sizeof(prompt), "Last sector, +/-sectors or +size{K,M,G,T} (%llu-%llu, default %llu): ",
             (unsigned long long)first, (unsigned long long)limit, (unsigned long long)limit);
    if (!ask(prompt, line, sizeof(line)))
        return;
    uint64_t last = limit;
    if (line[0] && !parse_last(line, first, &last)) {
        printf("Invalid value.\n");
        return;
    }
    if (last < first || last > limit) {
        printf("Value out of range.\n");
        return;
    }
    struct part *p = &d->parts[num - 1];
    memset(p, 0, sizeof(*p));
    memcpy(p->type, types[TYPE_LINUX].guid, 16);
    random_guid(p->uuid);
    p->first = first;
    p->last = last;
    d->dirty = true;
    char size[32];
    size_text(last - first + 1, size, sizeof(size));
    printf("Created a new partition %llu of type '%s' and of size %s.\n", (unsigned long long)num,
           types[TYPE_LINUX].name, size);
}

static int ask_existing(struct disk *d, const char *what)
{
    int only = -1, count = 0;
    for (uint32_t i = 0; i < MAX_PARTS; i++)
        if (!zero_guid(d->parts[i].type)) {
            only = (int)i;
            count++;
        }
    if (!count) {
        printf("No partition is defined yet!\n");
        return -1;
    }
    if (count == 1) {
        printf("Selected partition %d\n", only + 1);
        return only;
    }
    char line[32], prompt[64];
    snprintf(prompt, sizeof(prompt), "Partition number to %s: ", what);
    uint64_t n;
    if (!ask(prompt, line, sizeof(line)) || !parse_number(line, &n) || n < 1 || n > MAX_PARTS ||
        zero_guid(d->parts[n - 1].type)) {
        printf("No such partition.\n");
        return -1;
    }
    return (int)n - 1;
}

static void cmd_delete(struct disk *d)
{
    int i = ask_existing(d, "delete");
    if (i < 0)
        return;
    memset(&d->parts[i], 0, sizeof(d->parts[i]));
    d->dirty = true;
    printf("Partition %d has been deleted.\n", i + 1);
}

static void cmd_type(struct disk *d)
{
    int i = ask_existing(d, "change");
    if (i < 0)
        return;
    char line[32];
    if (!ask("Partition type or alias (type L to list all): ", line, sizeof(line)))
        return;
    if (line[0] == 'L' || line[0] == 'l') {
        list_types();
        if (!ask("Partition type or alias: ", line, sizeof(line)))
            return;
    }
    int t = pick_type(line);
    if (t < 0) {
        printf("Unknown partition type.\n");
        return;
    }
    memcpy(d->parts[i].type, types[t].guid, 16);
    d->dirty = true;
    printf("Changed type of partition %d to '%s'.\n", i + 1, types[t].name);
}

static void build_table(const struct disk *d, uint8_t *table)
{
    memset(table, 0, MAX_PARTS * ENTRY_SIZE);
    for (uint32_t i = 0; i < MAX_PARTS; i++) {
        const struct part *p = &d->parts[i];
        if (zero_guid(p->type))
            continue;
        uint8_t *e = table + i * ENTRY_SIZE;
        memcpy(e, p->type, 16);
        memcpy(e + 16, p->uuid, 16);
        put64(e + 32, p->first);
        put64(e + 40, p->last);
        put64(e + 48, p->attrs);
        for (int k = 0; k < 36 && p->name[k]; k++)
            e[56 + k * 2] = (uint8_t)p->name[k];
    }
}

static void build_header(const struct disk *d, uint8_t *hdr, uint64_t self, uint64_t other, uint64_t entries,
                         uint32_t table_crc)
{
    memset(hdr, 0, SECTOR);
    memcpy(hdr, "EFI PART", 8);
    put32(hdr + 8, 0x00010000u);
    put32(hdr + 12, 92);
    put64(hdr + 24, self);
    put64(hdr + 32, other);
    put64(hdr + 40, d->first_usable);
    put64(hdr + 48, d->last_usable);
    memcpy(hdr + 56, d->guid, 16);
    put64(hdr + 72, entries);
    put32(hdr + 80, MAX_PARTS);
    put32(hdr + 84, ENTRY_SIZE);
    put32(hdr + 88, table_crc);
    put32(hdr + 16, crc32(hdr, 92));
}

static bool write_disk(struct disk *d)
{
    static uint8_t table[MAX_PARTS * ENTRY_SIZE];
    uint8_t hdr[SECTOR];
    build_table(d, table);
    uint32_t table_crc = crc32(table, sizeof(table));
    uint64_t last = d->sectors - 1;
    uint8_t mbr[SECTOR];
    memcpy(mbr, d->mbr_sector, SECTOR);
    memset(mbr + 446, 0, 64);
    uint8_t *e = mbr + 446;
    e[1] = 0x00;
    e[2] = 0x02;
    e[3] = 0x00;
    e[4] = 0xEE;
    e[5] = 0xFF;
    e[6] = 0xFF;
    e[7] = 0xFF;
    put32(e + 8, 1);
    put32(e + 12, last > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)last);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    build_header(d, hdr, 1, last, 2, table_crc);
    if (!io(d, 2, table, ENTRY_SECTORS, true) || !io(d, 1, hdr, 1, true))
        return false;
    build_header(d, hdr, last, 1, last - ENTRY_SECTORS, table_crc);
    if (!io(d, last - ENTRY_SECTORS, table, ENTRY_SECTORS, true) || !io(d, last, hdr, 1, true))
        return false;
    if (!io(d, 0, mbr, 1, true))
        return false;
    memcpy(d->mbr_sector, mbr, SECTOR);
    d->gpt = true;
    d->mbr = true;
    d->dirty = false;
    return true;
}

static void help(void)
{
    printf("\nHelp:\n\n"
           "  GPT\n"
           "   g   create a new empty GPT partition table\n\n"
           "  Partitions\n"
           "   n   add a new partition\n"
           "   d   delete a partition\n"
           "   t   change a partition type\n"
           "   l   list known partition types\n"
           "   p   print the partition table\n\n"
           "  Save & Exit\n"
           "   w   write table to disk and exit\n"
           "   q   quit without saving changes\n\n"
           "   m   print this menu\n\n");
}

static bool is_root_disk(const char *name)
{
    char root[32];
    return pc_get_root_device(root, sizeof(root)) >= 0 && same_disk(root, name);
}

static int interactive(const char *name)
{
    static struct disk d;
    memset(&d, 0, sizeof(d));
    if (!load(&d, name))
        return 1;
    printf("\nWelcome to fdisk (PureC OS).\n"
           "Changes will remain in memory only, until you decide to write them.\n"
           "Be careful before using the write command.\n\n");
    if (!d.info.writable)
        printf("%s is read-only, the table cannot be written.\n\n", name);
    if (is_root_disk(name))
        printf("%s holds the mounted root file system. A new layout is used after a reboot.\n\n", name);
    if (!d.gpt)
        printf(d.mbr ? "The disk has an MBR (dos) table. Use 'g' to replace it with GPT.\n\n"
                     : "Device does not contain a recognized partition table. Use 'g' to create GPT.\n\n");
    char line[64];
    for (;;) {
        if (!ask("Command (m for help): ", line, sizeof(line))) {
            printf("\n");
            return 0;
        }
        if (!line[0])
            continue;
        switch (line[0]) {
        case 'm': help(); break;
        case 'p': print_table(&d); break;
        case 'l': list_types(); break;
        case 'g':
            reset_layout(&d);
            d.gpt = true;
            d.dirty = true;
            printf("Created a new GPT disklabel.\n");
            break;
        case 'n':
        case 'd':
        case 't':
            if (!d.gpt) {
                printf("No GPT on this disk. Use 'g' first.\n");
                break;
            }
            if (line[0] == 'n')
                cmd_new(&d);
            else if (line[0] == 'd')
                cmd_delete(&d);
            else
                cmd_type(&d);
            break;
        case 'w':
            if (!d.gpt) {
                printf("Nothing to write.\n");
                return 0;
            }
            if (!d.info.writable) {
                printf("%s is read-only.\n", name);
                break;
            }
            if (!write_disk(&d)) {
                util_err("%s: write failed", name);
                return 1;
            }
            printf("The partition table has been altered.\n"
                   "Reboot so the kernel sees the new layout, then format with mkfs ext2 %sN.\n",
                   name);
            return 0;
        case 'q':
            if (d.dirty)
                printf("Changes discarded.\n");
            return 0;
        default:
            printf("%c: unknown command\n", line[0]);
            break;
        }
    }
}

static int list_all(int argc, char **argv, int first)
{
    struct storage_device_info list[MAX_DISKS];
    int32_t n = pc_list_disks(list, MAX_DISKS);
    if (n < 0)
        return util_err("cannot enumerate disks");
    int status = 0;
    bool any = false;
    for (int32_t i = 0; i < n; i++) {
        bool wanted = first >= argc;
        for (int a = first; a < argc; a++)
            if (same_disk(argv[a], list[i].name))
                wanted = true;
        if (!wanted)
            continue;
        any = true;
        static struct disk d;
        memset(&d, 0, sizeof(d));
        if (!load(&d, list[i].name)) {
            status = 1;
            continue;
        }
        print_table(&d);
    }
    if (!any && first < argc)
        return util_err("%s: no such disk", argv[first]);
    return status;
}

int main(int argc, char **argv)
{
    util_init(argc, argv);
    crc_init();
    srand((unsigned)time(NULL));
    if (argc >= 2 && (!strcmp(argv[1], "-l") || !strcmp(argv[1], "--list")))
        return list_all(argc, argv, 2);
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        util_usage("[-l [DISK...]] | DISK");
        return 0;
    }
    if (argc == 2 && argv[1][0] != '-') {
        return interactive(argv[1]);
    }
    return util_err("usage: fdisk -l [DISK...] | fdisk DISK");
}
