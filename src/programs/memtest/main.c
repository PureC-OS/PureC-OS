#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE 4096UL
#define MIB (1024UL * 1024UL)

static int failures;

static void check(const char *name, int ok) {
    printf("memtest %-32s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static long mem_free_kb(void) {
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file) return -1;
    char line[128];
    long value = -1;
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "MemFree:", 8) == 0) {
            value = strtol(line + 8, 0, 10);
            break;
        }
    }
    fclose(file);
    return value;
}

static int crashes_with(void (*body)(void *), void *argument, int signal_number) {
    pid_t child = fork();
    if (child == 0) {
        body(argument);
        _exit(0);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child) return 0;
    return (status & 0x7f) == signal_number;
}

static void touch_byte(void *address) { *(volatile char *)address = 1; }

static void read_byte(void *address) { (void)*(volatile char *)address; }

struct race_job {
    unsigned char *base;
    unsigned pages;
    unsigned id;
};

static void *race_worker(void *argument) {
    struct race_job *job = argument;
    for (unsigned round = 0; round < 3; round++)
        for (unsigned page = 0; page < job->pages; page++)
            job->base[page * PAGE + job->id] = (unsigned char)(job->id + 1);
    return 0;
}

int main(void) {
    long before = mem_free_kb();
    unsigned char *big = mmap(0, 1024 * MIB, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    long after_map = mem_free_kb();
    check("mmap 1 GiB is lazy", big != MAP_FAILED && before > 0 &&
                                    before - after_map < 4096);
    int zero = 1;
    for (unsigned page = 0; page < 64 && big != MAP_FAILED; page++)
        for (unsigned i = 0; i < PAGE; i += 512)
            if (big[page * PAGE + i]) zero = 0;
    check("untouched pages read zero", zero);
    for (unsigned page = 0; page < 64; page++)
        big[page * PAGE] = (unsigned char)page;
    long after_touch = mem_free_kb();
    long used = after_map - after_touch;
    check("touch allocates on demand", used >= 200 && used < 2048);
    int pattern = 1;
    for (unsigned page = 0; page < 64; page++)
        if (big[page * PAGE] != (unsigned char)page) pattern = 0;
    check("touched data is kept", pattern);
    munmap(big, 1024 * MIB);
    long after_unmap = mem_free_kb();
    check("munmap returns memory", after_unmap >= after_touch + used - 64);

    unsigned char *reserve = mmap(0, 64UL * 1024 * MIB, PROT_NONE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    check("64 GiB PROT_NONE reservation", reserve != MAP_FAILED);
    if (reserve != MAP_FAILED) {
        check("PROT_NONE access faults",
              crashes_with(read_byte, reserve, SIGSEGV));
        int status = mprotect(reserve, MIB, PROT_READ | PROT_WRITE);
        if (status == 0) reserve[123] = 77;
        check("mprotect commits part of reservation",
              status == 0 && reserve[123] == 77);
        check("rest of reservation still faults",
              crashes_with(touch_byte, reserve + 2 * MIB, SIGSEGV));
        munmap(reserve, 64UL * 1024 * MIB);
    }

    unsigned char *area = mmap(0, 16 * PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for (unsigned page = 0; page < 16; page++) area[page * PAGE] = (unsigned char)(page + 1);
    munmap(area + 4 * PAGE, 4 * PAGE);
    check("munmap hole keeps neighbours",
          area[3 * PAGE] == 4 && area[8 * PAGE] == 9);
    check("munmap hole faults", crashes_with(read_byte, area + 5 * PAGE, SIGSEGV));
    munmap(area, 4 * PAGE);
    munmap(area + 8 * PAGE, 8 * PAGE);

    unsigned char *prot = mmap(0, 4 * PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    prot[0] = 5;
    int rc = mprotect(prot, 4 * PAGE, PROT_READ);
    check("mprotect read-only keeps data", rc == 0 && prot[0] == 5);
    check("write to read-only faults", crashes_with(touch_byte, prot + PAGE, SIGSEGV));
    check("write to touched read-only faults", crashes_with(touch_byte, prot, SIGSEGV));
    rc = mprotect(prot, 4 * PAGE, PROT_READ | PROT_WRITE);
    prot[PAGE] = 6;
    check("mprotect back to writable", rc == 0 && prot[0] == 5 && prot[PAGE] == 6);
    munmap(prot, 4 * PAGE);

    unsigned char *shared = mmap(0, 8 * PAGE, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for (unsigned page = 0; page < 4; page++) shared[page * PAGE] = 0xA0 + page;
    pid_t child = fork();
    if (child == 0) {
        int ok = shared[0] == 0xA0 && shared[3 * PAGE] == 0xA3 && shared[6 * PAGE] == 0;
        shared[0] = 0xEE;
        shared[6 * PAGE] = 0xDD;
        _exit(ok && shared[0] == 0xEE && shared[6 * PAGE] == 0xDD ? 0 : 1);
    }
    int status = 0;
    waitpid(child, &status, 0);
    check("fork child sees lazy copy", status == 0);
    check("fork isolates writes", shared[0] == 0xA0 && shared[6 * PAGE] == 0);
    munmap(shared, 8 * PAGE);

    unsigned char *buffer = mmap(0, 4 * PAGE, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int pipefd[2];
    pipe(pipefd);
    const char message[] = "hello lazy kernel buffer";
    write(pipefd[1], message, sizeof(message));
    ssize_t got = read(pipefd[0], buffer + 2 * PAGE + 100, sizeof(message));
    check("read() into untouched mmap", got == (ssize_t)sizeof(message) &&
                                            strcmp((char *)buffer + 2 * PAGE + 100, message) == 0);
    unsigned char *source = mmap(0, PAGE, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ssize_t sent = write(pipefd[1], source, 16);
    check("write() from untouched mmap", sent == 16);
    close(pipefd[0]);
    close(pipefd[1]);
    munmap(buffer, 4 * PAGE);
    munmap(source, PAGE);

    unsigned char *grow = mmap(0, 2 * PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    grow[0] = 11;
    grow[PAGE] = 22;
    unsigned char *moved = mremap(grow, 2 * PAGE, 200 * PAGE, MREMAP_MAYMOVE);
    check("mremap grows and keeps data", moved != MAP_FAILED && moved[0] == 11 &&
                                             moved[PAGE] == 22);
    if (moved != MAP_FAILED) {
        moved[150 * PAGE] = 33;
        check("mremap new area usable", moved[150 * PAGE] == 33);
        void *shrunk = mremap(moved, 200 * PAGE, 3 * PAGE, 0);
        check("mremap shrinks", shrunk == moved && moved[0] == 11);
        munmap(moved, 3 * PAGE);
    }

    unsigned pages = 64;
    unsigned char *race = mmap(0, pages * PAGE, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    pthread_t threads[8];
    struct race_job jobs[8];
    int created = 0;
    for (unsigned i = 0; i < 8; i++) {
        jobs[i] = (struct race_job){race, pages, i};
        if (pthread_create(&threads[i], 0, race_worker, &jobs[i]) == 0) created++;
    }
    for (int i = 0; i < created; i++) pthread_join(threads[i], 0);
    int intact = created == 8;
    for (unsigned page = 0; page < pages; page++)
        for (unsigned i = 0; i < 8; i++)
            if (race[page * PAGE + i] != (unsigned char)(i + 1)) intact = 0;
    check("concurrent first touch (8 threads)", intact);
    munmap(race, pages * PAGE);

    long end_free = mem_free_kb();
    check("no memory leaked", before > 0 && before - end_free < 2048);
    printf("memtest done failures=%d\n", failures);
    return failures ? 1 : 0;
}
