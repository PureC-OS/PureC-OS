#include "../../libc/include/purec.h"

#define MKFS_DEVICE_CAPACITY 12
#define MKFS_ARGUMENT_CAPACITY 256

/* Поддерживаемые файловые системы — получаем от ядра через макросы */
#define MKFS_FS_FAT32 FS_TYPE_FAT32  /* 0 */
#define MKFS_FS_EXT2  FS_TYPE_EXT2   /* 1 */

static bool space(char c){
    return c == ' ' || c == '\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

static uint32_t token_len(const char *text){
    uint32_t len = 0;
    while(text[len] && !space(text[len])) len++;
    return len;
}

static void print_usage(void){
    pc_write("usage: mkfs <fs-type> <device>\n");
    pc_write("       mkfs --list\n");
    pc_write("\n");
    pc_write("Supported filesystems:\n");
    pc_write("  fat32   FAT32\n");
    pc_write("  ext2    EXT2\n");
    pc_write("\n");
    pc_write("Examples:\n");
    pc_write("  mkfs --list            show available disks\n");
    pc_write("  mkfs fat32 sda         format sda as FAT32\n");
    pc_write("  mkfs ext2 sdb          format sdb as EXT2\n");
}

static void print_size(uint64_t bytes){
    uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    uint64_t mib = 1024ULL * 1024ULL;
    if(bytes >= gib){
        pc_write_u64(bytes / gib);
        pc_write(" GiB");
    } else {
        pc_write_u64(bytes / mib);
        pc_write(" MiB");
    }
}

static int command_list(void){
    struct storage_device_info devices[MKFS_DEVICE_CAPACITY];
    int32_t count = pc_list_disks(devices, MKFS_DEVICE_CAPACITY);
    if(count < 0){
        pc_write("mkfs: cannot enumerate disks\n");
        return 1;
    }
    if(!count){
        pc_write("mkfs: no disks found\n");
        return 0;
    }

    /* Текущая ФС корневого раздела */
    char current_fs[32];
    if(pc_get_fs_type(current_fs, sizeof(current_fs)) < 0){
        pc_copy(current_fs, "unknown", sizeof(current_fs));
    }

    char root_dev[32];
    if(pc_get_root_device(root_dev, sizeof(root_dev)) < 0){
        root_dev[0] = '\0';
    }

    pc_write("Available disks:\n");
    pc_write("  NAME     SIZE        MODEL                   WRITABLE  TRANSPORT\n");
    pc_write("  -------- ----------- ----------------------- --------- ---------\n");

    for(int32_t i = 0; i < count; i++){
        const struct storage_device_info *d = &devices[i];
        pc_write("  ");
        pc_write(d->name);

        /* выравнивание */
        uint32_t nlen = pc_strlen(d->name);
        for(uint32_t j = nlen; j < 9; j++) pc_write(" ");

        uint64_t bytes = (uint64_t)d->sector_count * d->sector_size;
        print_size(bytes);
        pc_write("      ");

        /* модель */
        const char *model = d->model[0] ? d->model : "Unknown";
        pc_write(model);
        uint32_t mlen = pc_strlen(model);
        for(uint32_t j = mlen; j < 24; j++) pc_write(" ");

        pc_write(d->writable ? "rw        " : "ro        ");

        switch(d->transport){
            case 1: pc_write("ATA-PIO"); break;
            case 2: pc_write("AHCI");    break;
            case 3: pc_write("USB-MSC"); break;
            case 4: pc_write("USB-EHCI");break;
            default: pc_write("unknown");break;
        }

        /* пометить системный диск */
        if(root_dev[0] && pc_strcmp(d->name, root_dev) == 0){
            pc_write("  [SYSTEM fs=");
            pc_write(current_fs);
            pc_write("]");
        } else if(!d->operational){
            pc_write("  [offline]");
        }

        pc_write("\n");
    }

    pc_write("\nCurrent root filesystem: ");
    pc_write(current_fs);
    pc_write("\n");
    return 0;
}

static int command_format(const char *fs_arg, const char *dev_arg){
    uint8_t fs_type;
    if(pc_strcmp(fs_arg, "fat32") == 0){
        fs_type = MKFS_FS_FAT32;
    } else if(pc_strcmp(fs_arg, "ext2") == 0){
        fs_type = MKFS_FS_EXT2;
    } else {
        pc_write("mkfs: unknown filesystem: ");
        pc_write(fs_arg);
        pc_write("\n");
        pc_write("      supported: fat32, ext2\n");
        return 1;
    }

    if(!dev_arg[0]){
        pc_write("mkfs: device name required (e.g. sda, sdb)\n");
        pc_write("      use 'mkfs --list' to see available disks\n");
        return 1;
    }

    /* Найти диск и получить serial */
    struct storage_device_info devices[MKFS_DEVICE_CAPACITY];
    int32_t count = pc_list_disks(devices, MKFS_DEVICE_CAPACITY);
    if(count < 0){
        pc_write("mkfs: cannot enumerate disks\n");
        return 1;
    }

    const struct storage_device_info *target = 0;
    for(int32_t i = 0; i < count; i++){
        if(pc_strcmp(devices[i].name, dev_arg) == 0){
            target = &devices[i];
            break;
        }
    }

    if(!target){
        pc_write("mkfs: device not found: ");
        pc_write(dev_arg);
        pc_write("\n");
        pc_write("      use 'mkfs --list' to see available disks\n");
        return 1;
    }

    if(!target->writable){
        pc_write("mkfs: device is read-only: ");
        pc_write(dev_arg);
        pc_write("\n");
        return 1;
    }

    if(!target->operational){
        pc_write("mkfs: device is offline: ");
        pc_write(dev_arg);
        pc_write("\n");
        return 1;
    }

    pc_write("mkfs: formatting ");
    pc_write(dev_arg);
    pc_write(" as ");
    pc_write(fs_type == MKFS_FS_EXT2 ? "ext2" : "fat32");
    pc_write("...\n");

    int32_t result = pc_format_device_ex(target->name, target->serial, fs_type);
    if(result < 0){
        pc_write("mkfs: format failed (error ");
        pc_write_i64(result);
        pc_write(")\n");
        return 1;
    }

    pc_write("mkfs: done\n");
    return 0;
}

int mkfs_main(void){
    char arguments[MKFS_ARGUMENT_CAPACITY];
    if(pc_get_command_line(arguments, sizeof(arguments)) < 0){
        arguments[0] = '\0';
    }

    const char *cursor = skip_spaces(arguments);

    if(!*cursor || pc_strcmp(cursor, "--help") == 0 || pc_strcmp(cursor, "-h") == 0){
        print_usage();
        return 0;
    }

    if(pc_strcmp(cursor, "--list") == 0 || pc_strcmp(cursor, "-l") == 0){
        return command_list();
    }

    /* парсим: mkfs <fs-type> <device> */
    uint32_t fs_len = token_len(cursor);
    char fs_arg[32] = {0};
    if(fs_len >= sizeof(fs_arg)){
        pc_write("mkfs: argument too long\n");
        return 1;
    }
    for(uint32_t i = 0; i < fs_len; i++) fs_arg[i] = cursor[i];
    fs_arg[fs_len] = '\0';

    const char *after_fs = skip_spaces(cursor + fs_len);

    uint32_t dev_len = token_len(after_fs);
    char dev_arg[32] = {0};
    if(dev_len >= sizeof(dev_arg)){
        pc_write("mkfs: device name too long\n");
        return 1;
    }
    for(uint32_t i = 0; i < dev_len; i++) dev_arg[i] = after_fs[i];
    dev_arg[dev_len] = '\0';

    return command_format(fs_arg, dev_arg);
}

void _start(void){
    pc_exit(mkfs_main());
}
