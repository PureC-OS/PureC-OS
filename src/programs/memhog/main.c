#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHUNK (1024UL * 1024UL)
#define REPORT_MB 128

static void show_meminfo(const char *label) {
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file) return;
    char line[128];
    printf("memhog %s:", label);
    for (int i = 0; i < 3 && fgets(line, sizeof(line), file); i++) {
        line[strcspn(line, "\n")] = 0;
        printf(" [%s]", line);
    }
    printf("\n");
    fclose(file);
}

static void pause_seconds(unsigned seconds) {
    struct timespec step = {1, 0};
    for (unsigned i = 0; i < seconds; i++) nanosleep(&step, 0);
}

static unsigned long hog(unsigned long target_mb, int *denied) {
    unsigned long got = 0;
    *denied = 0;
    while (got < target_mb) {
        unsigned char *block = mmap(0, CHUNK, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (block == MAP_FAILED) {
            *denied = errno ? errno : 1;
            break;
        }
        for (unsigned long offset = 0; offset < CHUNK; offset += 4096)
            block[offset] = (unsigned char)(got + offset);
        got++;
        if (got % REPORT_MB == 0)
            printf("memhog pid=%d holds %lu MiB\n", (int)getpid(), got);
    }
    return got;
}

static int run_single(unsigned long target_mb, unsigned hold) {
    int denied;
    unsigned long got = hog(target_mb, &denied);
    printf("memhog pid=%d got %lu of %lu MiB%s (errno %d)\n", (int)getpid(),
           got, target_mb, denied ? " DENIED" : "", denied);
    fflush(stdout);
    pause_seconds(hold);
    return denied ? 3 : 0;
}

struct thread_job { unsigned long target_mb; unsigned hold; unsigned long got; int denied; };

static void *thread_hog(void *argument) {
    struct thread_job *job = argument;
    job->got = hog(job->target_mb, &job->denied);
    pause_seconds(job->hold);
    return 0;
}

static int run_threads(int count, unsigned long each_mb, unsigned hold) {
    pthread_t threads[16];
    struct thread_job jobs[16];
    if (count > 16) count = 16;
    int started = 0;
    for (int i = 0; i < count; i++) {
        jobs[i] = (struct thread_job){each_mb, hold, 0, 0};
        if (pthread_create(&threads[i], 0, thread_hog, &jobs[i]) != 0) {
            printf("memhog pthread_create %d failed\n", i);
            break;
        }
        started++;
    }
    unsigned long total = 0;
    int denied = 0;
    for (int i = 0; i < started; i++) {
        pthread_join(threads[i], 0);
        total += jobs[i].got;
        if (jobs[i].denied) denied++;
    }
    printf("memhog threads started=%d total=%lu MiB denied=%d\n", started,
           total, denied);
    return denied ? 3 : 0;
}

static int run_fork(int count, unsigned long each_mb, unsigned hold) {
    pid_t children[32];
    if (count > 32) count = 32;
    int started = 0;
    for (int i = 0; i < count; i++) {
        pid_t child = fork();
        if (child == 0)
            _exit(run_single(each_mb, hold));
        if (child < 0) {
            printf("memhog fork %d failed errno %d\n", i, errno);
            break;
        }
        children[started++] = child;
    }
    for (int i = 0; i < started; i++) {
        int status = 0;
        pid_t done = waitpid(children[i], &status, 0);
        if (done < 0) {
            printf("memhog child %d: waitpid failed errno %d\n", (int)children[i], errno);
        } else if (status & 0x7f) {
            printf("memhog child %d: KILLED by signal %d\n", (int)done, status & 0x7f);
        } else {
            printf("memhog child %d: exit %d\n", (int)done, (status >> 8) & 0xff);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    show_meminfo("before");
    int result = 0;
    if (argc >= 4 && strcmp(argv[1], "fork") == 0)
        result = run_fork(atoi(argv[2]), strtoul(argv[3], 0, 10),
                          argc > 4 ? (unsigned)atoi(argv[4]) : 5);
    else if (argc >= 4 && strcmp(argv[1], "threads") == 0)
        result = run_threads(atoi(argv[2]), strtoul(argv[3], 0, 10),
                             argc > 4 ? (unsigned)atoi(argv[4]) : 5);
    else if (argc >= 2)
        result = run_single(strtoul(argv[1], 0, 10),
                            argc > 2 ? (unsigned)atoi(argv[2]) : 5);
    else
        printf("usage: memhog <mb> [hold_s] | fork <n> <mb> [hold_s] | threads <n> <mb> [hold_s]\n");
    show_meminfo("after");
    fflush(stdout);
    return result;
}
