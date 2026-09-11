#include "include/ext2.h"
#include "include/ext2_types.h"
#include "include/ext2_block.h"
#include "include/ext2_super.h"
#include "include/ext2_inode.h"
#include "include/ext2_dir.h"
#include "include/ext2_file.h"
#include "include/ext2_format.h"
#include "../../drivers/storage/block_device.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"

bool ext2_mount_specific(const char *device) {
    if (!device) {
        return false;
    }
    int32_t idx = block_device_find(device);
    if (idx < 0) {
        return false;
    }
    struct storage_device_info info;
    if (!block_device_get_info((uint32_t)idx, &info)) {
        return false;
    }
    if (!block_device_select((uint32_t)idx)) {
        return false;
    }
    uint8_t *sec = ext2_scratch_sector();
    bool has_gpt = block_device_read(1, sec) && memcmp(sec, "EFI PART", 8) == 0;
    if (!has_gpt) {
        if (ext2_super_mount_at(0)) {
            return true;
        }
    }
    if (has_gpt) {
        uint64_t entries_lba = 0;
        for (int b = 0; b < 8; b++) {
            entries_lba |= (uint64_t)sec[72 + b] << (b * 8);
        }
        uint32_t entry_count = ext2_read_u32(sec + 80);
        uint32_t entry_size = ext2_read_u32(sec + 84);
        if (entry_count && entry_size == 128 && entries_lba <= UINT32_MAX) {
            uint32_t per = BLOCK_SECTOR_SIZE / 128;
            uint8_t type_basic[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
            uint8_t type_linux[16] = {0xAF, 0x3C, 0x4C, 0x24, 0x00, 0x00, 0x42, 0x4D, 0xA0, 0x76, 0x4D, 0x89, 0x41, 0xFF, 0x86, 0xFE};
            for (uint32_t i = 0; i < entry_count; i++) {
                if (i % per == 0) {
                    if (!block_device_read((uint32_t)entries_lba + i / per, sec)) {
                        break;
                    }
                }
                uint32_t off = (i % per) * 128;
                bool typed = memcmp(sec + off, type_basic, 16) == 0
                    || memcmp(sec + off, type_linux, 16) == 0;
                if (!typed) {
                    continue;
                }
                uint64_t first = 0;
                for (int b = 0; b < 8; b++) {
                    first |= (uint64_t)sec[off + 32 + b] << (b * 8);
                }
                if (first > UINT32_MAX) {
                    continue;
                }
                if (ext2_super_mount_at((uint32_t)first)) {
                    return true;
                }
            }
        }
    }
    if (!has_gpt && block_device_read(0, sec)) {
        for (int i = 0; i < 4; i++) {
            uint16_t o = 446 + i * 16;
            uint8_t type = sec[o + 4];
            if (type == 0x83) {
                uint32_t lba = ext2_read_u32(sec + o + 8);
                if (lba && ext2_super_mount_at(lba)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool ext2_init(void) {
    struct ext2_volume *vol = ext2_volume();
    if (vol->mounted) {
        return true;
    }
    if (!block_device_init()) {
        return false;
    }
    uint32_t n = block_device_count();
    for (uint32_t d = 0; d < n; d++) {
        if (!block_device_select(d)) {
            continue;
        }
        if (ext2_mount_specific(block_device_name())) {
            return true;
        }
    }
    return false;
}

bool ext2_is_mounted(void) {
    return ext2_volume()->mounted;
}

const char *ext2_device_name(void) {
    return block_device_name();
}

int32_t ext2_open(const char *path) {
    return ext2_file_open(path);
}

int32_t ext2_read(int32_t d, void *b, uint32_t c) {
    return ext2_file_read(d, b, c);
}

int64_t ext2_seek(int32_t d, int64_t offset, uint32_t whence) {
    return ext2_file_seek(d, offset, whence);
}

int32_t ext2_close(int32_t d) {
    return ext2_file_close(d);
}

int32_t ext2_list(const char *path, struct fs_directory_entry *entries, uint32_t capacity) {
    if (!entries || capacity == 0) {
        return -3;
    }
    uint32_t ino;
    int32_t st = ext2_dir_resolve(path, &ino);
    if (st < 0) {
        return st;
    }
    return ext2_dir_list(ino, entries, capacity);
}

int32_t ext2_create_file(const char *path) {
    return ext2_file_create(path);
}

int32_t ext2_write_file(const char *path, const void *buffer, uint32_t count) {
    return ext2_file_write(path, buffer, count);
}

int32_t ext2_append_file(const char *path, const void *buffer, uint32_t count) {
    return ext2_file_append(path, buffer, count);
}

int32_t ext2_create_directory(const char *path) {
    return ext2_file_create_dir(path);
}

int32_t ext2_delete(const char *path) {
    (void)path;
    return -10;
}

int32_t ext2_rename(const char *path, const char *n) {
    (void)path;
    (void)n;
    return -10;
}

int32_t ext2_move(const char *a, const char *b) {
    (void)a;
    (void)b;
    return -10;
}

int32_t ext2_format_device(const char *device_name, const char *serial_confirmation, const char *erase_confirmation) {
    int32_t r = ext2_format_device_impl(device_name, serial_confirmation, erase_confirmation);
    if (r == 0) {
        ext2_volume()->mounted = false;
        return ext2_init() ? 0 : -1;
    }
    return r;
}

int32_t ext2_format_device_force(const char *device_name, const char *serial_confirmation) {
    return ext2_format_device(device_name, serial_confirmation, "ERASE");
}

static const struct ext2_module_ops g_ops = {
    1,
    ext2_init,
    ext2_is_mounted,
    ext2_device_name,
    ext2_open,
    ext2_read,
    ext2_close,
    ext2_list,
    ext2_create_file,
    ext2_write_file,
    ext2_create_directory,
    ext2_format_device,
    ext2_mount_specific,
};

const struct ext2_module_ops *ext2_get_ops(void) {
    return &g_ops;
}
