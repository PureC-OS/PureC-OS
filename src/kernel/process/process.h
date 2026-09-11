#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PROCESS_MAX_COUNT 16
#define PROCESS_FD_COUNT 32
#define PROCESS_COMMAND_LINE_CAPACITY 256
#define PROCESS_ENVIRONMENT_COUNT 16
#define PROCESS_ENVIRONMENT_NAME_CAPACITY 32
#define PROCESS_ENVIRONMENT_VALUE_CAPACITY 128
#define PROCESS_CAP_STORAGE_ADMIN (1U<<0)

struct process_environment_entry {
    bool used;
    char name[PROCESS_ENVIRONMENT_NAME_CAPACITY];
    char value[PROCESS_ENVIRONMENT_VALUE_CAPACITY];
};

enum process_state {
    PROCESS_FREE=0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_EXITED
};

struct process {
    uint32_t pid;
    uint32_t parent_pid;
    enum process_state state;
    int32_t exit_code;
    uint64_t address_space;
    uint64_t entry;
    uint64_t user_stack_top;
    uint64_t heap_base;
    uint64_t heap_break;
    uint64_t heap_mapped_end;
    uint64_t heap_limit;
    int32_t thread_id;
    int32_t waiter_thread_id;
    uint64_t runtime_ticks;
    uint64_t sampled_runtime_ticks;
    uint32_t capabilities;
    int32_t descriptors[PROCESS_FD_COUNT];
    char command_line[PROCESS_COMMAND_LINE_CAPACITY];
    struct process_environment_entry environment[PROCESS_ENVIRONMENT_COUNT];
    char name[32];
};

void process_init(void);
int32_t process_spawn_elf(const void *image, uint64_t image_size,
                          const char *name, const char *command_line);
int32_t process_spawn_module(const char *path, const char *command_line);
int32_t process_wait(uint32_t pid, int32_t *status, bool nohang);
struct process *process_current(void);
int32_t process_current_pid(void);
bool process_current_is_user(void);
bool process_has_capability(uint32_t capability);
uint64_t process_current_address_space(void);
uint64_t process_heap_grow(uint64_t size);
void process_exit_current(int32_t status) __attribute__((noreturn));
int32_t process_fd_install(int32_t kernel_descriptor);
int32_t process_fd_resolve(int32_t descriptor);
int32_t process_fd_close(int32_t descriptor);
bool process_user_buffer(const void *buffer, uint64_t size, bool writable);
bool process_user_string(const char *text, uint64_t capacity);
int32_t process_command_line(char *buffer, uint32_t capacity);
int32_t process_name(char *buffer, uint32_t capacity);
int32_t process_environment_get(const char *name, char *buffer,
                                uint32_t capacity);
int32_t process_environment_set(const char *name, const char *value);
int32_t process_environment_unset(const char *name);
int32_t process_environment_list(struct process_environment_entry *entries,
                                 uint32_t capacity);
struct process_monitor_info;
int32_t process_monitor_list(struct process_monitor_info *entries,
                             uint32_t capacity);
