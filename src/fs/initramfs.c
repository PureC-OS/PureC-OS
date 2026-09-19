#include "initramfs.h"
#include "vfs.h"
#include "../boot/install_source.h"
#include "../lib/string.h"
#include "../kernel/diagnostics/klog.h"

#define CPIO_HEADER_SIZE 110u
#define CPIO_ALIGNMENT 4u
#define INITRAMFS_MAX_FILES 512u

struct initramfs_entry {
    uint64_t path_hash;
    uint32_t path_length;
    const void *data;
    uint32_t size;
};

static const uint8_t *archive;
static uint64_t archive_size;
static struct initramfs_entry file_index[INITRAMFS_MAX_FILES];
static uint32_t file_count;

static uint32_t align4(uint32_t value) { return (value + 3u) & ~3u; }

static uint64_t path_hash(const char *path, uint32_t length) {
    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i < length; i++) {
        hash ^= (uint8_t)path[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool hex8(const uint8_t *text, uint32_t *value) {
    uint32_t result = 0;
    for (uint32_t i = 0; i < 8; i++) {
        uint8_t c = text[i];
        uint32_t digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        result = (result << 4) | digit;
    }
    *value = result;
    return true;
}

static bool next_entry(uint32_t *offset, const char **name, uint32_t *name_size,
                       const void **data, uint32_t *data_size) {
    if (!archive || !offset || *offset > archive_size ||
        archive_size - *offset < CPIO_HEADER_SIZE) return false;
    const uint8_t *header = archive + *offset;
    if (memcmp(header, "070701", 6) && memcmp(header, "070702", 6)) return false;
    uint32_t size, namesize;
    if (!hex8(header + 54, &size) || !hex8(header + 94, &namesize) || !namesize) return false;
    uint64_t name_at = (uint64_t)*offset + CPIO_HEADER_SIZE;
    uint64_t data_at = (uint64_t)align4((uint32_t)(name_at + namesize));
    uint64_t next = (uint64_t)align4((uint32_t)(data_at + size));
    if (name_at + namesize > archive_size || data_at + size > archive_size || next > archive_size)
        return false;
    *name = (const char *)(archive + name_at);
    *name_size = namesize - 1;
    *data = archive + data_at;
    *data_size = size;
    *offset = (uint32_t)next;
    return true;
}

bool initramfs_boot_image(const void **data, uint64_t *size) {
    if (!data || !size) return false;
    return boot_get_module(INITRAMFS_BOOT_PATH, data, size)
        || boot_get_module(INITRAMFS_ESP_ALIAS, data, size)
        || boot_get_module("/boot/" INITRAMFS_ESP_ALIAS_NAME, data, size);
}

bool initramfs_mount(void) {
    const void *data = 0;
    uint64_t size = 0;
    if (!initramfs_boot_image(&data, &size) || !data ||
        size < CPIO_HEADER_SIZE || size > UINT32_MAX) return false;
    archive = data;
    archive_size = size;
    file_count = 0;
    memset(file_index, 0, sizeof(file_index));
    uint32_t offset = 0;
    const char *name;
    uint32_t namesize, filesize;
    const void *filedata;
    while (next_entry(&offset, &name, &namesize, &filedata, &filesize)) {
        if (namesize == 10 && memcmp(name, "TRAILER!!!", 10) == 0) {
            klogf(KLOG_OK, "initramfs: mounted %u files (%llu KiB)", file_count,
                  (unsigned long long)(archive_size / 1024));
            return true;
        }
        if (file_count >= INITRAMFS_MAX_FILES) {
            klog(KLOG_ERROR, "initramfs: file index capacity exceeded");
            break;
        }
        file_index[file_count].path_hash = path_hash(name, namesize);
        file_index[file_count].path_length = namesize;
        file_index[file_count].data = filedata;
        // file_index[file_count].data = filedata;
        file_index[file_count].size = filesize;
        file_count++;
    }
    archive = 0;
    archive_size = 0;
    file_count = 0;
    klog(KLOG_ERROR, "initramfs: invalid CPIO archive");
    return false;
}

bool initramfs_is_mounted(void) { return archive != 0; }

bool initramfs_find(const char *path, const void **data, uint32_t *size) {
    if (!archive || !path || !data || !size) return false;
    while (*path == '/') path++;
    uint32_t length = (uint32_t)strlen(path);
    uint64_t hash = path_hash(path, length);
    for (uint32_t i = 0; i < file_count; i++) {
        if (file_index[i].path_length == length &&
            file_index[i].path_hash == hash) {
            *data = file_index[i].data;
            *size = file_index[i].size;
            return true;
        }
    }
    return false;
}

static int32_t list_common(const char *path, void *entries, uint32_t capacity, bool long_names) {
    if (!archive || !path || !entries || !capacity) return FS_ERROR_INVALID;
    while (*path == '/') path++;
    uint32_t prefix = (uint32_t)strlen(path);
    if (prefix && path[prefix - 1] != '/') return FS_ERROR_NOT_DIR;
    uint32_t offset = 0, count = 0, namesize, filesize;
    const char *name;
    const void *filedata;
    while (next_entry(&offset, &name, &namesize, &filedata, &filesize)) {
        if (namesize == 10 && memcmp(name, "TRAILER!!!", 10) == 0) break;
        if (namesize <= prefix || memcmp(name, path, prefix) != 0) continue;
        const char *base = name + prefix;
        uint32_t length = namesize - prefix;
        bool child = true;
        for (uint32_t i = 0; i < length; i++) if (base[i] == '/') { child = false; break; }
        if (!child || count >= capacity) continue;
        if (long_names) {
            struct fs_directory_entry_long *out = entries;
            uint32_t n = length < FS_LONG_NAME_CAPACITY - 1 ? length : FS_LONG_NAME_CAPACITY - 1;
            memcpy(out[count].name, base, n); out[count].name[n] = 0;
            out[count].size = filesize; out[count].attributes = 0; memset(out[count].reserved, 0, 3);
        } else {
            struct fs_directory_entry *out = entries;
            uint32_t n = length < FS_DIRECTORY_NAME_CAPACITY - 1 ? length : FS_DIRECTORY_NAME_CAPACITY - 1;
            memcpy(out[count].name, base, n); out[count].name[n] = 0;
            out[count].size = filesize; out[count].attributes = 0;
        }
        count++;
    }
    return (int32_t)count;
}

int32_t initramfs_list(const char *path, struct fs_directory_entry *entries, uint32_t capacity) {
    return list_common(path, entries, capacity, false);
}
int32_t initramfs_list_long(const char *path, struct fs_directory_entry_long *entries, uint32_t capacity) {
    return list_common(path, entries, capacity, true);
}
