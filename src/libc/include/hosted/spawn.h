#pragma once

#include "sys/types.h"
#include "signal.h"

#define POSIX_SPAWN_RESETIDS 1
#define POSIX_SPAWN_SETPGROUP 2
#define POSIX_SPAWN_SETSIGDEF 4
#define POSIX_SPAWN_SETSIGMASK 8
#define POSIX_SPAWN_SETSCHEDPARAM 16
#define POSIX_SPAWN_SETSCHEDULER 32
#define POSIX_SPAWN_USEVFORK 64

struct posix_spawn_action;

typedef struct {
    int count;
    int capacity;
    struct posix_spawn_action *actions;
} posix_spawn_file_actions_t;

typedef struct {
    int flags;
    sigset_t sigmask;
    sigset_t sigdefault;
    pid_t pgroup;
} posix_spawnattr_t;

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *actions);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *actions);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *actions, int fd,
                                     const char *path, int flags, unsigned int mode);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *actions, int fd);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *actions, int fd,
                                     int new_fd);
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *actions,
                                         const char *path);

int posix_spawnattr_init(posix_spawnattr_t *attributes);
int posix_spawnattr_destroy(posix_spawnattr_t *attributes);
int posix_spawnattr_setflags(posix_spawnattr_t *attributes, short flags);
int posix_spawnattr_getflags(const posix_spawnattr_t *attributes, short *flags);
int posix_spawnattr_setsigmask(posix_spawnattr_t *attributes, const sigset_t *mask);
int posix_spawnattr_getsigmask(const posix_spawnattr_t *attributes, sigset_t *mask);
int posix_spawnattr_setsigdefault(posix_spawnattr_t *attributes, const sigset_t *set);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attributes, sigset_t *set);
int posix_spawnattr_setpgroup(posix_spawnattr_t *attributes, pid_t group);
int posix_spawnattr_getpgroup(const posix_spawnattr_t *attributes, pid_t *group);

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *actions,
                const posix_spawnattr_t *attributes, char *const argv[],
                char *const envp[]);
int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *actions,
                 const posix_spawnattr_t *attributes, char *const argv[],
                 char *const envp[]);
