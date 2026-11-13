#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SYS_WRITE 1
#define SYS_CLEAR 2
#define SYS_SLEEP 3
#define SYS_GETPID 39
#define SYS_FORK 57
#define SYS_EXEC 59
#define SYS_EXIT 60
#define SYS_WAIT 61
#define SYS_DRAW_RECT 100
#define SYS_DRAW_LINE 101
#define SYS_GET_MOUSE 102
#define SYS_FB_INFO 103

#define SYS_SCROLL_RECT_UP 106
#define SYS_FB_BEGIN_UPDATE 109
#define SYS_FB_END_UPDATE 110
#define SYS_DISPLAY_SET_MODE 281
#define SYS_THERMAL_INFO 282
#define SYS_CPU_CORE_INFO 283
#define SYS_FILE_OPEN 200
#define SYS_FILE_READ 201
#define SYS_FILE_DELETE 202
#define SYS_FILE_RENAME 203
#define SYS_FILE_MOVE 204
#define SYS_DIR_LIST 205
#define SYS_FILE_CREATE 206
#define SYS_DIR_CREATE 207
#define SYS_DISK_LIST 208
#define SYS_STORAGE_CONTROLLERS 209
#define SYS_FS_FORMAT 210
#define SYS_FILE_WRITE 211
#define SYS_USB_RESCAN 212
#define SYS_FS_FORMAT_FORCE 213
#define SYS_INSTALL_DEVICE 214
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
#define SYS_NET_IF_LIST 290
#define SYS_WIFI_SCAN 260
#define SYS_WIFI_LIST 261
#define SYS_WIFI_CONNECT 262
#define SYS_WIFI_DISCONNECT 263
#define SYS_WIFI_STATUS 264
#define SYS_SAVE_KLOG 265
#define SYS_GET_ROOT_DEVICE 266
#define SYS_FS_FORMAT_CUSTOM 267
#define SYS_FORMAT_DEVICE_EX 268
#define SYS_INSTALL_START_EX 269
#define SYS_FILE_SEEK 275
#define SYS_FILE_STAT 276
#define SYS_AUDIO_STOP_TONE 277
#define SYS_AUDIO_PCM_PUSH 278
#define SYS_AUDIO_PCM_START 279
#define SYS_AUDIO_PCM_STOP 280
#define SYS_DIR_LIST_LONG 288
#define SYS_IPC_SEND 291
#define SYS_IPC_RECV 292
/* TCP sockets. a1 packs handle (low 32 bits) | timeout_ms << 32 where the
 * call can block; timeout 0 = never block, 0xFFFFFFFF = wait forever. */
#define SYS_TCP_SOCKET 300  /* () -> handle */
#define SYS_TCP_CONNECT 301 /* (handle|timeout<<32, ipv4, port) */
#define SYS_TCP_LISTEN 302  /* (handle, port, backlog) */
#define SYS_TCP_ACCEPT 303  /* (handle|timeout<<32) -> new handle */
#define SYS_TCP_SEND 304    /* (handle|timeout<<32, buf, len) -> bytes */
#define SYS_TCP_RECV 305    /* (handle|timeout<<32, buf, len) -> bytes, 0=EOF */
#define SYS_TCP_CLOSE 306   /* (handle) */
#define SYS_TCP_ABORT 307   /* (handle) send RST and release */
#define SYS_TCP_INFO 308    /* (handle, struct net_tcp_info *) */
#define SYS_GETUID 309
#define SYS_GETGID 310
#define SYS_SETUID 311
#define SYS_SETGID 312
#define SYS_CHMOD 313       /* (path, mode) */
#define SYS_CHOWN 314       /* (path, uid, gid) */
#define SYS_FILE_PERM 315   /* (path, struct file_perm_info *) */
#define SYS_USER_LOOKUP 316 /* (name|NULL, uid, struct user_info *) */
#define SYS_GETRANDOM 317   /* (buf, len) -> len */
#define SYS_SESSION_BEGIN 318 /* (uid, gid), root only */
#define SYS_TIME_GET 319      /* (struct time_info *) */
#define SYS_TIME_SET 320      /* (unix_seconds, tz_minutes), root only */
#define SYS_FILE_STAT_EX 321  /* (path, struct file_stat_ex *) */
#define SYS_FB_BLIT 322       /* (struct fb_blit_request *) */
#define SYS_KEY_EVENT_GET 323 /* (struct key_event *) -> 1 event, 0 empty */
#define SYS_EXECVE 324
#define SYS_WAITPID 325
#define SYS_PIPE 326
#define SYS_DUP 327
#define SYS_DUP2 328
#define SYS_FD_READ 329
#define SYS_FD_WRITE 330
#define SYS_FD_OPEN 331
#define SYS_FD_CTL 332
#define SYS_CHDIR 333
#define SYS_GETCWD 334
#define SYS_KILL 335
#define SYS_SIGACTION 336
#define SYS_SIGPROCMASK 337
#define SYS_SIGRETURN 338
#define SYS_GETPPID 339
#define SYS_FD_STAT 340
#define SYS_SIGPENDING 341
#define SYS_PAUSE 342
#define SYS_FD_PATH 343
#define SYS_FD_SEEK 344
#define SYS_FD_TRUNCATE 345
#define SYS_DESKTOP_CLAIM 346 /* () -> 0, root only, desktop=rs session */
#define SYS_DESKTOP_BLIT 347  /* (struct fb_blit_request *) into desktop layer */
#define SYS_DESKTOP_WAIT 348  /* (timeout_ms) -> RS_WAIT_* bits */
#define SYS_DESKTOP_WM 349    /* (op, arg1, arg2) */
#define SYS_GUI_WINDOW_INFO 350  /* (struct gui_window_info *) */
#define SYS_GUI_WINDOW_FLAGS 351 /* (GUI_WINDOW_FLAG_*) */
#define SYS_GUI_WINDOW_CMD 352   /* (GUI_WINDOW_CMD_*) */
#define SYS_GUI_WINDOW_TITLE 353 /* (const char *title) */
#define SYS_TTY_READ 354         /* (buffer, count) -> bytes, console input */
#define SYS_TTY_IOCTL 355        /* (TTY_* request, argument pointer) */
#define SYS_POLL 356             /* (struct kernel_pollfd *, count, timeout_ms) */
#define SYS_MMAP 357             /* (struct kernel_mmap_args *) -> address */
#define SYS_MUNMAP 358           /* (address, length) */
#define SYS_MPROTECT 359         /* (address, length, KMAP_PROT_*) */
#define SYS_MREMAP 360           /* (old, old_len, new_len, KMAP_REMAP_*) -> address */
#define SYS_DISPLAY_SCALE_INFO 361 /* (struct display_scale_info *) */
#define SYS_SOCKET 362       /* (domain, type|flags, protocol) -> fd */
#define SYS_CONNECT 363      /* (fd, sockaddr *, len) */
#define SYS_BIND 364         /* (fd, sockaddr *, len) */
#define SYS_LISTEN 365       /* (fd, backlog) */
#define SYS_ACCEPT 366       /* (fd, sockaddr *, socklen_t *, flags) -> fd */
#define SYS_SOCK_SHUTDOWN 367    /* (fd, how) */
#define SYS_SOCKNAME 368     /* (fd, sockaddr *, socklen_t *, peer) */
#define SYS_SETSOCKOPT 369   /* (fd, level<<32|name, value *, len) */
#define SYS_GETSOCKOPT 370   /* (fd, level<<32|name, value *, len *) */
#define SYS_DNS_RESOLVE 371  /* (name, timeout_ms, uint32_t *ipv4_be) */
#define SYS_SOCK_SEND 372    /* (fd, buf, len, msg_flags) */
#define SYS_SOCK_RECV 373    /* (fd, buf, len, msg_flags) */
#define SYS_UTIMES 374       /* (path, atime, mtime), 0xFFFFFFFF = now */
#define SYS_THREAD_CREATE 375 /* (struct kernel_thread_args *) -> tid */
#define SYS_THREAD_EXIT 376   /* (unmap_address, unmap_length), no return */
#define SYS_FUTEX 377         /* (FUTEX_*, uaddr, value, timeout_ms in rsi) */
#define SYS_SET_FS_BASE 378   /* (base) -> 0 */
#define SYS_GETTID 379        /* () -> tid */
#define SYS_EVENTFD 380          /* (initial, EFD_* flags) -> fd */
#define SYS_TIMERFD_CREATE 381   /* (clock_id, TFD_* flags) -> fd */
#define SYS_TIMERFD_SETTIME 382  /* (fd, flags, new itimerspec *, old itimerspec * in rsi) */
#define SYS_TIMERFD_GETTIME 383  /* (fd, itimerspec *) */
#define SYS_EPOLL_CREATE 384     /* (EPOLL_CLOEXEC flag) -> fd */
#define SYS_EPOLL_CTL 385        /* (epfd, op, fd, struct epoll_event * in rsi) */
#define SYS_EPOLL_WAIT 386       /* (epfd, events *, max, timeout_ms in rsi) */
#define SYS_SOCKETPAIR 387       /* (domain, type|flags, protocol, int fds[2] in rsi) */
#define SYS_SENDMSG 388          /* (fd, struct kernel_msghdr *, msg_flags) -> bytes */
#define SYS_RECVMSG 389          /* (fd, struct kernel_msghdr *, msg_flags) -> bytes */
#define SYS_MEMFD_CREATE 390     /* (name, flags) -> fd */
#define SYS_SHM_OPEN 391         /* (name, O_* flags) -> fd */
#define SYS_SHM_UNLINK 392       /* (name) */
#define SYS_MADVISE 393          /* (address, length, advice) */
#define SYS_DISPLAY_CLAIM 398     /* () -> 0; caller (PROCESS_CAP_DISPLAY or root) owns the screen */
#define SYS_FB_MAP 399            /* (struct fb_map_info *) -> 0; maps the front framebuffer */
#define SYS_INPUT_OPEN 400        /* () -> fd delivering struct input_record */
#define SYS_FB_DAMAGE 401         /* (x, y, w, h) -> 0; flush a front-buffer rect to the device */
#define SYS_DISPLAY_RELEASE 402   /* () -> 0; give the screen back to the kernel */
#define SYS_SENDTO 403            /* (fd, buf, len, flags, const sockaddr_in *dst|0) -> bytes */
#define SYS_RECVFROM 404          /* (fd, buf, len, flags, sockaddr_in *src|0) -> bytes */
#define SYS_NET_CONFIGURE 405     /* (const struct net_if_config *) -> 0; root or NET_ADMIN */
#define SYS_KLOG_WRITE 406        /* (level 0..3, const char *message) -> 0; appends to the kernel journal */
#define SYS_SUDO 397              /* (const char *password) -> 0, caller becomes root if in wheel */
#define SYS_OPENPTY 396           /* (int fds[2], flags) -> 0; fds[0]=master, fds[1]=slave */
#define SYS_TIME_ZONE 395        /* (op, name buffer, capacity): 0 get, 1 set (root) */
#define TIME_ZONE_GET 0
#define TIME_ZONE_SET 1
#define SYS_SIGALTSTACK 394      /* (const struct kernel_stack *, struct kernel_stack *old) */

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

#define SYS_OPEN SYS_FILE_OPEN
#define SYS_READ SYS_FILE_READ
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
  uint32_t reserved;
};

struct file_perm_info {
  uint32_t mode;
  uint32_t uid;
  uint32_t gid;
  uint32_t reserved;
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
void install_report_ui_crash(int32_t status);
void filesystem_syscall_lock(void);
bool filesystem_syscall_acquire(void);
bool install_job_running(void);
void filesystem_syscall_unlock(void);
