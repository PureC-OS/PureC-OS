#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "syscall_nr.h"

#define SYS_OPEN SYS_FILE_OPEN
#define SYS_READ SYS_FILE_READ
#define SYS_CLOSE SYS_FILE_CLOSE
#define SYS_UNLINK SYS_FILE_DELETE
#define SYS_RENAME SYS_FILE_RENAME
#define SYS_MKDIR SYS_DIR_CREATE


#define TIME_ZONE_GET 0
#define TIME_ZONE_SET 1

#define FD_CTL_GETFD 1
#define FD_CTL_SETFD 2
#define FD_CTL_DUPFD 3
#define FD_CTL_KIND 4
#define FD_CTL_DUPFD_CLOEXEC 5
#define FD_CTL_GETFL 6
#define FD_CTL_SETFL 7

#define FD_KIND_CONSOLE 0
#define FD_KIND_FILE 1
#define FD_KIND_PIPE 2
#define FD_KIND_SOCKET 3
#define FD_KIND_EVENT 4
#define FD_KIND_PTY 5
#define FD_KIND_INPUT 6

struct fb_map_info {
  uint64_t address;
  uint64_t bytes;
  uint32_t width;
  uint32_t height;
  uint32_t pitch_bytes;
  uint32_t bpp;
  uint32_t generation;
  uint32_t reserved;
};

#define INPUT_RECORD_KEY 1
#define INPUT_RECORD_MOUSE 2

struct input_record {
  uint32_t type;
  uint32_t time_ms;
  int32_t x;
  int32_t y;
  int32_t dx;
  int32_t dy;
  int32_t wheel;
  uint32_t buttons;
  uint16_t scancode;
  uint8_t pressed;
  uint8_t repeat;
  uint8_t mods;
  uint8_t reserved[3];
};

#define TTY_TCGETS 0x5401u
#define TTY_TCSETS 0x5402u
#define TTY_TCSETSW 0x5403u
#define TTY_TCSETSF 0x5404u
#define TTY_TCFLSH 0x540Bu
#define TTY_TIOCGWINSZ 0x5413u
#define TTY_TIOCSWINSZ 0x5414u
#define TTY_FIONREAD 0x541Bu
#define TTY_RAW_ACTIVE 0x7F01u
#define TTY_TIOCSPGRP_PID 0x5410u

#define KPOLLIN 0x001
#define KPOLLOUT 0x004
#define KPOLLERR 0x008
#define KPOLLHUP 0x010
#define KPOLLNVAL 0x020

struct kernel_pollfd {
  int32_t fd;
  int16_t events;
  int16_t revents;
};

#define KMAP_PROT_READ 1
#define KMAP_PROT_WRITE 2
#define KMAP_PROT_EXEC 4
#define KMAP_FIXED 1
#define KMAP_SHARED 2
#define KMADV_DONTNEED 4
#define KMAP_REMAP_MAYMOVE 1

struct kernel_msghdr {
  uint64_t name;
  uint32_t name_length;
  uint32_t reserved0;
  uint64_t iov;
  uint64_t iov_count;
  uint64_t control;
  uint64_t control_length;
  int32_t flags;
  int32_t reserved1;
};

struct kernel_cmsghdr {
  uint64_t length;
  int32_t level;
  int32_t type;
};

#define KMSG_CTRUNC 0x8
#define KMSG_CMSG_CLOEXEC 0x40000000
#define KSCM_RIGHTS 1
#define KSOL_SOCKET 1
#define KSO_PEERCRED 17

struct kernel_thread_args {
  uint64_t entry;
  uint64_t stack;
  uint64_t argument;
  uint64_t tls_base;
  uint64_t clear_tid;
};

struct kernel_mmap_args {
  uint64_t address;
  uint64_t length;
  uint64_t prot;
  uint64_t flags;
  int64_t fd;
  uint64_t offset;
};

#define KERR_PERM 1
#define KERR_NOENT 2
#define KERR_NODEV 19
#define KERR_SRCH 3
#define KERR_INTR 4
#define KERR_IO 5
#define KERR_2BIG 7
#define KERR_NOEXEC 8
#define KERR_BADF 9
#define KERR_CHILD 10
#define KERR_AGAIN 11
#define KERR_NOMEM 12
#define KERR_ACCES 13
#define KERR_FAULT 14
#define KERR_BUSY 16
#define KERR_EXIST 17
#define KERR_NOTDIR 20
#define KERR_INVAL 22
#define KERR_NFILE 23
#define KERR_NOTTY 25
#define KERR_MFILE 24
#define KERR_SPIPE 29
#define KERR_PIPE 32
#define KERR_NAMETOOLONG 36
#define KERR_LOOP 40
#define KERR_NOTSOCK 88
#define KERR_DESTADDRREQ 89
#define KERR_MSGSIZE 90
#define KERR_PROTOTYPE 91
#define KERR_NOPROTOOPT 92
#define KERR_PROTONOSUPPORT 93
#define KERR_OPNOTSUPP 95
#define KERR_AFNOSUPPORT 97
#define KERR_ADDRINUSE 98
#define KERR_ADDRNOTAVAIL 99
#define KERR_NETUNREACH 101
#define KERR_CONNABORTED 103
#define KERR_CONNRESET 104
#define KERR_NOBUFS 105
#define KERR_ISCONN 106
#define KERR_NOTCONN 107
#define KERR_TIMEDOUT 110
#define KERR_CONNREFUSED 111
#define KERR_HOSTUNREACH 113
#define KERR_ALREADY 114
#define KERR_INPROGRESS 115
#define KERR_NOSYS 38
#define KERR_CONSOLE 1000

#define SIGNAL_ACTION_DEFAULT 0ULL
#define SIGNAL_ACTION_IGNORE 1ULL
#define SIGNAL_FLAG_ONSTACK 0x08000000U
#define SIGNAL_FLAG_NODEFER 0x40000000U
#define SIGNAL_FLAG_RESETHAND 0x80000000U

#define KSS_ONSTACK 1
#define KSS_DISABLE 2
#define KMINSIGSTKSZ 2048

struct kernel_stack {
  uint64_t sp;
  int32_t flags;
  int32_t reserved;
  uint64_t size;
};

struct ksigaction {
  uint64_t handler;
  uint64_t mask;
  uint64_t restorer;
  uint32_t flags;
  uint32_t reserved;
};

#define FS_TYPE_EXT2 1
#define FS_TYPE_AUTO 255

struct format_request {
  char device[32];
  char serial[32];
  char erase[8];
  uint8_t fs_type;
  uint8_t reserved[3];
};

#define INSTALL_HOSTNAME_MAX 64
#define INSTALL_USER_MAX 32
#define INSTALL_GECOS_MAX 64
#define INSTALL_HASH_MAX 128
#define INSTALL_FIRST_UID 1000

#define INSTALL_USERS_MAX 8
#define INSTALL_WHEEL_GID 10

struct install_user {
  char name[INSTALL_USER_MAX];
  char gecos[INSTALL_GECOS_MAX];
  char hash[INSTALL_HASH_MAX];
  uint32_t uid;
  uint32_t admin;
};

struct install_account_request {
  uint32_t present;
  uint32_t user_count;
  char hostname[INSTALL_HOSTNAME_MAX];
  char root_hash[INSTALL_HASH_MAX];
  struct install_user users[INSTALL_USERS_MAX];
  char timezone[32];
};

struct install_start_request {
  char device[32];
  char serial[32];
  uint8_t fs_type;
  uint8_t reserved[3];
  struct install_account_request accounts;
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

#define NET_IF_NAME_CAPACITY 16
#define NET_IF_MAX_COUNT 8

#define NET_CFG_SET_IPV4 1
#define NET_CFG_CLEAR_IPV4 2
#define NET_CFG_SET_DNS 3
#define NET_CFG_DHCP_START 4
#define NET_CFG_DHCP_STOP 5

struct net_if_config {
  char name[NET_IF_NAME_CAPACITY];
  uint32_t op;
  uint32_t address;
  uint32_t netmask;
  uint32_t gateway;
  uint32_t dns;
  uint32_t reserved;
};

#define CLOCK_ADJ_STEP 0
#define CLOCK_ADJ_SLEW 1
#define CLOCK_ADJ_QUERY 2
#define CLOCK_ADJ_SAVE_RTC 3

#define NET_SOCK_TCP 6
#define NET_SOCK_UDP 17
#define NET_SOCK_STATE_UNCONN 0xFF
#define NET_SOCK_FLAG_KERNEL 1U
#define NET_SOCK_FLAG_ORPHAN 2U
#define NET_SOCK_FLAG_REUSE 4U
#define NET_SOCK_FLAG_BROADCAST 8U
#define NET_SOCKETS_MAX 512

struct net_socket_entry {
  uint8_t protocol;
  uint8_t state;
  uint16_t flags;
  uint32_t local_address;
  uint32_t remote_address;
  uint16_t local_port;
  uint16_t remote_port;
  uint32_t recv_queue;
  uint32_t send_queue;
  uint32_t pid;
  uint32_t uid;
  uint32_t fd;
  char process[32];
  char device[NET_IF_NAME_CAPACITY];
};

struct net_if_info {
  char name[NET_IF_NAME_CAPACITY];
  uint8_t mac[6];
  uint16_t mtu;
  uint32_t link_up;
  uint32_t has_ip;
  uint32_t ip_address;
  uint32_t netmask;
  uint32_t gateway;
  uint32_t dns_server;
  uint32_t dhcp_bound;
  uint64_t rx_packets;
  uint64_t tx_packets;
  uint64_t rx_bytes;
  uint64_t tx_bytes;
  uint64_t rx_dropped;
  uint64_t tx_dropped;
};

#define GUI_WINDOW_STATE_FOCUSED 1
#define GUI_WINDOW_STATE_REPAINT 2
#define GUI_WINDOW_STATE_CLOSE 4
#define GUI_WINDOW_STATE_SERVER 8
#define GUI_WINDOW_STATE_DRAGGING 16
#define GUI_WINDOW_STATE_GEN_SHIFT 8

#define GUI_WINDOW_INFO_MINIMIZED 1
#define GUI_WINDOW_INFO_MAXIMIZED 2

#define GUI_WINDOW_FLAG_FIXED 1
#define GUI_WINDOW_FLAG_NOCLOSE 2
#define GUI_WINDOW_FLAG_POPUP 4
#define GUI_WINDOW_FLAG_NORESIZE 8

#define GUI_RESIZE_GRIP 6
#define GUI_RESIZE_TOP_GRIP 4
#define GUI_RESIZE_CORNER 18
#define GUI_EDGE_LEFT 1U
#define GUI_EDGE_RIGHT 2U
#define GUI_EDGE_TOP 4U
#define GUI_EDGE_BOTTOM 8U

static inline uint32_t gui_resize_edges(int32_t fx, int32_t fy, int32_t fw,
                                        int32_t fh, int32_t x, int32_t y) {
  int32_t rx = x - fx, ry = y - fy;
  if (rx < 0 || ry < 0 || rx >= fw || ry >= fh)
    return 0;
  uint32_t edges = 0;
  if (rx < GUI_RESIZE_GRIP)
    edges |= GUI_EDGE_LEFT;
  else if (rx >= fw - GUI_RESIZE_GRIP)
    edges |= GUI_EDGE_RIGHT;
  if (ry < GUI_RESIZE_TOP_GRIP)
    edges |= GUI_EDGE_TOP;
  else if (ry >= fh - GUI_RESIZE_GRIP)
    edges |= GUI_EDGE_BOTTOM;
  if (edges & (GUI_EDGE_TOP | GUI_EDGE_BOTTOM)) {
    if (rx < GUI_RESIZE_CORNER)
      edges |= GUI_EDGE_LEFT;
    else if (rx >= fw - GUI_RESIZE_CORNER)
      edges |= GUI_EDGE_RIGHT;
  }
  if (edges & (GUI_EDGE_LEFT | GUI_EDGE_RIGHT)) {
    if (ry < GUI_RESIZE_CORNER)
      edges |= GUI_EDGE_TOP;
    else if (ry >= fh - GUI_RESIZE_CORNER)
      edges |= GUI_EDGE_BOTTOM;
  }
  return edges;
}

#define GUI_WINDOW_CMD_MINIMIZE 1
#define GUI_WINDOW_CMD_RESTORE 2
#define GUI_WINDOW_CMD_MAXIMIZE 3
#define GUI_WINDOW_CMD_UNMAXIMIZE 4

struct gui_window_request {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

struct gui_task_info {
  uint32_t pid;
  uint32_t flags;
  char name[32];
};

#define GUI_TASK_MINIMIZED 1
#define GUI_TASK_FOCUSED 2

struct gui_window_info {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
  uint32_t flags;
};

#define PROCESS_ENVIRONMENT_LIMIT 16
#define PROCESS_ENVIRONMENT_NAME_LIMIT 32
#define PROCESS_ENVIRONMENT_VALUE_LIMIT 128

struct process_environment_variable {
  uint8_t used;
  char name[PROCESS_ENVIRONMENT_NAME_LIMIT];
  char value[PROCESS_ENVIRONMENT_VALUE_LIMIT];
};


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

#define CPU_CORE_MAX_COUNT 16

struct cpu_core_entry {
  uint32_t id;
  uint32_t online;
  uint64_t total_ticks;
  uint64_t idle_ticks;
};

struct cpu_core_info {
  uint32_t count;
  uint32_t reserved;
  struct cpu_core_entry cores[CPU_CORE_MAX_COUNT];
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

struct display_scale_info {
  uint32_t physical_width;
  uint32_t physical_height;
  uint32_t scale_percent;
  uint32_t reserved;
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

struct thermal_info {
  uint32_t available;
  uint32_t thermal_zone_count;
  uint32_t fan_count;
  int32_t temperature_deci_c;
  uint32_t fan_rpm;
  char thermal_zone[32];
  char fan[32];
};

#define BATTERY_PERCENT_UNKNOWN 0xFFFFFFFFu
#define BATTERY_VALUE_UNKNOWN_U32 0u

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

struct custom_format_request {
  char device[32];
  uint32_t partition_count;
  uint64_t sizes_gb[4];
  uint32_t reserved;
};

struct file_stat_info {
  uint64_t size;
  uint32_t is_directory;
  uint32_t reserved;
};

#define FB_BLIT_KEEP_ALPHA 1u

struct fb_blit_request {
  uint64_t pixels;
  uint32_t stride;
  uint32_t width;
  uint32_t height;
  int32_t x;
  int32_t y;
  uint32_t flags;
};

struct time_info {
  int64_t unix_seconds;
  uint32_t millis;
  int32_t tz_minutes;
  uint64_t uptime_ms;
  uint32_t source;
  uint32_t reserved;
};

struct file_stat_ex {
  uint64_t size;
  uint32_t mode;
  uint32_t uid;
  uint32_t gid;
  uint32_t is_directory;
  uint32_t atime;
  uint32_t mtime;
  uint32_t ctime;
  uint32_t flags;
};

#define FILE_STAT_FLAG_SYMLINK 1U

struct file_perm_info {
  uint32_t mode;
  uint32_t uid;
  uint32_t gid;
  uint32_t reserved;
};

#define DISK_IO_MAX_SECTORS 128

struct disk_io_request {
  char device[16];
  uint64_t lba;
  uint64_t buffer;
  uint32_t count;
  uint32_t write;
};

struct user_info {
  char name[32];
  uint32_t uid;
  uint32_t gid;
  char home[64];
  char shell[64];
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

struct net_tcp_info {
  uint32_t state;
  uint32_t local_address;
  uint32_t remote_address;
  uint16_t local_port;
  uint16_t remote_port;
  uint32_t bytes_unread;
  uint32_t bytes_unsent;
  uint32_t cwnd;
  uint32_t rto_ms;
};

struct syscall_regs {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx,
      rax;
  uint64_t vector, err;
  uint64_t rip, cs, rflags, rsp, ss;
};

void syscall_init(void);
int64_t syscall_handler(struct syscall_regs *regs);
int64_t do_syscall_64(struct syscall_regs *regs);
long x64_sys_call(const struct syscall_regs *regs, uint64_t nr);
struct syscall_regs *syscall_current_regs(void);
int64_t syscall_kernel_invoke(uint64_t n, uint64_t a1, uint64_t a2,
                              uint64_t a3, uint64_t a4, uint64_t a5);
int syscall_entry_c(struct syscall_regs *regs);
void install_report_ui_crash(int32_t status);
void filesystem_syscall_lock(void);
bool filesystem_syscall_acquire(void);
bool install_job_running(void);
void filesystem_syscall_unlock(void);