#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define WORKERS 4
#define INCREMENTS 5000

static int failures;

static __thread int tls_init = 42;
static __thread int tls_zero;
static __thread char tls_buffer[300] = "tdata-string";

static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
static long counter;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cond_lock = PTHREAD_MUTEX_INITIALIZER;
static int cond_value;
static volatile int detached_done;
static volatile int key_destroyed;
static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int once_runs;

static void check(const char *name, int ok) {
    printf("thrtest %-28s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static void *tls_worker(void *argument) {
    long id = (long)argument;
    int ok = tls_init == 42 && tls_zero == 0 &&
             strcmp(tls_buffer, "tdata-string") == 0;
    tls_init += (int)id;
    tls_zero = (int)id * 10;
    tls_buffer[0] = (char)('a' + id);
    for (int i = 0; i < 100; i++)
        sched_yield();
    ok = ok && tls_init == 42 + (int)id && tls_zero == (int)id * 10 &&
         tls_buffer[0] == (char)('a' + id);
    return (void *)(long)ok;
}

static void *counter_worker(void *argument) {
    (void)argument;
    for (int i = 0; i < INCREMENTS; i++) {
        pthread_mutex_lock(&counter_lock);
        counter++;
        pthread_mutex_unlock(&counter_lock);
    }
    return 0;
}

static void *cond_waiter(void *argument) {
    (void)argument;
    pthread_mutex_lock(&cond_lock);
    while (cond_value == 0)
        pthread_cond_wait(&cond, &cond_lock);
    int seen = cond_value;
    pthread_mutex_unlock(&cond_lock);
    return (void *)(long)seen;
}

static void *detached_worker(void *argument) {
    (void)argument;
    detached_done = 1;
    return 0;
}

static void destructor(void *value) { key_destroyed = (int)(long)value; }

static void once_routine(void) { once_runs++; }

static void *key_worker(void *argument) {
    (void)argument;
    pthread_once(&once, once_routine);
    pthread_setspecific(key, (void *)77);
    return pthread_getspecific(key);
}

static void *malloc_worker(void *argument) {
    long id = (long)argument;
    void *blocks[64];
    int ok = 1;
    for (int round = 0; round < 200; round++) {
        for (int i = 0; i < 64; i++) {
            size_t size = 16 + (size_t)((i * 37 + round) % 300);
            blocks[i] = malloc(size);
            if (!blocks[i]) { ok = 0; continue; }
            memset(blocks[i], (int)(id + i), size);
        }
        for (int i = 0; i < 64; i++) {
            if (!blocks[i]) continue;
            if (((unsigned char *)blocks[i])[0] != (unsigned char)(id + i)) ok = 0;
            free(blocks[i]);
        }
    }
    return (void *)(long)ok;
}

static void *spin_forever(void *argument) {
    (void)argument;
    for (;;)
        sched_yield();
    return 0;
}

static void *exit_from_thread(void *argument) {
    (void)argument;
    struct timespec pause = {0, 20 * 1000 * 1000};
    nanosleep(&pause, 0);
    exit(6);
    return 0;
}

static void *sleeper(void *argument) {
    (void)argument;
    sleep(30);
    return 0;
}

static int wait_status(pid_t child) {
    int status = 0;
    if (waitpid(child, &status, 0) != child)
        return -1;
    return status;
}

int main(void) {
    check("main tls initial", tls_init == 42 && tls_zero == 0 &&
                                  strcmp(tls_buffer, "tdata-string") == 0);
    tls_init = 1000;

    pthread_t workers[WORKERS];
    void *result;
    int all_ok = 1;
    for (long i = 0; i < WORKERS; i++)
        if (pthread_create(&workers[i], 0, tls_worker, (void *)(i + 1)) != 0)
            all_ok = 0;
    for (int i = 0; i < WORKERS; i++) {
        if (pthread_join(workers[i], &result) != 0 || !result)
            all_ok = 0;
    }
    check("tls isolation", all_ok && tls_init == 1000);

    pthread_t self = pthread_self();
    check("pthread_self stable", pthread_equal(self, pthread_self()));

    for (int i = 0; i < WORKERS; i++)
        pthread_create(&workers[i], 0, counter_worker, 0);
    for (int i = 0; i < WORKERS; i++)
        pthread_join(workers[i], 0);
    check("mutex counter", counter == (long)WORKERS * INCREMENTS);

    pthread_t waiter;
    pthread_create(&waiter, 0, cond_waiter, 0);
    struct timespec pause = {0, 30 * 1000 * 1000};
    nanosleep(&pause, 0);
    pthread_mutex_lock(&cond_lock);
    cond_value = 9;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&cond_lock);
    pthread_join(waiter, &result);
    check("condvar wake", (long)result == 9);

    pthread_mutex_t timed_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t timed_cond = PTHREAD_COND_INITIALIZER;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 50 * 1000 * 1000;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&timed_lock);
    int timed = pthread_cond_timedwait(&timed_cond, &timed_lock, &deadline);
    pthread_mutex_unlock(&timed_lock);
    check("condvar timeout", timed == ETIMEDOUT);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t detached;
    int created = pthread_create(&detached, &attr, detached_worker, 0);
    for (int i = 0; i < 200 && !detached_done; i++) {
        struct timespec tick = {0, 5 * 1000 * 1000};
        nanosleep(&tick, 0);
    }
    check("detached thread", created == 0 && detached_done == 1);

    pthread_key_create(&key, destructor);
    pthread_t keyed[2];
    void *got[2] = {0, 0};
    for (int i = 0; i < 2; i++)
        pthread_create(&keyed[i], 0, key_worker, 0);
    for (int i = 0; i < 2; i++)
        pthread_join(keyed[i], &got[i]);
    check("pthread_once", once_runs == 1);
    check("pthread key + destructor",
          (long)got[0] == 77 && (long)got[1] == 77 && key_destroyed == 77);

    for (long i = 0; i < WORKERS; i++)
        pthread_create(&workers[i], 0, malloc_worker, (void *)(i + 1));
    all_ok = 1;
    for (int i = 0; i < WORKERS; i++) {
        pthread_join(workers[i], &result);
        if (!result) all_ok = 0;
    }
    check("malloc from threads", all_ok);

    pid_t child = fork();
    if (child == 0) {
        int ok = tls_init == 1000;
        pthread_t t;
        ok = ok && pthread_create(&t, 0, tls_worker, (void *)3) == 0;
        void *r = 0;
        ok = ok && pthread_join(t, &r) == 0 && r;
        _exit(ok ? 0 : 1);
    }
    check("fork keeps tls", wait_status(child) == 0);

    child = fork();
    if (child == 0) {
        pthread_t t;
        pthread_create(&t, 0, spin_forever, 0);
        pthread_create(&t, 0, sleeper, 0);
        struct timespec brief = {0, 40 * 1000 * 1000};
        nanosleep(&brief, 0);
        exit(5);
    }
    check("exit kills siblings", wait_status(child) == (5 << 8));

    child = fork();
    if (child == 0) {
        pthread_t t;
        pthread_create(&t, 0, exit_from_thread, 0);
        pthread_join(t, 0);
        _exit(1);
    }
    check("exit from non-main", wait_status(child) == (6 << 8));

    printf("thrtest done failures=%d\n", failures);
    return failures ? 1 : 0;
}
