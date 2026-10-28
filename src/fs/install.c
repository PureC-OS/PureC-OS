#include "install.h"
#include "ext2/include/ext2_block.h"
#include "ext2/include/ext2_dir.h"
#include "ext2/include/ext2_file.h"
#include "ext2/include/ext2_format.h"
#include "ext2/include/ext2_inode.h"
#include "ext2/include/ext2_super.h"
#include "ext2/include/ext2_types.h"
#include "vfs.h"

#include "../boot/install_source.h"
#include "../drivers/storage/block_device.h"
#include "../kernel/diagnostics/klog.h"
#include "../lib/string.h"
#include <stddef.h>

#define INSTALL_ESP_START_LBA 2048
#define INSTALL_ESP_ALIGN_SECTORS 2048U
#define INSTALL_ESP_IMAGE_MODULE "/boot/esp.img"
#define INSTALL_MAX_SECTORS UINT32_MAX
#define GPT_ENTRY_COUNT 128
#define GPT_ENTRY_SIZE 128
#define GPT_ENTRY_SECTORS 32
#define GPT_HEADER_SIZE 92

static const uint8_t gpt_esp_type_guid[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

/* ext2.c locates the system partition by the basic-data type GUID. */
static const uint8_t gpt_basic_data_type_guid[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};

static uint8_t sector_buffer[BLOCK_SECTOR_SIZE] __attribute__((aligned(2)));
/* ESP partition size in sectors; set from the prebuilt image before use. */
static uint32_t esp_sectors;

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void write_u64(uint8_t *data, uint64_t value)
{
    write_u32(data, (uint32_t)value);
    write_u32(data + 4, (uint32_t)(value >> 32));
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    for (uint32_t index = 0; index < size; index++)
    {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

static uint32_t crc32(const uint8_t *data, uint32_t size)
{
    return ~crc32_update(0xFFFFFFFFU, data, size);
}

static void generate_guid(uint8_t guid[16], const char *serial, uint32_t salt)
{
    uint32_t state = 2166136261U ^ salt;
    while (serial && *serial)
    {
        state ^= (uint8_t)*serial++;
        state *= 16777619U;
    }
    for (uint8_t index = 0; index < 16; index++)
    {
        state = state * 1664525U + 1013904223U;
        guid[index] = (uint8_t)(state >> 24);
    }
    guid[7] = (uint8_t)((guid[7] & 0x0F) | 0x40);
    guid[8] = (uint8_t)((guid[8] & 0x3F) | 0x80);
}

static void write_gpt_name(uint8_t *entry, const char *name)
{
    for (uint8_t index = 0; name[index] && index < 36; index++)
    {
        write_u16(&entry[56 + index * 2], (uint8_t)name[index]);
    }
}

static void build_gpt_header(uint32_t current_lba, uint32_t backup_lba,
                             uint32_t entries_lba, uint32_t last_usable_lba,
                             const uint8_t disk_guid[16], uint32_t entries_crc)
{
    memset(sector_buffer, 0, BLOCK_SECTOR_SIZE);
    memcpy(&sector_buffer[0], "EFI PART", 8);
    write_u32(&sector_buffer[8], 0x00010000);
    write_u32(&sector_buffer[12], GPT_HEADER_SIZE);
    write_u64(&sector_buffer[24], current_lba);
    write_u64(&sector_buffer[32], backup_lba);
    write_u64(&sector_buffer[40], 34);
    write_u64(&sector_buffer[48], last_usable_lba);
    memcpy(&sector_buffer[56], disk_guid, 16);
    write_u64(&sector_buffer[72], entries_lba);
    write_u32(&sector_buffer[80], GPT_ENTRY_COUNT);
    write_u32(&sector_buffer[84], GPT_ENTRY_SIZE);
    write_u32(&sector_buffer[88], entries_crc);
    write_u32(&sector_buffer[16], crc32(sector_buffer, GPT_HEADER_SIZE));
}

static bool write_gpt_layout(uint32_t total_sectors, const char *serial)
{
    uint32_t data_start = INSTALL_ESP_START_LBA + esp_sectors;
    if (total_sectors <= data_start + GPT_ENTRY_SECTORS + 65535)
    {
        klogf(KLOG_ERROR, "write_gpt: total %u too small", total_sectors);
        return false;
    }

    uint32_t backup_header_lba = total_sectors - 1;
    uint32_t backup_entries_lba = backup_header_lba - GPT_ENTRY_SECTORS;
    uint32_t last_usable_lba = backup_entries_lba - 1;
    uint8_t disk_guid[16];
    uint8_t esp_guid[16];
    uint8_t data_guid[16];
    generate_guid(disk_guid, serial, 0x47505444U ^ total_sectors);
    generate_guid(esp_guid, serial, 0x45535031U ^ total_sectors);
    generate_guid(data_guid, serial, 0x44415441U ^ total_sectors);

    memset(sector_buffer, 0, BLOCK_SECTOR_SIZE);
    uint8_t *partition = &sector_buffer[446];
    partition[1] = 0x00;
    partition[2] = 0x02;
    partition[3] = 0x00;
    partition[4] = 0xEE;
    partition[5] = 0xFF;
    partition[6] = 0xFF;
    partition[7] = 0xFF;
    write_u32(&partition[8], 1);
    write_u32(&partition[12], total_sectors - 1);
    sector_buffer[510] = 0x55;
    sector_buffer[511] = 0xAA;
    if (!block_device_write(0, sector_buffer))
        return false;

    uint32_t entries_crc = 0xFFFFFFFFU;
    for (uint8_t sector = 0; sector < GPT_ENTRY_SECTORS; sector++)
    {
        memset(sector_buffer, 0, BLOCK_SECTOR_SIZE);
        if (sector == 0)
        {
            memcpy(&sector_buffer[0], gpt_esp_type_guid, 16);
            memcpy(&sector_buffer[16], esp_guid, 16);
            write_u64(&sector_buffer[32], INSTALL_ESP_START_LBA);
            write_u64(&sector_buffer[40], data_start - 1);
            write_gpt_name(&sector_buffer[0], "PureC ESP");

            memcpy(&sector_buffer[GPT_ENTRY_SIZE], gpt_basic_data_type_guid, 16);
            memcpy(&sector_buffer[GPT_ENTRY_SIZE + 16], data_guid, 16);
            write_u64(&sector_buffer[GPT_ENTRY_SIZE + 32], data_start);
            write_u64(&sector_buffer[GPT_ENTRY_SIZE + 40], last_usable_lba);
            write_gpt_name(&sector_buffer[GPT_ENTRY_SIZE], "PureC System");
        }
        entries_crc = crc32_update(entries_crc, sector_buffer, BLOCK_SECTOR_SIZE);
        if (!block_device_write(2 + sector, sector_buffer) || !block_device_write(backup_entries_lba + sector, sector_buffer))
        {
            return false;
        }
    }
    entries_crc = ~entries_crc;

    build_gpt_header(1, backup_header_lba, 2, last_usable_lba,
                     disk_guid, entries_crc);
    if (!block_device_write(1, sector_buffer))
        return false;
    build_gpt_header(backup_header_lba, 1, backup_entries_lba, last_usable_lba,
                     disk_guid, entries_crc);
    if (!block_device_write(backup_header_lba, sector_buffer))
        return false;

    klogf(KLOG_OK, "write_gpt: ESP %u..%u data %u..%u",
          INSTALL_ESP_START_LBA, data_start - 1, data_start, last_usable_lba);
    return true;
}

static int32_t payload_mkdir(const char *path)
{
    int32_t status = ext2_create_directory(path);
    if (status == -5)
    {
        uint32_t ino;
        if (ext2_dir_resolve(path, &ino) < 0)
            return FS_ERROR_NOT_DIR;
        uint8_t ibuf[256];
        if (!ext2_inode_read(ino, ibuf))
            return FS_ERROR_IO;
        uint16_t mode = ext2_read_u16(ibuf);
        return ((mode & 0xF000) == EXT2_S_IFDIR) ? 0 : FS_ERROR_NOT_DIR;
    }
    return status;
}

static int32_t payload_write_file(const char *path, const void *data, uint32_t size)
{
    return ext2_write_file(path, data, size);
}

static int32_t payload_write_named(const char *directory, const char *name,
                                   const void *data, uint32_t size)
{
    char full_path[256];
    uint32_t dlen = (uint32_t)strlen(directory);
    uint32_t nlen = (uint32_t)strlen(name);
    if (dlen + 1 + nlen >= sizeof(full_path))
        return FS_ERROR_INVALID;
    memcpy(full_path, directory, dlen);
    if (dlen == 0 || full_path[dlen - 1] != '/')
        full_path[dlen++] = '/';
    memcpy(full_path + dlen, name, nlen);
    full_path[dlen + nlen] = '\0';
    return ext2_write_file(full_path, data, size);
}

static int32_t payload_verify_file(const char *path, uint32_t expected_size)
{
    uint32_t ino;
    if (ext2_dir_resolve(path, &ino) < 0)
        return FS_ERROR_IO;
    uint8_t inode[256];
    if (!ext2_inode_read(ino, inode))
        return FS_ERROR_IO;
    return ext2_read_u32(inode + 4) == expected_size ? 0 : FS_ERROR_IO;
}

static int32_t install_firmware_payload(void)
{
    if (payload_mkdir("/bin/firmware") < 0)
        return FS_ERROR_IO;
    if (payload_mkdir("/bin/firmware/Intel") < 0)
        return FS_ERROR_IO;
    if (payload_mkdir("/bin/firmware/Intel/wifi") < 0)
        return FS_ERROR_IO;

    struct fw_entry
    {
        const char *module_path;
        const char *long_name;
    };
    static const struct fw_entry fw_table[] = {
        {"/firmware/iwlwifi-so-a0-hr-b0-89.ucode", "iwlwifi-so-a0-hr-b0-89.ucode"},
        {"/firmware/iwlwifi-so-a0-hr-b0-86.ucode", "iwlwifi-so-a0-hr-b0-86.ucode"},
        {"/firmware/iwlwifi-so-a0-hr-b0-83.ucode", "iwlwifi-so-a0-hr-b0-83.ucode"},
        {"/firmware/iwlwifi-so-a0-hr-b0-77.ucode", "iwlwifi-so-a0-hr-b0-77.ucode"},
        {"/firmware/iwlwifi-so-a0-hr-b0-74.ucode", "iwlwifi-so-a0-hr-b0-74.ucode"},
        {"/firmware/iwlwifi-so-a0-hr-b0-72.ucode", "iwlwifi-so-a0-hr-b0-72.ucode"},
        {"/firmware/iwlwifi-so-a0-gf-a0-89.ucode", "iwlwifi-so-a0-gf-a0-89.ucode"},
        {"/firmware/iwlwifi-so-a0-gf-a0-86.ucode", "iwlwifi-so-a0-gf-a0-86.ucode"},
        {"/firmware/iwlwifi-so-a0-gf-a0-83.ucode", "iwlwifi-so-a0-gf-a0-83.ucode"},
        {"/firmware/iwlwifi-so-a0-gf-a0-77.ucode", "iwlwifi-so-a0-gf-a0-77.ucode"},
        {"/firmware/iwlwifi-so-a0-gf4-a0-89.ucode", "iwlwifi-so-a0-gf4-a0-89.ucode"},
        {"/firmware/iwlwifi-so-a0-gf4-a0-86.ucode", "iwlwifi-so-a0-gf4-a0-86.ucode"},
        {"/firmware/iwlwifi-so-a0-jf-b0-77.ucode", "iwlwifi-so-a0-jf-b0-77.ucode"},
        {"/firmware/iwlwifi-so-a0-jf-b0-72.ucode", "iwlwifi-so-a0-jf-b0-72.ucode"},
        {"/firmware/iwlwifi-QuZ-a0-hr-b0-77.ucode", "iwlwifi-QuZ-a0-hr-b0-77.ucode"},
        {"/firmware/iwlwifi-QuZ-a0-hr-b0-74.ucode", "iwlwifi-QuZ-a0-hr-b0-74.ucode"},
        {"/firmware/iwlwifi-Qu-b0-hr-b0-77.ucode", "iwlwifi-Qu-b0-hr-b0-77.ucode"},
        {"/firmware/iwlwifi-cc-a0-77.ucode", "iwlwifi-cc-a0-77.ucode"},
    };
    for (uint32_t i = 0; i < sizeof(fw_table) / sizeof(fw_table[0]); i++)
    {
        const void *data = NULL;
        uint64_t size = 0;
        if (!boot_get_module(fw_table[i].module_path, &data, &size) || !data || size > UINT32_MAX || size < 8)
        {
            klogf(KLOG_INFO, "install: firmware %s not present as Limine module, skip", fw_table[i].long_name);
            continue;
        }
        uint32_t sz = (uint32_t)size;
        if (size >= 8)
        {
            uint32_t magic = ((uint32_t)((const uint8_t *)data)[4]) | ((uint32_t)((const uint8_t *)data)[5] << 8) | ((uint32_t)((const uint8_t *)data)[6] << 16) | ((uint32_t)((const uint8_t *)data)[7] << 24);
            if (magic != 0x0A4C5749U)
            {
                klogf(KLOG_WARN, "install: firmware %s bad magic 0x%08x, still copying", fw_table[i].long_name, magic);
            }
        }
        char long_path[96];
        const char *prefix = "/bin/firmware/Intel/wifi/";
        uint32_t p = 0;
        for (uint32_t k = 0; prefix[k] && p + 1 < sizeof(long_path); k++)
            long_path[p++] = prefix[k];
        for (uint32_t k = 0; fw_table[i].long_name[k] && p + 1 < sizeof(long_path); k++)
            long_path[p++] = fw_table[i].long_name[k];
        long_path[p] = '\0';
        int32_t st = payload_write_file(long_path, data, sz);
        if (st < 0)
        {
            klogf(KLOG_ERROR, "install: write firmware %s failed %d", fw_table[i].long_name, st);
            continue;
        }
        klogf(KLOG_OK, "install: firmware %s (%u KB)", fw_table[i].long_name, sz / 1024U);
        int32_t v = payload_verify_file(long_path, sz);
        if (v < 0)
            klogf(KLOG_WARN, "install: verify firmware %s failed %d", long_path, v);
    }
    klog(KLOG_OK, "install: firmware payload complete");
    return 0;
}

#define MANIFEST_LINE_MAX 256
#define MANIFEST_FIELD_MAX 128

struct header_module
{
    const char *module;
    const char *directory;
    const char *name;
    const char *alias_path;
    const char *alias_name;
    bool required;
};

static int32_t install_header_module(const struct header_module *entry)
{
    const void *data;
    uint64_t size;
    if (!boot_get_module(entry->module, &data, &size) || !data || !size || size > UINT32_MAX)
    {
        klogf(entry->required ? KLOG_ERROR : KLOG_WARN,
              "install: header module %s missing (%s)",
              entry->module, entry->required ? "aborting" : "non-fatal, skipped");
        return entry->required ? FS_ERROR_NOT_FOUND : 0;
    }
    char dest_path[MANIFEST_FIELD_MAX];
    uint32_t dp = 0;
    const char *dp_part = entry->directory;
    while (*dp_part && dp + 1 < sizeof(dest_path))
        dest_path[dp++] = *dp_part++;
    if (dp + 1 < sizeof(dest_path))
        dest_path[dp++] = '/';
    dp_part = entry->name;
    while (*dp_part && dp + 1 < sizeof(dest_path))
        dest_path[dp++] = *dp_part++;
    dest_path[dp] = '\0';
    int32_t status;
    if (entry->alias_path)
    {
        status = payload_write_named(entry->directory, entry->name, data,
                                     (uint32_t)size);
    }
    else
    {
        status = payload_write_file(dest_path, data, (uint32_t)size);
    }
    if (status < 0)
    {
        klogf(entry->required ? KLOG_ERROR : KLOG_WARN,
              "install: write %s failed %d (%s)",
              dest_path, status, entry->required ? "aborting" : "non-fatal, skipped");
        return entry->required ? status : 0;
    }
    status = payload_verify_file(dest_path, (uint32_t)size);
    if (status < 0)
    {
        klogf(entry->required ? KLOG_ERROR : KLOG_WARN,
              "install: verify %s failed %d (%s)",
              dest_path, status,
              entry->required ? "aborting" : "non-fatal, skipped");
        if (entry->required)
            return status;
    }
    return 0;
}

static void manifest_mkdir_parents(const char *dest)
{
    char prefix[MANIFEST_FIELD_MAX];
    uint32_t out = 0;
    for (uint32_t i = 0; dest[i] != '\0' && out + 1 < sizeof(prefix); i++)
    {
        if (dest[i] == '/' && i > 0)
        {
            prefix[out] = '\0';
            (void)payload_mkdir(prefix);
        }
        prefix[out++] = dest[i];
    }
}

static int32_t install_manifest_pass(bool dry_run)
{
    const void *blob;
    uint64_t blob_size;
    if (!boot_get_module("/manifest.txt", &blob, &blob_size) || !blob || !blob_size || blob_size > 65536U)
    {
        klog(KLOG_ERROR, "install: missing /manifest.txt module");
        return FS_ERROR_NOT_FOUND;
    }
    const char *text = (const char *)blob;
    uint64_t pos = 0;
    uint32_t installed = 0;
    char line[MANIFEST_LINE_MAX];
    while (pos < blob_size)
    {
        uint32_t len = 0;
        while (pos < blob_size && text[pos] != '\n' && len + 1 < sizeof(line))
        {
            line[len++] = text[pos++];
        }
        if (pos < blob_size && text[pos] == '\n')
        {
            pos++;
        }
        else if (pos < blob_size)
        {
            klog(KLOG_ERROR, "install: manifest line too long");
            return FS_ERROR_INVALID;
        }
        line[len] = '\0';
        uint32_t cursor = 0;
        while (line[cursor] == ' ' || line[cursor] == '\t')
            cursor++;
        if (line[cursor] == '\0' || line[cursor] == '#')
            continue;
        char *cols[5];
        uint32_t ncols = 0;
        while (line[cursor] != '\0' && ncols < 5)
        {
            while (line[cursor] == ' ' || line[cursor] == '\t')
                cursor++;
            if (line[cursor] == '\0')
                break;
            cols[ncols++] = &line[cursor];
            while (line[cursor] != '\0' && line[cursor] != ' ' && line[cursor] != '\t')
                cursor++;
            if (line[cursor] != '\0')
                line[cursor++] = '\0';
        }
        if (ncols != 5)
        {
            klog(KLOG_ERROR, "install: malformed manifest line");
            return FS_ERROR_INVALID;
        }
        const char *module = cols[0];
        const char *dest = cols[1];
        const char *alias = cols[2];
        bool required;
        if (cols[3][0] == '1' && cols[3][1] == '\0')
            required = true;
        else if (cols[3][0] == '0' && cols[3][1] == '\0')
            required = false;
        else
        {
            klog(KLOG_ERROR, "install: bad required flag in manifest");
            return FS_ERROR_INVALID;
        }
        if (module[0] != '/')
        {
            klog(KLOG_ERROR, "install: bad module path in manifest");
            return FS_ERROR_INVALID;
        }
        bool use_alias = true;
        if (alias[0] == '-' && alias[1] == '\0')
            use_alias = false;
        else if (alias[0] != '/')
        {
            klog(KLOG_ERROR, "install: bad alias path in manifest");
            return FS_ERROR_INVALID;
        }
        const char *slash = 0;
        for (uint32_t i = 0; dest[i] != '\0'; i++)
        {
            if (dest[i] == '/')
                slash = &dest[i];
        }
        if (!slash || slash == dest)
        {
            klog(KLOG_ERROR, "install: bad dest path in manifest");
            return FS_ERROR_INVALID;
        }
        char directory[MANIFEST_FIELD_MAX];
        char name[64];
        uint32_t dir_len = (uint32_t)(slash - dest);
        if (dir_len == 0 || dir_len >= sizeof(directory))
        {
            klog(KLOG_ERROR, "install: dest directory too long");
            return FS_ERROR_INVALID;
        }
        for (uint32_t i = 0; i < dir_len; i++)
            directory[i] = dest[i];
        directory[dir_len] = '\0';
        uint32_t name_len = 0;
        const char *np = slash + 1;
        while (np[name_len] != '\0' && name_len + 1 < sizeof(name))
        {
            name[name_len] = np[name_len];
            name_len++;
        }
        name[name_len] = '\0';
        if (np[name_len] != '\0' || name_len == 0)
        {
            klog(KLOG_ERROR, "install: bad file name in manifest");
            return FS_ERROR_INVALID;
        }
        char alias_name[32];
        const char *alias_name_ptr = 0;
        if (use_alias)
        {
            const char *aslash = 0;
            for (uint32_t i = 0; alias[i] != '\0'; i++)
            {
                if (alias[i] == '/')
                    aslash = &alias[i];
            }
            if (!aslash || aslash[1] == '\0')
            {
                klog(KLOG_ERROR, "install: bad alias path in manifest");
                return FS_ERROR_INVALID;
            }
            uint32_t an = 0;
            const char *ap = aslash + 1;
            while (ap[an] != '\0' && an + 1 < sizeof(alias_name))
            {
                alias_name[an] = ap[an];
                an++;
            }
            alias_name[an] = '\0';
            if (ap[an] != '\0')
            {
                klog(KLOG_ERROR, "install: alias name too long");
                return FS_ERROR_INVALID;
            }
            alias_name_ptr = alias_name;
        }
        if (dry_run)
        {
            if (required)
            {
                const void *probe;
                uint64_t probe_size;
                if (!boot_get_module(module, &probe, &probe_size) || !probe || !probe_size || probe_size > UINT32_MAX)
                {
                    klogf(KLOG_ERROR, "install: manifest preflight missing %s",
                          module);
                    return FS_ERROR_NOT_FOUND;
                }
            }
            installed++;
            continue;
        }
        manifest_mkdir_parents(dest);
        struct header_module entry;
        entry.module = module;
        entry.directory = directory;
        entry.name = name;
        entry.alias_path = use_alias ? alias : 0;
        entry.alias_name = alias_name_ptr;
        entry.required = required;
        int32_t status = install_header_module(&entry);
        if (status < 0)
            return status;
        installed++;
    }
    if (dry_run)
    {
        klogf(KLOG_OK, "install: manifest preflight ok entries=%u", installed);
        return 0;
    }
    klogf(KLOG_OK, "install: manifest payload done entries=%u", installed);
    return 0;
}

static int32_t install_standard_directories(void)
{
    static const char *const directories[] = {
        "/etc", "/home", "/root", "/mnt", "/opt", "/srv", "/var",
        "/var/log", "/var/tmp", "/var/cache", "/usr", "/usr/bin", "/usr/lib",
        "/usr/local", "/usr/local/bin", "/usr/local/lib", "/usr/share",
        "/usr/games", "/sbin", "/boot"};
    for (uint32_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++)
    {
        int32_t status = payload_mkdir(directories[i]);
        if (status < 0)
        {
            klogf(KLOG_ERROR, "install: mkdir %s failed %d", directories[i], status);
            return status;
        }
    }
    return 0;
}

static int32_t install_program_payload(void)
{
    int32_t status = install_standard_directories();
    if (status < 0)
        return status;
    status = install_manifest_pass(true);
    if (status < 0)
        return status;
    status = install_manifest_pass(false);
    if (status < 0)
        return status;
    int32_t fw_st = install_firmware_payload();
    if (fw_st < 0)
        klogf(KLOG_WARN, "install: firmware payload failed %d (non-fatal, will continue)", fw_st);
    return 0;
}

#define ESP_WRITE_CHUNK_SECTORS 2048U

static int32_t install_write_esp_image(const void *image, uint64_t image_size,
                                       install_progress_callback callback)
{
    uint32_t image_sectors = (uint32_t)(image_size / BLOCK_SECTOR_SIZE);
    const uint8_t *src = (const uint8_t *)image;
    uint32_t done = 0;
    while (done < image_sectors)
    {
        uint32_t count = image_sectors - done;
        if (count > ESP_WRITE_CHUNK_SECTORS)
            count = ESP_WRITE_CHUNK_SECTORS;
        if (!block_device_write_sectors(INSTALL_ESP_START_LBA + done,
                                        src + (uint64_t)done * BLOCK_SECTOR_SIZE, count))
        {
            klogf(KLOG_ERROR, "install: ESP write failed at sector %u/%u", done, image_sectors);
            return FS_ERROR_IO;
        }
        done += count;
        if (callback)
            callback(20 + (uint32_t)((uint64_t)done * 45 / image_sectors), "Writing EFI system partition image");
    }
    if (!block_device_flush())
        return FS_ERROR_IO;
    uint8_t check[BLOCK_SECTOR_SIZE];
    if (!block_device_read(INSTALL_ESP_START_LBA, check) ||
        memcmp(check, src, BLOCK_SECTOR_SIZE) != 0 || check[510] != 0x55 || check[511] != 0xAA ||
        !block_device_read(INSTALL_ESP_START_LBA + image_sectors - 1, check) ||
        memcmp(check, src + (uint64_t)(image_sectors - 1) * BLOCK_SECTOR_SIZE, BLOCK_SECTOR_SIZE) != 0)
    {
        klog(KLOG_ERROR, "install: ESP image verify failed");
        return FS_ERROR_IO;
    }
    klogf(KLOG_OK, "install: ESP image written (%u sectors)", image_sectors);
    return 0;
}

int32_t installer_install_device(const char *device_name,
                                 const char *serial_confirmation,
                                 install_progress_callback callback)
{
    if (callback)
        callback(2, "Validating target disk");
    if (!device_name || !serial_confirmation)
        return FS_ERROR_INVALID;
    int32_t idx = block_device_find(device_name);
    if (idx < 0)
        return FS_ERROR_NOT_FOUND;
    struct storage_device_info info;
    if (!block_device_get_info((uint32_t)idx, &info))
        return FS_ERROR_INVALID;
    if (!info.operational || !info.writable)
        return FS_ERROR_READ_ONLY;
    if (info.serial[0] && strcmp(info.serial, serial_confirmation) != 0)
        return FS_ERROR_CONFIRMATION;
    if (info.sector_size != BLOCK_SECTOR_SIZE || info.sector_count > INSTALL_MAX_SECTORS)
    {
        klogf(KLOG_ERROR, "install: UNSUPPORTED ss=%u expected %u sectors=%llu max=%u",
              info.sector_size, BLOCK_SECTOR_SIZE, info.sector_count,
              INSTALL_MAX_SECTORS);
        return FS_ERROR_UNSUPPORTED;
    }
    const void *esp_image;
    uint64_t esp_image_size;
    if (!boot_get_module(INSTALL_ESP_IMAGE_MODULE, &esp_image, &esp_image_size) ||
        esp_image_size < BLOCK_SECTOR_SIZE || esp_image_size > UINT32_MAX ||
        (esp_image_size % BLOCK_SECTOR_SIZE) != 0)
    {
        klog(KLOG_ERROR, "install: missing or invalid " INSTALL_ESP_IMAGE_MODULE " module");
        return FS_ERROR_NOT_FOUND;
    }
    esp_sectors = (uint32_t)(esp_image_size / BLOCK_SECTOR_SIZE);
    esp_sectors = (esp_sectors + INSTALL_ESP_ALIGN_SECTORS - 1) & ~(INSTALL_ESP_ALIGN_SECTORS - 1);
    uint32_t total_sectors = (uint32_t)info.sector_count;
    uint32_t data_start = INSTALL_ESP_START_LBA + esp_sectors;
    if (total_sectors <= data_start + GPT_ENTRY_SECTORS + 65535)
        return FS_ERROR_TOO_SMALL;
    uint32_t data_sectors = total_sectors - data_start - GPT_ENTRY_SECTORS - 1;
    if (!block_device_select((uint32_t)idx))
        return FS_ERROR_INVALID;
    if (callback)
        callback(8, "Writing GPT partition table");
    klogf(KLOG_INFO, "install: %s total %u ESP %u sectors data %u sectors",
          device_name, total_sectors, esp_sectors, data_sectors);
    if (!write_gpt_layout(total_sectors, info.serial) || !block_device_flush())
    {
        klog(KLOG_ERROR, "install: GPT write failed");
        return FS_ERROR_IO;
    }
    if (callback)
        callback(20, "Writing EFI system partition image");
    int32_t esp_status = install_write_esp_image(esp_image, esp_image_size, callback);
    if (esp_status < 0)
        return esp_status;
    if (callback)
        callback(70, "Formatting PureC system partition as ext2");
    if (ext2_format_at(data_start, data_sectors) != 0)
    {
        klog(KLOG_ERROR, "install: ext2 system partition format failed");
        return FS_ERROR_IO;
    }
    if (!block_device_flush())
        return FS_ERROR_IO;
    ext2_volume()->mounted = false;
    if (!ext2_super_mount_at(data_start))
    {
        klog(KLOG_ERROR, "install: ext2 system partition mount failed");
        return FS_ERROR_IO;
    }
    vfs_set_active_fs(VFS_FS_EXT2);
    if (callback)
        callback(88, "Copying programs and libraries");
    int32_t status = install_program_payload();
    if (status < 0)
    {
        klogf(KLOG_ERROR, "install: system payload failed %d", status);
        return status;
    }
    if (!block_device_flush())
        return FS_ERROR_IO;
    if (callback)
        callback(90, "System partition mounted");
    klog(KLOG_OK, "install: GPT and ext2 system partition ready");
    return 0;
}

int32_t installer_format_custom_device(const char *device, uint32_t partition_count, const uint64_t *sizes_gb)
{
    if (!device || !device[0] || !partition_count || partition_count > 4 || !sizes_gb)
        return FS_ERROR_INVALID;
    int32_t idx = block_device_find(device);
    if (idx < 0)
        return FS_ERROR_NOT_FOUND;
    struct storage_device_info info;
    if (!block_device_get_info((uint32_t)idx, &info))
        return FS_ERROR_INVALID;
    if (!info.operational || !info.writable)
        return FS_ERROR_READ_ONLY;
    if (info.sector_size != BLOCK_SECTOR_SIZE)
        return FS_ERROR_UNSUPPORTED;
    uint32_t total_sectors = (uint32_t)info.sector_count;
    if (total_sectors < 65536)
        return FS_ERROR_TOO_SMALL;
    uint64_t total_gb = 0;
    for (uint32_t i = 0; i < partition_count; i++)
        total_gb += sizes_gb[i];
    uint64_t total_bytes_needed = total_gb * 1024ULL * 1024ULL * 1024ULL;
    uint64_t disk_bytes = (uint64_t)total_sectors * BLOCK_SECTOR_SIZE;
    if (total_bytes_needed == 0 || total_bytes_needed > disk_bytes - 2 * 1024 * 1024)
        return FS_ERROR_NO_SPACE;
    if (!block_device_select((uint32_t)idx))
        return FS_ERROR_INVALID;
    uint32_t current_lba = 34;
    uint32_t part_starts[4], part_sectors_arr[4];
    for (uint32_t i = 0; i < partition_count; i++)
    {
        uint64_t bytes = sizes_gb[i] * 1024ULL * 1024ULL * 1024ULL;
        uint32_t sectors = (uint32_t)(bytes / BLOCK_SECTOR_SIZE);
        if (sectors < 32768)
            sectors = 32768;
        part_starts[i] = current_lba;
        part_sectors_arr[i] = sectors;
        current_lba += sectors;
        uint32_t align = 2048;
        uint32_t rem = current_lba % align;
        if (rem)
            current_lba += align - rem;
    }
    if (current_lba > total_sectors - 33)
        return FS_ERROR_NO_SPACE;
    uint8_t disk_guid[16], part_guid[16];
    generate_guid(disk_guid, info.serial, 0x47505444U ^ total_sectors);
    memset(sector_buffer, 0, BLOCK_SECTOR_SIZE);
    sector_buffer[510] = 0x55;
    sector_buffer[511] = 0xAA;
    sector_buffer[446 + 4] = 0xEE;
    write_u32(&sector_buffer[446 + 8], 1);
    write_u32(&sector_buffer[446 + 12], total_sectors - 1);
    if (!block_device_write(0, sector_buffer))
        return FS_ERROR_IO;
    uint32_t entries_crc = 0xFFFFFFFFU;
    uint8_t gpt_entries[32 * 128] = {0};
    for (uint32_t i = 0; i < partition_count; i++)
    {
        uint8_t *e = &gpt_entries[i * 128];
        memcpy(e, gpt_basic_data_type_guid, 16);
        generate_guid(part_guid, info.serial, 0x44415441U ^ i ^ total_sectors);
        memcpy(e + 16, part_guid, 16);
        write_u64(e + 32, part_starts[i]);
        write_u64(e + 40, part_starts[i] + part_sectors_arr[i] - 1);
        write_gpt_name(e, i == 0 ? "PureC System" : "PureC Data");
    }
    entries_crc = crc32(gpt_entries, sizeof(gpt_entries));
    build_gpt_header(1, total_sectors - 1, 2, total_sectors - 33 - 1, disk_guid, entries_crc);
    if (!block_device_write(1, sector_buffer))
        return FS_ERROR_IO;
    for (uint32_t s = 0; s < 32; s++)
    {
        memset(sector_buffer, 0, BLOCK_SECTOR_SIZE);
        memcpy(sector_buffer, &gpt_entries[s * 512], 512);
        if (!block_device_write(2 + s, sector_buffer))
            return FS_ERROR_IO;
    }
    build_gpt_header(total_sectors - 1, 1, total_sectors - 32, total_sectors - 33 - 1, disk_guid, entries_crc);
    if (!block_device_write(total_sectors - 1, sector_buffer))
        return FS_ERROR_IO;
    for (uint32_t s = 0; s < 32; s++)
    {
        memset(sector_buffer, 0, BLOCK_SECTOR_SIZE);
        memcpy(sector_buffer, &gpt_entries[s * 512], 512);
        if (!block_device_write(total_sectors - 32 + s, sector_buffer))
            return FS_ERROR_IO;
    }
    for (uint32_t i = 0; i < partition_count; i++)
    {
        if (ext2_format_at(part_starts[i], part_sectors_arr[i]) != 0)
            return FS_ERROR_IO;
    }
    if (!block_device_flush())
        return FS_ERROR_IO;
    block_device_select((uint32_t)idx);
    ext2_volume()->mounted = false;
    if (ext2_super_mount_at(part_starts[0]))
        vfs_set_active_fs(VFS_FS_EXT2);
    else
        vfs_mount_root();
    klogf(KLOG_OK, "install_custom: device %s formatted %u ext2 partitions", device, partition_count);
    for (uint32_t i = 0; i < partition_count; i++)
        klogf(KLOG_INFO, " install_custom: part%u start %u sectors %u (%llu GB)", i, part_starts[i], part_sectors_arr[i], sizes_gb[i]);
    return 0;
}
