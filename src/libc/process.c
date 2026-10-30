
#include "include/hosted/unistd.h"
#include "include/hosted/fcntl.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/errno.h"
#include "include/hosted/signal.h"
#include "include/hosted/spawn.h"
#include "include/hosted/stdio.h"
#include "include/hosted/sys/wait.h"
#include "include/purec.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern char **environ;
void stdio_set_child(FILE *stream, int pid);
int stdio_get_child(const FILE *stream);

static int result_errno(int64_t result) {
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return (int)result;
}

pid_t fork(void) {
    fflush(0);
    int64_t result = pc_syscall(SYS_FORK, 0, 0, 0);
    if (result < 0) {
        errno = result == -1 ? EAGAIN : (int)-result;
        return -1;
    }
    return (pid_t)result;
}

pid_t getpid(void) { return pc_getpid(); }

pid_t getppid(void) { return (pid_t)pc_syscall(SYS_GETPPID, 0, 0, 0); }

pid_t waitpid(pid_t pid, int *status, int options) {
    int32_t raw = 0;
    int64_t result = pc_syscall(SYS_WAITPID, (uint64_t)(int64_t)(pid > 0 ? pid : -1),
                                (uint64_t)(uintptr_t)&raw, (uint64_t)(uint32_t)options);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    if (result > 0 && status) *status = raw;
    return (pid_t)result;
}

pid_t wait(int *status) { return waitpid(-1, status, 0); }

int pause(void) {
    pc_syscall(SYS_PAUSE, 0, 0, 0);
    errno = EINTR;
    return -1;
}

unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0;
}

int execve(const char *path, char *const *argv, char *const *envp) {
    if (!path || !path[0]) { errno = ENOENT; return -1; }
    fflush(0);
    return result_errno(pc_syscall(SYS_EXECVE, (uint64_t)(uintptr_t)path,
                                   (uint64_t)(uintptr_t)argv, (uint64_t)(uintptr_t)envp));
}

int execv(const char *path, char *const *argv) {
    return execve(path, argv, environ);
}

static const char *const shell_paths[] = {"/usr/bin/sh", "/bin/sh", "/bin/program/sh"};

static int exec_script(const char *path, char *const *argv, char *const *envp) {
    char *vector[66];
    size_t count = 0;
    vector[count++] = (char *)"sh";
    vector[count++] = (char *)path;
    for (size_t i = 1; argv && argv[i] && count + 1 < sizeof(vector) / sizeof(vector[0]); i++)
        vector[count++] = argv[i];
    vector[count] = 0;
    for (size_t i = 0; i < sizeof(shell_paths) / sizeof(shell_paths[0]); i++) {
        execve(shell_paths[i], vector, envp);
        if (errno != ENOENT) break;
    }
    errno = ENOEXEC;
    return -1;
}

int execvpe(const char *file, char *const *argv, char *const *envp) {
    if (!file || !file[0]) { errno = ENOENT; return -1; }
    if (strchr(file, '/')) {
        execve(file, argv, envp);
        if (errno == ENOEXEC) return exec_script(file, argv, envp);
        return -1;
    }
    const char *path = getenv("PATH");
    if (!path || !path[0]) path = "/usr/bin:/bin:/usr/games:/sbin";
    size_t file_length = strlen(file);
    int saved = ENOENT;
    const char *cursor = path;
    for (;;) {
        const char *end = strchr(cursor, ':');
        size_t directory_length = end ? (size_t)(end - cursor) : strlen(cursor);
        char candidate[512];
        if (directory_length + file_length + 2 <= sizeof(candidate)) {
            if (directory_length == 0) {
                candidate[0] = '.';
                directory_length = 1;
            } else {
                memcpy(candidate, cursor, directory_length);
            }
            candidate[directory_length] = '/';
            memcpy(candidate + directory_length + 1, file, file_length + 1);
            execve(candidate, argv, envp);
            if (errno == ENOEXEC) return exec_script(candidate, argv, envp);
            if (errno != ENOENT && errno != ENOTDIR) saved = errno;
        }
        if (!end) break;
        cursor = end + 1;
    }
    errno = saved;
    return -1;
}

int execvp(const char *file, char *const *argv) {
    return execvpe(file, argv, environ);
}

static int exec_list(const char *name, const char *first, va_list arguments,
                     bool search) {
    char *vector[256];
    size_t count = 0;
    const char *argument = first;
    while (count + 1 < sizeof(vector) / sizeof(vector[0])) {
        vector[count++] = (char *)argument;
        if (!argument) break;
        argument = va_arg(arguments, const char *);
    }
    if (count == sizeof(vector) / sizeof(vector[0]) - 1) vector[count] = 0;
    return search ? execvp(name, vector) : execv(name, vector);
}

int execl(const char *path, const char *argument, ...) {
    va_list arguments;
    va_start(arguments, argument);
    int result = exec_list(path, argument, arguments, false);
    va_end(arguments);
    return result;
}

int execlp(const char *file, const char *argument, ...) {
    va_list arguments;
    va_start(arguments, argument);
    int result = exec_list(file, argument, arguments, true);
    va_end(arguments);
    return result;
}

enum spawn_action_kind {
    SPAWN_ACTION_OPEN,
    SPAWN_ACTION_CLOSE,
    SPAWN_ACTION_DUP2,
    SPAWN_ACTION_CHDIR
};

struct posix_spawn_action {
    enum spawn_action_kind kind;
    int fd;
    int new_fd;
    int flags;
    unsigned int mode;
    char *path;
};

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *actions) {
    actions->count = 0;
    actions->capacity = 0;
    actions->actions = 0;
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *actions) {
    for (int i = 0; i < actions->count; i++) free(actions->actions[i].path);
    free(actions->actions);
    actions->actions = 0;
    actions->count = actions->capacity = 0;
    return 0;
}

static struct posix_spawn_action *action_append(posix_spawn_file_actions_t *actions) {
    if (actions->count == actions->capacity) {
        int grown = actions->capacity ? actions->capacity * 2 : 8;
        struct posix_spawn_action *fresh = (struct posix_spawn_action *)realloc(
            actions->actions, (size_t)grown * sizeof(*fresh));
        if (!fresh) return 0;
        actions->actions = fresh;
        actions->capacity = grown;
    }
    struct posix_spawn_action *action = &actions->actions[actions->count++];
    memset(action, 0, sizeof(*action));
    return action;
}

static char *duplicate_string(const char *text) {
    size_t length = strlen(text) + 1;
    char *copy = (char *)malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *actions, int fd,
                                     const char *path, int flags, unsigned int mode) {
    if (fd < 0 || !path) return EINVAL;
    struct posix_spawn_action *action = action_append(actions);
    if (!action) return ENOMEM;
    action->kind = SPAWN_ACTION_OPEN;
    action->fd = fd;
    action->flags = flags;
    action->mode = mode;
    action->path = duplicate_string(path);
    return action->path ? 0 : ENOMEM;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *actions, int fd) {
    if (fd < 0) return EBADF;
    struct posix_spawn_action *action = action_append(actions);
    if (!action) return ENOMEM;
    action->kind = SPAWN_ACTION_CLOSE;
    action->fd = fd;
    return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *actions, int fd,
                                     int new_fd) {
    if (fd < 0 || new_fd < 0) return EBADF;
    struct posix_spawn_action *action = action_append(actions);
    if (!action) return ENOMEM;
    action->kind = SPAWN_ACTION_DUP2;
    action->fd = fd;
    action->new_fd = new_fd;
    return 0;
}

int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *actions,
                                         const char *path) {
    if (!path) return EINVAL;
    struct posix_spawn_action *action = action_append(actions);
    if (!action) return ENOMEM;
    action->kind = SPAWN_ACTION_CHDIR;
    action->path = duplicate_string(path);
    return action->path ? 0 : ENOMEM;
}

int posix_spawnattr_init(posix_spawnattr_t *attributes) {
    memset(attributes, 0, sizeof(*attributes));
    return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *attributes) {
    (void)attributes;
    return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *attributes, short flags) {
    attributes->flags = flags;
    return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t *attributes, short *flags) {
    *flags = (short)attributes->flags;
    return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t *attributes, const sigset_t *mask) {
    attributes->sigmask = *mask;
    return 0;
}

int posix_spawnattr_getsigmask(const posix_spawnattr_t *attributes, sigset_t *mask) {
    *mask = attributes->sigmask;
    return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t *attributes, const sigset_t *set) {
    attributes->sigdefault = *set;
    return 0;
}

int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attributes, sigset_t *set) {
    *set = attributes->sigdefault;
    return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t *attributes, pid_t group) {
    attributes->pgroup = group;
    return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *attributes, pid_t *group) {
    *group = attributes->pgroup;
    return 0;
}

static int spawn_apply_actions(const posix_spawn_file_actions_t *actions) {
    for (int i = 0; actions && i < actions->count; i++) {
        const struct posix_spawn_action *action = &actions->actions[i];
        switch (action->kind) {
        case SPAWN_ACTION_OPEN: {
            int fd = open(action->path, action->flags, action->mode);
            if (fd < 0) return errno;
            if (fd != action->fd) {
                if (dup2(fd, action->fd) < 0) return errno;
                close(fd);
            }
            break;
        }
        case SPAWN_ACTION_CLOSE:
            if (close(action->fd) < 0 && errno != EBADF) return errno;
            break;
        case SPAWN_ACTION_DUP2:
            if (action->fd == action->new_fd) {
                if (fcntl(action->fd, F_SETFD, 0) < 0) return errno;
            } else if (dup2(action->fd, action->new_fd) < 0) {
                return errno;
            }
            break;
        case SPAWN_ACTION_CHDIR:
            if (chdir(action->path) < 0) return errno;
            break;
        }
    }
    return 0;
}

static int spawn_core(pid_t *pid, const char *file, bool search,
                      const posix_spawn_file_actions_t *actions,
                      const posix_spawnattr_t *attributes, char *const argv[],
                      char *const envp[]) {
    int report[2];
    if (pipe2(report, O_CLOEXEC) < 0) return errno;
    pid_t child = fork();
    if (child < 0) {
        int failure = errno;
        close(report[0]);
        close(report[1]);
        return failure;
    }
    if (child == 0) {
        close(report[0]);
        int failure = 0;
        if (attributes && (attributes->flags & POSIX_SPAWN_SETSIGDEF)) {
            for (int sig = 1; sig < 32; sig++)
                if (attributes->sigdefault & (1UL << (sig - 1))) signal(sig, SIG_DFL);
        }
        if (attributes && (attributes->flags & POSIX_SPAWN_SETSIGMASK))
            sigprocmask(SIG_SETMASK, &attributes->sigmask, 0);
        failure = spawn_apply_actions(actions);
        if (!failure) {
            char *const *environment = envp ? envp : environ;
            if (search) execvpe(file, argv, environment);
            else execve(file, argv, environment);
            failure = errno;
        }
        write(report[1], &failure, sizeof(failure));
        _exit(127);
    }
    close(report[1]);
    int failure = 0;
    ssize_t got;
    do {
        got = read(report[0], &failure, sizeof(failure));
    } while (got < 0 && errno == EINTR);
    close(report[0]);
    if (got == (ssize_t)sizeof(failure)) {
        int status;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        return failure;
    }
    if (pid) *pid = child;
    return 0;
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *actions,
                const posix_spawnattr_t *attributes, char *const argv[],
                char *const envp[]) {
    return spawn_core(pid, path, false, actions, attributes, argv, envp);
}

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *actions,
                 const posix_spawnattr_t *attributes, char *const argv[],
                 char *const envp[]) {
    return spawn_core(pid, file, true, actions, attributes, argv, envp);
}

static void exec_shell_command(const char *command) {
    char *argv[4] = {(char *)"sh", (char *)"-c", (char *)command, 0};
    for (size_t i = 0; i < sizeof(shell_paths) / sizeof(shell_paths[0]); i++) {
        execve(shell_paths[i], argv, environ);
        if (errno != ENOENT) break;
    }
    _exit(127);
}

int system(const char *command) {
    if (!command) {
        struct file_stat_info info;
        for (size_t i = 0; i < sizeof(shell_paths) / sizeof(shell_paths[0]); i++)
            if (pc_file_stat(shell_paths[i], &info) == 0) return 1;
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) exec_shell_command(command);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return status;
}

FILE *popen(const char *command, const char *type) {
    if (!command || !type || (type[0] != 'r' && type[0] != 'w')) {
        errno = EINVAL;
        return 0;
    }
    bool reading = type[0] == 'r';
    int fds[2];
    if (pipe(fds) < 0) return 0;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return 0;
    }
    if (pid == 0) {
        dup2(reading ? fds[1] : fds[0], reading ? 1 : 0);
        close(fds[0]);
        close(fds[1]);
        exec_shell_command(command);
    }
    int keep = reading ? fds[0] : fds[1];
    close(reading ? fds[1] : fds[0]);
    FILE *stream = fdopen(keep, reading ? "r" : "w");
    if (!stream) {
        close(keep);
        int status;
        waitpid(pid, &status, 0);
        return 0;
    }
    stdio_set_child(stream, pid);
    return stream;
}

int pclose(FILE *stream) {
    if (!stream) { errno = EINVAL; return -1; }
    pid_t pid = (pid_t)stdio_get_child(stream);
    fclose(stream);
    if (pid <= 0) { errno = ECHILD; return -1; }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return status;
}
