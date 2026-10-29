#pragma once

#include "types.h"

#define WNOHANG 1
#define WIFEXITED(status) (((status) & 0x7F) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF)
#define WIFSIGNALED(status) (((status) & 0x7F) != 0)
#define WTERMSIG(status) ((status) & 0x7F)

pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait(int *status);
