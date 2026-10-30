#include "include/hosted/errno.h"
#include "include/hosted/fcntl.h"
#include "include/hosted/pwd.h"
#include "include/hosted/stdio.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/sys/resource.h"
#include "include/hosted/sys/select.h"
#include "include/hosted/sys/stat.h"
#include "include/hosted/sys/utsname.h"
#include "include/hosted/unistd.h"
#include "include/purec.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static mode_t current_umask = 022;

mode_t umask(mode_t mask)
{
    mode_t previous = current_umask;
    current_umask = mask & 0777;
    return previous;
}

uid_t getuid(void) { return (uid_t)pc_getuid(); }
uid_t geteuid(void) { return (uid_t)pc_getuid(); }
gid_t getgid(void) { return (gid_t)pc_syscall(SYS_GETGID, 0, 0, 0); }
gid_t getegid(void) { return getgid(); }

pid_t vfork(void) { return fork(); }

int setpgid(pid_t pid, pid_t group)
{
    (void)pid;
    (void)group;
    return 0;
}

pid_t getpgrp(void) { return getpid(); }

pid_t tcgetpgrp(int fd)
{
    (void)fd;
    errno = ENOTTY;
    return -1;
}

int tcsetpgrp(int fd, pid_t group)
{
    (void)fd;
    (void)group;
    errno = ENOTTY;
    return -1;
}

int getrlimit(int resource, struct rlimit *limit)
{
    if (!limit)
    {
        errno = EFAULT;
        return -1;
    }
    limit->rlim_cur = resource == RLIMIT_NOFILE ? 64 : RLIM_INFINITY;
    limit->rlim_max = limit->rlim_cur;
    return 0;
}

int setrlimit(int resource, const struct rlimit *limit)
{
    (void)resource;
    (void)limit;
    return 0;
}

int uname(struct utsname *name)
{
    if (!name)
    {
        errno = EFAULT;
        return -1;
    }
    memset(name, 0, sizeof(*name));
    strcpy(name->sysname, "PureC");
    strcpy(name->nodename, "purec");
    strcpy(name->release, "1.0");
    strcpy(name->version, "purec-os");
    strcpy(name->machine, "x86_64");
    char host[64];
    if (pc_getenv("HOSTNAME", host, sizeof(host)) > 0)
    {
        strncpy(name->nodename, host, sizeof(name->nodename) - 1);
    }
    return 0;
}

int select(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set,
           struct timeval *timeout)
{
    (void)count;
    (void)read_set;
    (void)write_set;
    (void)error_set;
    (void)timeout;
    errno = ENOSYS;
    return -1;
}

int pselect(int count, fd_set *read_set, fd_set *write_set, fd_set *error_set,
            const struct timespec *timeout, const void *mask)
{
    (void)count;
    (void)read_set;
    (void)write_set;
    (void)error_set;
    (void)timeout;
    (void)mask;
    errno = ENOSYS;
    return -1;
}

static char passwd_name[64];
static char passwd_password[64];
static char passwd_gecos[128];
static char passwd_home[128];
static char passwd_shell[128];
static struct passwd passwd_entry;

static bool passwd_fill(char *line, const char *want_name, long want_uid)
{
    char *fields[7] = {0};
    int count = 0;
    char *cursor = line;
    fields[count++] = cursor;
    while (*cursor && count < 7)
    {
        if (*cursor == ':')
        {
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
        cursor++;
    }
    if (count < 7)
        return false;
    char *newline = strchr(fields[6], '\n');
    if (newline)
        *newline = '\0';
    long uid = strtol(fields[2], 0, 10);
    if (want_name ? strcmp(fields[0], want_name) != 0 : uid != want_uid)
        return false;
    strncpy(passwd_name, fields[0], sizeof(passwd_name) - 1);
    strncpy(passwd_password, fields[1], sizeof(passwd_password) - 1);
    strncpy(passwd_gecos, fields[4], sizeof(passwd_gecos) - 1);
    strncpy(passwd_home, fields[5], sizeof(passwd_home) - 1);
    strncpy(passwd_shell, fields[6], sizeof(passwd_shell) - 1);
    passwd_entry.pw_name = passwd_name;
    passwd_entry.pw_passwd = passwd_password;
    passwd_entry.pw_uid = (uid_t)uid;
    passwd_entry.pw_gid = (gid_t)strtol(fields[3], 0, 10);
    passwd_entry.pw_gecos = passwd_gecos;
    passwd_entry.pw_dir = passwd_home;
    passwd_entry.pw_shell = passwd_shell;
    return true;
}

static struct passwd *passwd_lookup(const char *name, long uid)
{
    FILE *file = fopen("/etc/passwd", "r");
    if (!file)
        return 0;
    char line[512];
    struct passwd *result = 0;
    while (fgets(line, sizeof(line), file))
    {
        if (passwd_fill(line, name, uid))
        {
            result = &passwd_entry;
            break;
        }
    }
    fclose(file);
    return result;
}

struct passwd *getpwnam(const char *name)
{
    if (!name)
        return 0;
    return passwd_lookup(name, 0);
}

struct passwd *getpwuid(uid_t uid) { return passwd_lookup(0, (long)uid); }

static void template_fill(char *tail, size_t count)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    uint8_t random[16];
    pc_syscall(SYS_GETRANDOM, (uint64_t)(uintptr_t)random, count, 0);
    for (size_t i = 0; i < count; i++)
        tail[i] = alphabet[random[i] % (sizeof(alphabet) - 1)];
}

static char *template_tail(char *path, size_t *count)
{
    size_t length = strlen(path);
    if (length < 6 || strcmp(path + length - 6, "XXXXXX") != 0)
        return 0;
    *count = 6;
    return path + length - 6;
}

int mkstemp(char *path)
{
    size_t count;
    char *tail = path ? template_tail(path, &count) : 0;
    if (!tail)
    {
        errno = EINVAL;
        return -1;
    }
    for (int attempt = 0; attempt < 100; attempt++)
    {
        template_fill(tail, count);
        int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
            return fd;
        if (errno != EEXIST)
            return -1;
    }
    errno = EEXIST;
    return -1;
}

char *mktemp(char *path)
{
    size_t count;
    char *tail = path ? template_tail(path, &count) : 0;
    if (!tail)
    {
        errno = EINVAL;
        if (path)
            path[0] = '\0';
        return path;
    }
    for (int attempt = 0; attempt < 100; attempt++)
    {
        template_fill(tail, count);
        if (access(path, F_OK) != 0)
            return path;
    }
    path[0] = '\0';
    return path;
}

char *mkdtemp(char *path)
{
    size_t count;
    char *tail = path ? template_tail(path, &count) : 0;
    if (!tail)
    {
        errno = EINVAL;
        return 0;
    }
    for (int attempt = 0; attempt < 100; attempt++)
    {
        template_fill(tail, count);
        if (mkdir(path, 0700) == 0)
            return path;
        if (errno != EEXIST)
            return 0;
    }
    errno = EEXIST;
    return 0;
}

void *mempcpy(void *destination, const void *source, size_t count)
{
    memcpy(destination, source, count);
    return (char *)destination + count;
}

char *stpcpy(char *destination, const char *source)
{
    size_t length = strlen(source);
    memcpy(destination, source, length + 1);
    return destination + length;
}

char *strsep(char **cursor, const char *delimiters)
{
    char *start = *cursor;
    if (!start)
        return 0;
    char *end = start + strcspn(start, delimiters);
    if (*end)
    {
        *end = '\0';
        *cursor = end + 1;
    }
    else
    {
        *cursor = 0;
    }
    return start;
}

int vsprintf(char *buffer, const char *format, va_list arguments)
{
    return vsnprintf(buffer, (size_t)0x7FFFFFFF, format, arguments);
}

int vasprintf(char **result, const char *format, va_list arguments)
{
    va_list copy;
    va_copy(copy, arguments);
    int length = vsnprintf(0, 0, format, copy);
    va_end(copy);
    if (length < 0)
        return -1;
    char *text = (char *)malloc((size_t)length + 1);
    if (!text)
    {
        errno = ENOMEM;
        return -1;
    }
    vsnprintf(text, (size_t)length + 1, format, arguments);
    *result = text;
    return length;
}

int asprintf(char **result, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int length = vasprintf(result, format, arguments);
    va_end(arguments);
    return length;
}

size_t confstr(int name, char *buffer, size_t length)
{
    const char *value = "/usr/bin:/bin";
    if (name != _CS_PATH)
    {
        errno = EINVAL;
        return 0;
    }
    size_t needed = strlen(value) + 1;
    if (buffer && length)
    {
        size_t copy = needed < length ? needed : length - 1;
        memcpy(buffer, value, copy);
        buffer[copy] = '\0';
    }
    return needed;
}

char *getlogin(void)
{
    static char login[64];
    const char *user = getenv("USER");
    if (!user)
    {
        struct passwd *entry = getpwuid(getuid());
        user = entry ? entry->pw_name : 0;
    }
    if (!user)
        return 0;
    strncpy(login, user, sizeof(login) - 1);
    return login;
}

long long strtoll(const char *text, char **end, int base) { return (long long)strtol(text, end, base); }
