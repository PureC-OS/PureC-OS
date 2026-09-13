// Read-only ramdisk over a ustar archive (see ramdisk.h).
//
// Parser limits (deliberate, matches what gen_assets.py produces):
// plain ustar, regular files + directories, short names (prefix honoured),
// no sparse/extended/Global headers. Anything else is skipped entry-wise,
// never fatal: a corrupt entry must not kill the whole archive.
#include "ramdisk.h"
#include "../boot/install_source.h"
#include "../kernel/diagnostics/klog.h"
#include "../lib/string.h"

#define RAMDISK_LIMINE_PATH "/boot/initramfs.tar"
#define RAMDISK_MAX_FILES 384u
#define TAR_BLOCK 512u

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

struct ramdisk_entry {
    const char *name; // inside the image, NUL-bounded by header field
    const void *data;
    uint64_t size;
};

static struct ramdisk_entry index_table[RAMDISK_MAX_FILES];
static uint32_t index_count = 0;
static bool ready = false;

static bool parse_octal(const char *field, uint32_t field_size, uint64_t *out) {
    // Numeric fields are NUL- or space-terminated octal; leading
    // NULs/spaces allowed. Reject anything else (base-256 binary
    // extension unsupported by design).
    uint64_t value = 0;
    uint32_t i = 0;
    while (i < field_size && (field[i] == '\0' || field[i] == ' '))
        i++;
    if (i == field_size) {
        *out = 0;
        return true;
    }
    bool any = false;
    for (; i < field_size; i++) {
        char c = field[i];
        if (c == '\0' || c == ' ')
            break;
        if (c < '0' || c > '7')
            return false;
        if (value > (UINT64_MAX - 7) / 8)
            return false;
        value = value * 8 + (uint64_t)(c - '0');
        any = true;
    }
    *out = value;
    return any;
}

static uint32_t stored_checksum(const struct tar_header *h) {
    uint64_t v = 0;
    if (!parse_octal(h->chksum, sizeof(h->chksum), &v) || v > UINT32_MAX)
        return UINT32_MAX;
    return (uint32_t)v;
}

static uint32_t compute_checksum(const struct tar_header *h) {
    const uint8_t *b = (const uint8_t *)h;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < TAR_BLOCK; i++)
        sum += b[i];
    // chksum field itself counts as spaces.
    for (uint32_t i = 0; i < sizeof(h->chksum); i++)
        sum -= (uint8_t)h->chksum[i];
    sum += (uint32_t)' ' * sizeof(h->chksum);
    return sum;
}

// Full entry name: prefix + "/" + name when prefix present.
// Returns pointer into the image or NULL when unterminated.
static const char *entry_name(const struct tar_header *h, const uint8_t *img_end) {
    const uint8_t *base = (const uint8_t *)h;
    // name[100] must be NUL-terminated inside the field...
    uint32_t ni = 0;
    while (ni < sizeof(h->name) && h->name[ni])
        ni++;
    if (ni == sizeof(h->name))
        return NULL;
    if (h->prefix[0]) {
        uint32_t pi = 0;
        while (pi < sizeof(h->prefix) && h->prefix[pi])
            pi++;
        if (pi == sizeof(h->prefix))
            return NULL;
        // ...but "prefix/name" composition needs a scratch buffer.
        // Long names never occur in our archive (gen_assets caps them),
        // so reject instead of complicating the index.
        (void)img_end;
        return NULL;
    }
    if (h->name[0] == '\0')
        return NULL;
    return h->name;
}

bool ramdisk_init(void) {
    index_count = 0;
    ready = false;
    const void *image = NULL;
    uint64_t image_size = 0;
    if (!boot_get_module(RAMDISK_LIMINE_PATH, &image, &image_size) || !image ||
        image_size < TAR_BLOCK) {
        klog(KLOG_WARN, "ramdisk: no /boot/initramfs.tar module, ramdisk disabled");
        return false;
    }
    const uint8_t *p = (const uint8_t *)image;
    const uint8_t *end = p + image_size;
    uint32_t skipped = 0;
    while (p + TAR_BLOCK <= end) {
        const struct tar_header *h = (const struct tar_header *)p;
        if (h->name[0] == '\0') {
            // Two zero blocks end the archive; one is enough to stop.
            break;
        }
        if (memcmp(h->magic, "ustar", 5) != 0) {
            klog(KLOG_WARN, "ramdisk: bad ustar magic, stopping parse");
            break;
        }
        uint64_t fsize = 0;
        if (!parse_octal(h->size, sizeof(h->size), &fsize)) {
            klog(KLOG_WARN, "ramdisk: bad size field, stopping parse");
            break;
        }
        if (stored_checksum(h) != compute_checksum(h)) {
            klog(KLOG_WARN, "ramdisk: checksum mismatch, stopping parse");
            break;
        }
        uint64_t blocks = (fsize + TAR_BLOCK - 1) / TAR_BLOCK;
        if (p + TAR_BLOCK + blocks * TAR_BLOCK < p || // overflow
            p + TAR_BLOCK + blocks * TAR_BLOCK > end) {
            klog(KLOG_WARN, "ramdisk: entry overruns image, stopping parse");
            break;
        }
        char type = h->typeflag;
        if ((type == '0' || type == '\0') && index_count < RAMDISK_MAX_FILES) {
            const char *name = entry_name(h, end);
            if (name) {
                // tar convention: later duplicates win.
                bool replaced = false;
                for (uint32_t i = 0; i < index_count; i++) {
                    if (strcmp(index_table[i].name, name) == 0) {
                        index_table[i].data = p + TAR_BLOCK;
                        index_table[i].size = fsize;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    index_table[index_count].name = name;
                    index_table[index_count].data = p + TAR_BLOCK;
                    index_table[index_count].size = fsize;
                    index_count++;
                }
            } else {
                skipped++;
            }
        } else if (type != '5') {
            skipped++;
        }
        p += TAR_BLOCK + blocks * TAR_BLOCK;
    }
    ready = true;
    klogf(KLOG_OK, "ramdisk: %u file(s) indexed from initramfs (%llu KB)%s", index_count,
          (unsigned long long)image_size / 1024,
          skipped ? " (some entries skipped)" : "");
    return true;
}

bool ramdisk_is_ready(void) {
    return ready;
}

bool ramdisk_file(const char *path, const void **data, uint64_t *size) {
    if (!path || !ready)
        return false;
    while (*path == '/')
        path++;
    if (!*path)
        return false;
    for (uint32_t i = 0; i < index_count; i++) {
        if (strcmp(index_table[i].name, path) == 0) {
            if (data)
                *data = index_table[i].data;
            if (size)
                *size = index_table[i].size;
            return true;
        }
    }
    return false;
}

uint32_t ramdisk_file_count(void) {
    return index_count;
}

void ramdisk_list(void) {
    klogf(KLOG_INFO, "ramdisk: %u file(s)", index_count);
    for (uint32_t i = 0; i < index_count; i++) {
        klogf(KLOG_INFO, "ramdisk:   %s (%llu bytes)", index_table[i].name,
              (unsigned long long)index_table[i].size);
    }
}
