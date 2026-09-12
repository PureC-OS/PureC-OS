#pragma once
#include <stdint.h>

#define SYS_WRITE   1
#define SYS_CLEAR   2
#define SYS_SLEEP   3
#define SYS_GETPID  39
#define SYS_EXEC    59
#define SYS_EXIT    60
#define SYS_WAIT    61
#define SYS_DRAW_RECT 100
#define SYS_DRAW_LINE 101
#define SYS_GET_MOUSE 102
#define SYS_FB_INFO 103
#define SYS_DRAW_TEXT 104
#define SYS_DRAW_TEXT_SIZED 105
#define SYS_SCROLL_RECT_UP 106
#define SYS_SET_FONT_FACE 107
#define SYS_GET_FONT_FACE 108
#define SYS_FB_BEGIN_UPDATE 109
#define SYS_FB_END_UPDATE 110
#define SYS_FILE_OPEN   200
#define SYS_FILE_READ   201
#define SYS_FILE_DELETE 202
#define SYS_FILE_RENAME 203
#define SYS_FILE_MOVE   204
#define SYS_DIR_LIST    205
#define SYS_FILE_CREATE 206
#define SYS_DIR_CREATE  207
#define SYS_DISK_LIST   208
#define SYS_STORAGE_CONTROLLERS 209
#define SYS_FAT32_FORMAT 210
#define SYS_FILE_WRITE   211
#define SYS_USB_RESCAN   212
#define SYS_FAT32_FORMAT_FORCE 213
#define SYS_FAT32_FORMAT_UEFI 214
#define SYS_CPU_INFO 215
#define SYS_MEMORY_INFO 216
#define SYS_DISK_STATS 217
#define SYS_REBOOT 218
#define SYS_SHUTDOWN 219
#define SYS_BATTERY_INFO 220
#define SYS_SCHED_YIELD 221
#define SYS_FILE_CLOSE 222
#define SYS_FILE_APPEND 223
#define SYS_GETCHAR 224
#define SYS_INSTALL_START 225
#define SYS_INSTALL_STATUS 226
#define SYS_TRY_GETCHAR 227
#define SYS_INSTALL_LOG 228
#define SYS_AUDIO_PLAY_TONE 229
#define SYS_AUDIO_GET_STATUS 230
#define SYS_AUDIO_GET_VOLUME 231
#define SYS_AUDIO_SET_VOLUME 232
#define SYS_AUDIO_IS_MUTED 233
#define SYS_AUDIO_SET_MUTED 234
#define SYS_AUDIO_ADJUST_VOLUME 235
#define SYS_AUDIO_PLAY_TEST_SOUND 236
#define SYS_AUDIO_UPDATE 237
#define SYS_AUDIO_SELECT_OUTPUT_DEVICE 238
#define SYS_GET_COMMAND_LINE 239
#define SYS_ENV_GET 240
#define SYS_ENV_SET 241
#define SYS_ENV_UNSET 242
#define SYS_ENV_LIST 243
#define SYS_GET_PROCESS_NAME 244
#define SYS_MOUSE_DEBUG_GET 245
#define SYS_MOUSE_DEBUG_SET 246
#define SYS_CONSOLE_CONFIGURE 247
#define SYS_CONSOLE_CLEAR 248
#define SYS_CONSOLE_DISABLE 249
#define SYS_DESKTOP_REDRAW 250
#define SYS_GUI_WINDOW_REGISTER 251
#define SYS_GUI_WINDOW_UPDATE 252
#define SYS_GUI_WINDOW_UNREGISTER 253
#define SYS_GUI_WINDOW_STATE 254
#define SYS_GUI_WINDOW_REPAINT_DONE 255
#define SYS_HEAP_GROW 256
#define SYS_PROCESS_LIST 257
#define SYS_TRY_GET_SPECIAL 258
#define SYS_NET_PING 259
#define SYS_WIFI_SCAN 260
#define SYS_WIFI_LIST 261
#define SYS_WIFI_CONNECT 262
#define SYS_WIFI_DISCONNECT 263
#define SYS_WIFI_STATUS 264
#define SYS_SAVE_KLOG 265
#define SYS_GET_ROOT_DEVICE 266
#define SYS_FAT32_FORMAT_CUSTOM 267
#define SYS_FORMAT_DEVICE_EX 268
#define SYS_INSTALL_START_EX 269
#define SYS_AUDIO_STOP_TONE 277
#define SYS_AUDIO_PCM_PUSH 278
#define SYS_AUDIO_PCM_START 279
#define SYS_AUDIO_PCM_STOP 280

#define FS_TYPE_FAT32 0
#define FS_TYPE_EXT2 1
#define FS_TYPE_AUTO 255

struct format_request {
    char device[32];
    char serial[32];
    char erase[8];
    uint8_t fs_type;
    uint8_t reserved[3];
};

struct install_start_request {
    char device[32];
    char serial[32];
    uint8_t fs_type;
    uint8_t reserved[3];
};

#define NETWORK_PING_TARGET_CAPACITY 128

struct network_ping_request {
    char target[NETWORK_PING_TARGET_CAPACITY];
    uint32_t timeout_ms;
    uint16_t sequence;
    uint16_t reserved;
};

struct network_ping_result {
    uint32_t address;
    uint32_t round_trip_ms;
    uint16_t sequence;
    uint8_t ttl;
    uint8_t reserved;
};

#define GUI_WINDOW_STATE_FOCUSED 1
#define GUI_WINDOW_STATE_REPAINT 2

struct gui_window_request {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

#define PROCESS_ENVIRONMENT_LIMIT 16
#define PROCESS_ENVIRONMENT_NAME_LIMIT 32
#define PROCESS_ENVIRONMENT_VALUE_LIMIT 128

struct process_environment_variable {
    uint8_t used;
    char name[PROCESS_ENVIRONMENT_NAME_LIMIT];
    char value[PROCESS_ENVIRONMENT_VALUE_LIMIT];
};

#define SYS_OPEN  SYS_FILE_OPEN
#define SYS_READ  SYS_FILE_READ
#define SYS_CLOSE SYS_FILE_CLOSE
#define SYS_UNLINK SYS_FILE_DELETE
#define SYS_RENAME SYS_FILE_RENAME
#define SYS_MKDIR SYS_DIR_CREATE

#define CPU_MONITOR_NAME_CAPACITY 49

struct cpu_monitor_info {
    char name[CPU_MONITOR_NAME_CAPACITY];
    uint32_t logical_processors;
    uint32_t usage_percent;
    uint64_t frequency_hz;
    uint64_t uptime_ms;
};

struct memory_monitor_info {
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t available_bytes;
    uint64_t framebuffer_bytes;
};

#define PROCESS_MONITOR_NAME_CAPACITY 32
#define PROCESS_MONITOR_STATE_READY 1
#define PROCESS_MONITOR_STATE_RUNNING 2
#define PROCESS_MONITOR_STATE_EXITED 3

struct process_monitor_info {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t state;
    int32_t exit_code;
    uint32_t cpu_percent;
    uint32_t reserved;
    uint64_t runtime_ms;
    uint64_t resident_bytes;
    char name[PROCESS_MONITOR_NAME_CAPACITY];
};

struct disk_monitor_info {
    uint32_t device_count;
    uint32_t operational_count;
    uint64_t total_bytes;
};

#define INSTALL_STAGE_CAPACITY 48
#define INSTALL_LOG_CAPACITY 16

struct install_status {
    uint32_t state;
    uint32_t progress;
    int32_t result;
    char stage[INSTALL_STAGE_CAPACITY];
};

struct install_log_entry {
    uint32_t progress;
    char stage[INSTALL_STAGE_CAPACITY];
};

struct install_log {
    uint32_t count;
    struct install_log_entry entries[INSTALL_LOG_CAPACITY];
};

struct framebuffer_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size_bytes;
    uint8_t bpp;
    uint8_t available;
    char protocol_name[16];
};

struct framebuffer_text_request {
    uint32_t x;
    uint32_t y;
    const char *text;
    uint32_t fg;
    uint32_t bg;
    uint32_t size;
};

struct framebuffer_scroll_request {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t amount;
    uint32_t fill_color;
};

struct framebuffer_console_request {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t foreground;
    uint32_t background;
};

struct usb_scan_status {
    uint32_t xhci_controllers;
    uint32_t xhci_connected_ports;
    uint32_t xhci_addressed_devices;
    uint32_t xhci_disks;
    uint32_t xhci_hid_mice;
    uint32_t xhci_hid_interfaces;
    uint32_t xhci_hubs;
    uint32_t xhci_mouse_transfer_errors;
    uint32_t xhci_failures;
    uint32_t xhci_stage;
    uint32_t xhci_error;
    uint32_t xhci_last_port;
    uint32_t xhci_portsc;
    uint32_t xhci_completion_code;
    uint32_t xhci_max_ports;
    uint32_t xhci_usb_status;
    uint32_t xhci_scratchpad_count;
    uint32_t ehci_connected_ports;
    uint32_t ehci_high_speed_ports;
    uint32_t ehci_disks;
    uint32_t ehci_failures;
    uint32_t ehci_stage;
};

struct battery_info {
    uint32_t present;
    uint32_t percent;
    uint32_t charging;
    uint32_t remaining_minutes;
    uint32_t voltage_mv;
    uint32_t current_ma;
    char name[32];
    char status_text[32];
};

#define AUDIO_BACKEND_NONE 0
#define AUDIO_BACKEND_PC_SPEAKER 1
#define AUDIO_BACKEND_HDA 2

struct audio_status {
    uint32_t volume;
    uint32_t muted;
    uint32_t backend;
    uint32_t available_backends;
    uint32_t pcm_ready;
    uint32_t test_active;
    uint32_t output_device_count;
    uint32_t selected_output_device;
    uint32_t hda_codec;
    uint32_t hda_dac_node;
    uint32_t hda_pin_node;
};

#define WIFI_SSID_CAPACITY 33
#define WIFI_PASSWORD_CAPACITY 64
#define WIFI_BSSID_CAPACITY 6
#define WIFI_SCAN_MAX 32
#define WIFI_SECURITY_OPEN 0
#define WIFI_SECURITY_WEP 1
#define WIFI_SECURITY_WPA2 2
#define WIFI_SECURITY_WPA3 3
#define WIFI_SECURITY_WPA2_WPA3 4
#define WIFI_STATE_DISCONNECTED 0
#define WIFI_STATE_SCANNING 1
#define WIFI_STATE_CONNECTING 2
#define WIFI_STATE_CONNECTED 3
#define WIFI_STATE_FAILED 4

struct wifi_network_info {
    char ssid[WIFI_SSID_CAPACITY];
    uint8_t bssid[WIFI_BSSID_CAPACITY];
    int8_t rssi;
    uint8_t channel;
    uint8_t security;
    uint8_t reserved;
};

struct wifi_status_info {
    uint32_t state;
    uint32_t connected;
    char ssid[WIFI_SSID_CAPACITY];
    uint8_t bssid[WIFI_BSSID_CAPACITY];
    int8_t rssi;
    uint8_t channel;
    uint8_t security;
    uint8_t reserved;
    int32_t last_error;
    uint32_t ip_address;
    uint8_t mac[WIFI_BSSID_CAPACITY];
    char interface_name[16];
    uint32_t scan_count;
    uint64_t last_scan_ms;
    uint32_t has_device;
};

struct wifi_connect_request {
    char ssid[WIFI_SSID_CAPACITY];
    char password[WIFI_PASSWORD_CAPACITY];
};

struct save_klog_request {
    char device[32];
    char path[64];
};

struct fat32_custom_format_request {
    char device[32];
    uint32_t partition_count;
    uint64_t sizes_gb[4];
    uint32_t reserved;
};

#define SYS_GET_FS_TYPE 270
#define SYS_EXT2_STAT 271
#define SYS_EXT2_INODE 272
#define SYS_EXT2_SUPER 273
#define SYS_EXT2_BLOCKS 274
#define SYS_FILE_SEEK 275
#define SYS_FILE_STAT 276

struct file_stat_info {
    uint64_t size;
    uint32_t is_directory;
    uint32_t reserved;
};

struct ext2_stat_info {
    uint32_t ino;
    uint16_t mode;
    uint16_t links;
    uint32_t size;
    uint32_t blocks;
    uint32_t uid;
    uint32_t gid;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint32_t flags;
    uint32_t blocks_ptr[15];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
};

struct ext2_super_info {
    uint32_t total_inodes;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t groups_count;
    uint32_t first_data_block;
    uint32_t inodes_per_block;
    uint16_t inode_size;
    uint16_t magic;
    uint32_t partition_lba;
    uint32_t state;
    uint32_t errors;
};

struct ext2_blocks_info {
    uint32_t ino;
    uint32_t logical_count;
    uint32_t blocks[64];
};

struct syscall_regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, err;
    uint64_t rip, cs, rflags;
};

void syscall_init(void);
int64_t syscall_handler(struct syscall_regs *regs);
void install_report_ui_crash(int32_t status);
void filesystem_syscall_lock(void);
void filesystem_syscall_unlock(void);
