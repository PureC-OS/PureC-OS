#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../../fs/types/fs_types.h"
#include "../../drivers/mouse/ps2_mouse.h"
#include "../../drivers/input/keyboard.h"
#include "../../drivers/storage/storage_types.h"
#include "../../kernel/syscall/syscall.h"

struct pc_display_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t size_bytes;
    uint8_t bpp;
    bool available;
};

int64_t pc_syscall(uint64_t number, uint64_t argument1,
                   uint64_t argument2, uint64_t argument3);
uint32_t pc_strlen(const char *text);
int pc_strcmp(const char *left, const char *right);
void pc_copy(char *destination, const char *source, uint32_t capacity);
void pc_write(const char *text);
void pc_write_u64(uint64_t value);
void pc_write_i64(int64_t value);
void pc_read_line(const char *prompt, char *buffer, uint32_t capacity);
void pc_sleep(uint32_t milliseconds);
int32_t pc_getpid(void);
int32_t pc_exec(const char *path);
int32_t pc_exec_with_args(const char *path, const char *arguments);
int32_t pc_wait(int32_t pid, int32_t *status, bool nohang);
int32_t pc_try_getchar(void);
int32_t pc_try_get_special(void);
int32_t pc_get_command_line(char *buffer, uint32_t capacity);
int32_t pc_get_process_name(char *buffer, uint32_t capacity);
int32_t pc_getenv(const char *name, char *buffer, uint32_t capacity);
int32_t pc_setenv(const char *name, const char *value);
int32_t pc_unsetenv(const char *name);
int32_t pc_listenv(struct process_environment_variable *variables,
                   uint32_t capacity);
int32_t pc_process_list(struct process_monitor_info *processes,
                        uint32_t capacity);
bool pc_cpu_info(struct cpu_monitor_info *info);
bool pc_memory_info(struct memory_monitor_info *info);
int32_t pc_ping(const char *target, uint16_t sequence, uint32_t timeout_ms,
                struct network_ping_result *result);
void *pc_heap_grow(uint64_t size);
bool pc_file_exists(const char *path);
int32_t pc_file_open(const char *path);
int32_t pc_file_read(int32_t descriptor, void *buffer, uint32_t capacity);
int64_t pc_file_seek(int32_t descriptor, int64_t offset, uint32_t whence);
int32_t pc_file_stat(const char *path, struct file_stat_info *out);
int32_t pc_file_close(int32_t descriptor);
int32_t pc_file_write(const char *path, const void *buffer, uint32_t size);
int32_t pc_directory_list(const char *path,
                          struct fs_directory_entry *entries,
                          uint32_t capacity);
int32_t pc_directory_list_long(const char *path,
                               struct fs_directory_entry_long *entries,
                               uint32_t capacity);
int32_t pc_file_create(const char *path);
int32_t pc_directory_create(const char *path);
int32_t pc_file_delete(const char *path);
int32_t pc_file_rename(const char *path, const char *new_name);
int32_t pc_file_move(const char *path, const char *destination_directory);
bool pc_display_get_info(struct pc_display_info *info);
void pc_display_begin_update(void);
void pc_display_end_update(void);
void pc_display_clear(uint32_t color);
void pc_desktop_redraw(void);
bool pc_gui_window_register(const struct gui_window_request *request);
bool pc_gui_window_update(const struct gui_window_request *request);
void pc_gui_window_unregister(void);
uint32_t pc_gui_window_state(void);
void pc_gui_window_repaint_done(void);
bool pc_console_configure(uint32_t x, uint32_t y,
                          uint32_t width, uint32_t height,
                          uint32_t foreground, uint32_t background);
void pc_console_clear(void);
void pc_console_disable(void);
void pc_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color);
void pc_draw_text(uint32_t x, uint32_t y, const char *text,
                  uint32_t foreground, uint32_t background);
void pc_draw_text_sized(uint32_t x, uint32_t y, const char *text,
                        uint32_t foreground, uint32_t background,
                        uint32_t size);
bool pc_mouse_get(struct mouse_state *state);
int32_t pc_list_disks(struct storage_device_info *devices, uint32_t capacity);
void pc_reboot(void);
void pc_shutdown(void);int32_t pc_install_start(const char *device, const char *serial);
bool pc_install_status(struct install_status *status);
bool pc_install_log(struct install_log *log);
bool pc_audio_get_status(struct audio_status *status);
int32_t pc_audio_get_volume(void);
bool pc_audio_is_muted(void);
void pc_audio_set_volume(uint32_t volume);
void pc_audio_set_muted(bool muted);
void pc_audio_adjust_volume(int32_t delta);
bool pc_audio_select_output(uint32_t index);
void pc_audio_play_test(void);
int32_t pc_audio_play_tone(uint32_t frequency_hz, uint32_t duration_ms);
void pc_audio_stop_tone(void);
int32_t pc_audio_pcm_push(const int16_t *samples, uint32_t frames);
void pc_audio_pcm_eos(void);
int32_t pc_audio_pcm_start(void);
void pc_audio_pcm_stop(void);
int32_t pc_wifi_scan(void);
int32_t pc_wifi_list(struct wifi_network_info *networks, uint32_t capacity);
int32_t pc_wifi_connect(const char *ssid, const char *password);
int32_t pc_wifi_disconnect(void);
bool pc_wifi_status(struct wifi_status_info *status);
int32_t pc_save_klog(const char *device, const char *path);
int32_t pc_get_root_device(char *buffer, uint32_t capacity);
int32_t pc_format_custom(const char *device, uint32_t partition_count, const uint64_t *sizes_gb);
int32_t pc_format_device_ex(const char *device, const char *serial, uint8_t fs_type);
int32_t pc_install_start_ex(const char *device, const char *serial, uint8_t fs_type);
int32_t pc_get_fs_type(char *buffer, uint32_t capacity);
int32_t pc_ext2_stat(const char *path, struct ext2_stat_info *out);
int32_t pc_ext2_inode(uint32_t ino, struct ext2_stat_info *out);
int32_t pc_ext2_super(struct ext2_super_info *out);
int32_t pc_ext2_blocks(const char *path, struct ext2_blocks_info *out);
void pc_exit(int32_t status) __attribute__((noreturn));