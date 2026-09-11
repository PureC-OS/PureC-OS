#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"
#include "../terminal/window.h"
#include "progress.h"

#define MAX_INSTALL_DISKS 20

#define INSTALL_IDLE 0
#define INSTALL_RUNNING 1
#define INSTALL_COMPLETE 2
#define INSTALL_FAILED 3

static struct storage_device_info disks[MAX_INSTALL_DISKS];
static int32_t disk_count;
static int32_t selected_disk;
static uint8_t selected_fs = FS_TYPE_FAT32;
static struct terminal_window installer_window;

static void clear_console(void) {
    pc_console_clear();
}

static void print_separator(void) {
    pc_write("------------------------------------------------------------\n");
}

static uint64_t disk_size_mib(const struct storage_device_info *disk) {
    return disk->sector_count * disk->sector_size / (1024u * 1024u);
}

static void print_disk(const struct storage_device_info *disk, int32_t index) {
    pc_write("  ");
    pc_write_i64(index + 1);
    pc_write(") ");
    pc_write(disk->name);
    pc_write("  ");
    pc_write(disk->model);
    pc_write("  ");
    pc_write_u64(disk_size_mib(disk));
    pc_write(" MiB");
    if (!disk->operational) {
        pc_write("  [unavailable]");
    } else if (!disk->writable) {
        pc_write("  [read only]");
    }
    pc_write("\n");
}

static int32_t parse_disk_number(const char *text) {
    int32_t value = 0;
    int32_t index = 0;

    if (!text || !text[0]) {
        return -1;
    }
    while (text[index]) {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10 + text[index] - '0';
        index++;
    }
    if (value < 1 || value > disk_count) {
        return -1;
    }
    return value - 1;
}

static bool wait_for_enter(void) {
    char input[2];
    return terminal_window_read_line(&installer_window,
        "Press Enter to continue.\n",input,sizeof(input));
}

static bool select_disk(void) {
    char input[32];

    for (;;) {
        clear_console();
        pc_write("Pure OS console installer\n");
        print_separator();
        pc_write("Select the target disk. All data on it will be erased.\n\n");
        for (int32_t i = 0; i < disk_count; i++) {
            print_disk(&disks[i], i);
        }

        if(!terminal_window_read_line(&installer_window,
                "\nDisk number (or q to quit): ",input,sizeof(input)))
            return false;
        if (pc_strcmp(input, "q") == 0 || pc_strcmp(input, "Q") == 0) {
            return false;
        }

        int32_t index = parse_disk_number(input);
        if (index < 0) {
            pc_write("Invalid disk number.\n");
            if(!wait_for_enter()) return false;
            continue;
        }
        if (!disks[index].operational || !disks[index].writable) {
            pc_write("This disk cannot be used.\n");
            if(!wait_for_enter()) return false;
            continue;
        }

        selected_disk = index;
        return true;
    }
}

static bool select_fs(void) {
    char input[32];
    for (;;) {
        clear_console();
        pc_write("Select filesystem\n");
        print_separator();
        pc_write("  1) FAT32 (UEFI, default)\n");
        pc_write("  2) EXT2  (Linux compatible, ext2 module)\n");
        if (!terminal_window_read_line(&installer_window, "\nFilesystem [1-2, default 1]: ", input, sizeof(input))) return false;
        if (input[0] == '\0' || pc_strcmp(input, "1") == 0) { selected_fs = FS_TYPE_FAT32; return true; }
        if (pc_strcmp(input, "2") == 0) { selected_fs = FS_TYPE_EXT2; return true; }
        pc_write("Invalid choice.\n");
        if (!wait_for_enter()) return false;
    }
}

static bool confirm_erase(void) {
    char input[32];
    const struct storage_device_info *disk = &disks[selected_disk];

    clear_console();
    pc_write("Confirm installation\n");
    print_separator();
    pc_write("Target: ");
    pc_write(disk->name);
    pc_write("  ");
    pc_write(disk->model);
    pc_write("\nSize:   ");
    pc_write_u64(disk_size_mib(disk));
    pc_write(" MiB\n");
    pc_write("FS:     ");
    pc_write(selected_fs == FS_TYPE_EXT2 ? "ext2\n\n" : "fat32\n\n");
    pc_write("WARNING: every partition and file on this disk will be erased.\n");
    if(!terminal_window_read_line(&installer_window,
            "Type ERASE to start, or anything else to cancel: ",
            input,sizeof(input))) return false;
    return pc_strcmp(input, "ERASE") == 0;
}

static bool write_install_config(void) {
    static const char directory[] = "/purec";
    static const char path[] = "/purec/install.cfg";
    char config[96];
    pc_copy(config, "boot=/bin/init\ninstaller=console-ring3\nfilesystem=", sizeof(config));
    const char *fs = selected_fs == FS_TYPE_EXT2 ? "ext2\n" : "fat32\n";
    uint32_t len = pc_strlen(config);
    pc_copy(config + len, fs, sizeof(config) - len);

    (void)pf_create_dir(directory);
    (void)pf_create_file(path);
    int64_t result = pf_write_file(path, config, sizeof(config) - 1);
    if (result < 0) {
        pc_write("[96%] Cannot write /purec/install.cfg\n");
        return false;
    }
    pc_write("[96%] Installation configuration written\n");
    return true;
}

static void wait_for_reboot(void) {
    char input[32];

    pc_write("\nInstallation completed successfully.\n");
    pc_write("Remove the installation media, type REBOOT and press Enter.\n");
    for (;;) {
        if(!terminal_window_read_line(&installer_window,"> ",input,
                                      sizeof(input))) return;
        if (pc_strcmp(input, "REBOOT") == 0 ||
            pc_strcmp(input, "reboot") == 0) {
            (void)pc_syscall(SYS_REBOOT, 0, 0, 0);
        }
    }
}

static bool run_installation(bool start_job) {
    struct install_status status;
    struct installer_progress_view progress_view;
    installer_progress_init(&progress_view);

    clear_console();
    pc_write("Pure OS installation\n");
    print_separator();

    if (start_job) {
        const struct storage_device_info *disk = &disks[selected_disk];
        int32_t result = pc_install_start_ex(disk->name, disk->serial, selected_fs);
        if (result < 0) {
            pc_write("Could not start installation. Error: ");
            pc_write_i64(result);
            pc_write("\n");
            return false;
        }
    }

    for (;;) {
        if(!terminal_window_service(&installer_window)) return false;
        if (!pc_install_status(&status)) {
            pc_write("Cannot read installation status.\n");
            return false;
        }
        if(pg_window_is_minimized(&installer_window.gui)){
            pc_sleep(20);
            continue;
        }
        installer_progress_update(&progress_view,&status,
                                  start_job ? disks[selected_disk].name
                                            : "active installation",false);
        if (status.state == INSTALL_FAILED) {
            pc_write("\nInstallation failed. Error: ");
            pc_write_i64(status.result);
            pc_write(" (");
            pc_write(installer_progress_error_text(status.result));
            pc_write(")");
            pc_write("\n");
            return false;
        }
        if (status.state == INSTALL_COMPLETE) {
            status.progress=96;
            pc_copy(status.stage,"Writing installation configuration",
                    sizeof(status.stage));
            installer_progress_update(&progress_view,&status,
                                      start_job ? disks[selected_disk].name
                                                : "active installation",true);
            if (!write_install_config()) {
                return false;
            }
            status.progress=100;
            pc_copy(status.stage,"Installation complete",
                    sizeof(status.stage));
            installer_progress_update(&progress_view,&status,
                                      start_job ? disks[selected_disk].name
                                                : "active installation",true);
            pc_write("\nInstallation complete.\n");
            wait_for_reboot();
        }
        pc_sleep(20);
    }
}

static int installer_main(void) {
    struct install_status status;

    disk_count = pc_list_disks(disks, MAX_INSTALL_DISKS);
    if (disk_count <= 0) {
        clear_console();
        pc_write("Pure OS console installer\n");
        print_separator();
        pc_write("No disks were detected.\n");
        return 1;
    }

    if (pc_install_status(&status) &&
        status.state != INSTALL_IDLE && status.state != INSTALL_FAILED) {
        (void)run_installation(false);
    }

    for (;;) {
        if (!select_disk()) {
            if(!pg_window_is_open(&installer_window.gui)) return 0;
            clear_console();
            pc_write("Installation cancelled.\n");
            return 0;
        }
        if (!select_fs()) continue;
        if (!confirm_erase()) {
            continue;
        }
        if (run_installation(true)) {
            return 0;
        }
        if(!pg_window_is_open(&installer_window.gui)) return 0;
        pc_write("\n");
        if(!wait_for_enter()) return 0;
    }
}

void _start(void) {
    if(!terminal_window_init_titled(&installer_window,"PureC Installer"))
        pc_exit(1);
    int status=installer_main();
    terminal_window_shutdown(&installer_window);
    pc_exit(status);
}
