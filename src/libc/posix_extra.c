#include "../kernel/syscall/syscall.h"
#include "include/hosted/errno.h"
#include "include/hosted/fcntl.h"
#include "include/hosted/grp.h"
#include "include/hosted/poll.h"
#include "include/hosted/pthread.h"
#include "include/hosted/pwd.h"
#include "include/hosted/sched.h"
#include "include/hosted/signal.h"
#include "include/hosted/stdio.h"
#include "include/hosted/stdlib.h"
#include "include/hosted/string.h"
#include "include/hosted/sys/file.h"
#include "include/hosted/sys/prctl.h"
#include "include/hosted/sys/random.h"
#include "include/hosted/sys/stat.h"
#include "include/hosted/sys/times.h"
#include "include/hosted/time.h"
#include "include/hosted/unistd.h"
#include <stdarg.h>
#include <stdint.h>

int64_t pc_syscall(uint64_t number, uint64_t argument1, uint64_t argument2,
                   uint64_t argument3);

static int copy_string(char **cursor, size_t *left, const char *text, char **out)
{
    size_t length = strlen(text) + 1;
    if (length > *left)
        return ERANGE;
    memcpy(*cursor, text, length);
    *out = *cursor;
    *cursor += length;
    *left -= length;
    return 0;
}

static int passwd_copy(const struct passwd *source, struct passwd *entry,
                       char *buffer, size_t size, struct passwd **result)
{
    char *cursor = buffer;
    size_t left = size;
    *result = 0;
    int status = copy_string(&cursor, &left, source->pw_name, &entry->pw_name);
    if (!status)
        status = copy_string(&cursor, &left, source->pw_passwd, &entry->pw_passwd);
    if (!status)
        status = copy_string(&cursor, &left, source->pw_gecos, &entry->pw_gecos);
    if (!status)
        status = copy_string(&cursor, &left, source->pw_dir, &entry->pw_dir);
    if (!status)
        status = copy_string(&cursor, &left, source->pw_shell, &entry->pw_shell);
    if (status)
        return status;
    entry->pw_uid = source->pw_uid;
    entry->pw_gid = source->pw_gid;
    *result = entry;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *entry, char *buffer, size_t size,
               struct passwd **result)
{
    *result = 0;
    struct passwd *found = name ? getpwnam(name) : 0;
    return found ? passwd_copy(found, entry, buffer, size, result) : 0;
}

int getpwuid_r(uid_t uid, struct passwd *entry, char *buffer, size_t size,
               struct passwd **result)
{
    *result = 0;
    struct passwd *found = getpwuid(uid);
    return found ? passwd_copy(found, entry, buffer, size, result) : 0;
}

static char group_line[1024];
static char *group_members[64];
static struct group group_entry;

static int group_parse(char *line, const char *want_name, long want_gid,
                       struct group *entry)
{
    char *fields[4];
    int count = 0;
    char *cursor = line;
    fields[count++] = cursor;
    while (*cursor && count < 4)
    {
        if (*cursor == ':')
        {
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
        cursor++;
    }
    if (count < 4)
        return 0;
    char *newline = strchr(fields[3], '\n');
    if (newline)
        *newline = '\0';
    long gid = strtol(fields[2], 0, 10);
    if (want_name ? strcmp(fields[0], want_name) != 0 : gid != want_gid)
        return 0;
    entry->gr_name = fields[0];
    entry->gr_passwd = fields[1];
    entry->gr_gid = (gid_t)gid;
    int members = 0;
    char *member = fields[3];
    while (*member && members < 63)
    {
        group_members[members++] = member;
        char *comma = strchr(member, ',');
        if (!comma)
            break;
        *comma = '\0';
        member = comma + 1;
    }
    group_members[members] = 0;
    entry->gr_mem = group_members;
    return 1;
}

static struct group *group_lookup(const char *name, long gid)
{
    FILE *file = fopen("/etc/group", "r");
    if (!file)
        return 0;
    struct group *result = 0;
    while (fgets(group_line, sizeof(group_line), file))
    {
        if (group_parse(group_line, name, gid, &group_entry))
        {
            result = &group_entry;
            break;
        }
    }
    fclose(file);
    return result;
}

struct group *getgrnam(const char *name) { return name ? group_lookup(name, 0) : 0; }
struct group *getgrgid(gid_t gid) { return group_lookup(0, (long)gid); }

static int group_copy(const struct group *source, struct group *entry, char *buffer,
                      size_t size, struct group **result)
{
    char *cursor = buffer;
    size_t left = size;
    *result = 0;
    int status = copy_string(&cursor, &left, source->gr_name, &entry->gr_name);
    if (!status)
        status = copy_string(&cursor, &left, source->gr_passwd, &entry->gr_passwd);
    if (status)
        return status;
    size_t members = 0;
    while (source->gr_mem && source->gr_mem[members])
        members++;
    size_t table = (members + 1) * sizeof(char *);
    size_t padding = (size_t)(-(uintptr_t)cursor) & (sizeof(char *) - 1);
    if (padding + table > left)
        return ERANGE;
    cursor += padding;
    left -= padding;
    char **list = (char **)cursor;
    cursor += table;
    left -= table;
    for (size_t i = 0; i < members; i++)
    {
        status = copy_string(&cursor, &left, source->gr_mem[i], &list[i]);
        if (status)
            return status;
    }
    list[members] = 0;
    entry->gr_mem = list;
    entry->gr_gid = source->gr_gid;
    *result = entry;
    return 0;
}

int getgrnam_r(const char *name, struct group *entry, char *buffer, size_t size,
               struct group **result)
{
    *result = 0;
    struct group *found = name ? getgrnam(name) : 0;
    return found ? group_copy(found, entry, buffer, size, result) : 0;
}

int getgrgid_r(gid_t gid, struct group *entry, char *buffer, size_t size,
               struct group **result)
{
    *result = 0;
    struct group *found = getgrgid(gid);
    return found ? group_copy(found, entry, buffer, size, result) : 0;
}

ssize_t getrandom(void *buffer, size_t length, unsigned int flags)
{
    (void)flags;
    if (!buffer && length)
    {
        errno = EFAULT;
        return -1;
    }
    size_t done = 0;
    while (done < length)
    {
        size_t chunk = length - done;
        if (chunk > 65536)
            chunk = 65536;
        int64_t got = pc_syscall(SYS_GETRANDOM, (uint64_t)(uintptr_t)buffer + done,
                                 chunk, 0);
        if (got <= 0)
        {
            errno = EIO;
            return done ? (ssize_t)done : -1;
        }
        done += (size_t)got;
    }
    return (ssize_t)done;
}

int getentropy(void *buffer, size_t length)
{
    if (length > 256)
    {
        errno = EIO;
        return -1;
    }
    return getrandom(buffer, length, 0) == (ssize_t)length ? 0 : -1;
}

int clock_nanosleep(int clock_id, int flags, const struct timespec *request,
                    struct timespec *remaining)
{
    if (!request || request->tv_nsec < 0 || request->tv_nsec >= 1000000000L)
        return EINVAL;
    struct timespec relative = *request;
    if (flags & TIMER_ABSTIME)
    {
        struct timespec now;
        if (clock_gettime(clock_id, &now) < 0)
            return errno;
        relative.tv_sec = request->tv_sec - now.tv_sec;
        relative.tv_nsec = request->tv_nsec - now.tv_nsec;
        if (relative.tv_nsec < 0)
        {
            relative.tv_sec--;
            relative.tv_nsec += 1000000000L;
        }
        if (relative.tv_sec < 0)
            return 0;
    }
    if (nanosleep(&relative, (flags & TIMER_ABSTIME) ? 0 : remaining) < 0)
        return errno;
    return 0;
}

int ppoll(struct pollfd *fds, nfds_t count, const struct timespec *timeout,
          const sigset_t *mask)
{
    int milliseconds = -1;
    if (timeout)
    {
        long long total = (long long)timeout->tv_sec * 1000LL +
                          (timeout->tv_nsec + 999999L) / 1000000L;
        milliseconds = total > 0x7FFFFFFF ? 0x7FFFFFFF : (int)total;
    }
    sigset_t saved;
    if (mask)
        sigprocmask(SIG_SETMASK, mask, &saved);
    int result = poll(fds, count, milliseconds);
    int saved_errno = errno;
    if (mask)
        sigprocmask(SIG_SETMASK, &saved, 0);
    errno = saved_errno;
    return result;
}

int pthread_spin_init(pthread_spinlock_t *lock, int shared)
{
    (void)shared;
    *lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock)
{
    (void)lock;
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock)
{
    return __atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) ? EBUSY : 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock)
{
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE))
    {
        while (__atomic_load_n(lock, __ATOMIC_RELAXED))
            sched_yield();
    }
    return 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock)
{
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    return 0;
}

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr,
                         unsigned count)
{
    (void)attr;
    if (!count)
        return EINVAL;
    barrier->count = count;
    barrier->waiting = 0;
    barrier->generation = 0;
    pthread_mutex_init(&barrier->lock, 0);
    pthread_cond_init(&barrier->wake, 0);
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    pthread_cond_destroy(&barrier->wake);
    pthread_mutex_destroy(&barrier->lock);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    pthread_mutex_lock(&barrier->lock);
    unsigned generation = barrier->generation;
    if (++barrier->waiting == barrier->count)
    {
        barrier->waiting = 0;
        barrier->generation++;
        pthread_cond_broadcast(&barrier->wake);
        pthread_mutex_unlock(&barrier->lock);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (generation == barrier->generation)
        pthread_cond_wait(&barrier->wake, &barrier->lock);
    pthread_mutex_unlock(&barrier->lock);
    return 0;
}

int flock(int fd, int operation)
{
    if (fcntl(fd, F_GETFD) < 0)
        return -1;
    int base = operation & ~LOCK_NB;
    if (base != LOCK_SH && base != LOCK_EX && base != LOCK_UN)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int posix_fallocate(int fd, off_t offset, off_t length)
{
    if (offset < 0 || length <= 0)
        return EINVAL;
    struct stat info;
    if (fstat(fd, &info) < 0)
        return errno;
    if (offset + length > (off_t)info.st_size && ftruncate(fd, offset + length) < 0)
        return errno;
    return 0;
}

int prctl(int option, ...)
{
    va_list arguments;
    va_start(arguments, option);
    int result = -1;
    if (option == PR_SET_NAME)
    {
        const char *name = va_arg(arguments, const char *);
        result = name && pthread_setname_np(pthread_self(), name) == 0 ? 0 : -1;
        if (result < 0)
            errno = EFAULT;
    }
    else if (option == PR_GET_NAME)
    {
        char *name = va_arg(arguments, char *);
        result = name && pthread_getname_np(pthread_self(), name, 16) == 0 ? 0 : -1;
        if (result < 0)
            errno = EFAULT;
    }
    else
        errno = EINVAL;
    va_end(arguments);
    return result;
}

int sched_getaffinity(int pid, unsigned long size, cpu_set_t *set)
{
    (void)pid;
    if (!set || size < sizeof(unsigned long))
    {
        errno = EINVAL;
        return -1;
    }
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus < 1)
        cpus = 1;
    if (size > sizeof(*set))
        size = sizeof(*set);
    memset(set, 0, size);
    for (long i = 0; i < cpus && (unsigned long)i < size * 8; i++)
        CPU_SET(i, set);
    return 0;
}

int sched_setaffinity(int pid, unsigned long size, const cpu_set_t *set)
{
    (void)pid;
    (void)size;
    (void)set;
    errno = ENOSYS;
    return -1;
}

int pthread_setaffinity_np(pthread_t thread, size_t size, const cpu_set_t *set)
{
    (void)thread;
    (void)size;
    (void)set;
    return ENOSYS;
}

int pthread_getaffinity_np(pthread_t thread, size_t size, cpu_set_t *set)
{
    (void)thread;
    return sched_getaffinity(0, size, set) == 0 ? 0 : errno;
}

clock_t times(struct tms *buffer)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return (clock_t)-1;
    clock_t ticks = (clock_t)(now.tv_sec * 100L + now.tv_nsec / 10000000L);
    if (buffer)
    {
        buffer->tms_utime = clock() / (CLOCKS_PER_SEC / 100);
        buffer->tms_stime = 0;
        buffer->tms_cutime = 0;
        buffer->tms_cstime = 0;
    }
    return ticks;
}
