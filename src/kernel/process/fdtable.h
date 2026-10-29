#pragma once

#include <stdbool.h>
#include <stdint.h>

struct process;
struct file_stat_info;

int32_t fd_open(const char *path, uint32_t flags);
int32_t fd_read(int32_t fd, void *buffer, uint32_t count);
int32_t fd_write(int32_t fd, const void *buffer, uint32_t count);
int32_t fd_close(int32_t fd);
int32_t fd_dup(int32_t fd, int32_t minimum, bool cloexec);
int32_t fd_dup2(int32_t old_fd, int32_t new_fd);
int32_t fd_pipe(int32_t fds[2], uint32_t flags);
int32_t fd_ctl(int32_t fd, uint32_t command, uint64_t argument);
int32_t fd_path(int32_t fd, char *out, uint32_t capacity);
int32_t fd_stat(int32_t fd, struct file_stat_info *out);
void fd_close_all(struct process *process);
void fd_close_on_exec(struct process *process);
void fd_clone(struct process *child, const struct process *parent);
