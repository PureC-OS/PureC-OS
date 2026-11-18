#include <arch/arch.h>
#include "klog_disk.h"
#include "klog.h"
#include "../../drivers/interrupts/timer.h"
#include "../process/scheduler.h"
#include "../syscall/syscall.h"
#include <stdbool.h>
#include <stdint.h>

#define KLOG_DISK_CHUNK (64 * 1024)
#define KLOG_DISK_MAX_BYTES 0xFFFFFFFFULL
#define KLOG_DISK_PATH "/kernel.log"

static char buffer[KLOG_DISK_CHUNK];

static inline int64_t kernel_syscall(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3)
{
    return arch_kernel_syscall(number, a1, a2, a3, 0, 0);
}

void klog_disk_thread(void *arg)
{
    (void)arg;
    uint64_t cursor = 0;
    uint64_t file_size = 0;
    uint32_t pending = 0;
    uint64_t last_flush = timer_ticks();
    int64_t rc = kernel_syscall(SYS_FILE_WRITE, (uint64_t)KLOG_DISK_PATH, 0, 0);
    if (rc < 0) {
        klogf(KLOG_ERROR, "klog-disk: cannot create %s status=%d; persistent logging disabled",
              KLOG_DISK_PATH, (int)rc);
        scheduler_exit();
        return;
    }
    klogf(KLOG_OK, "klog-disk: streaming enabled path=%s chunk=%u", KLOG_DISK_PATH, KLOG_DISK_CHUNK);
    bool recreate = false;
    for (;;) {
        bool lost = false;
        pending += klog_read_since(&cursor, buffer + pending, sizeof(buffer) - pending, &lost);
        if (lost)
            klogf(KLOG_ERROR, "klog-disk: RAM ring overrun cursor advanced to=%llu total=%llu", cursor,
                  klog_total_bytes());
        if (install_job_running()) {
            pending = 0;
            recreate = true;
            scheduler_sleep(100);
            continue;
        }
        if (recreate) {
            recreate = false;
            file_size = 0;
            if (kernel_syscall(SYS_FILE_WRITE, (uint64_t)KLOG_DISK_PATH, 0, 0) < 0) {
                klogf(KLOG_WARN, "klog-disk: cannot create %s on the new root; persistent logging disabled",
                      KLOG_DISK_PATH);
                scheduler_exit();
                return;
            }
        }
        uint64_t now = timer_ticks();
        if (pending == 0 || (pending < sizeof(buffer) && now - last_flush < 1000)) {
            scheduler_sleep(20);
            continue;
        }
        uint64_t remaining = KLOG_DISK_MAX_BYTES - file_size;
        uint32_t amount = pending > remaining ? (uint32_t)remaining : pending;
        if (!amount) {
            klogf(KLOG_WARN, "klog-disk: file reached size limit bytes=%llu", file_size);
            scheduler_exit();
            return;
        }
        rc = kernel_syscall(SYS_FILE_APPEND, (uint64_t)KLOG_DISK_PATH, (uint64_t)buffer, amount);
        if ((rc < 0 || (uint32_t)rc != amount) && install_job_running()) {
            pending = 0;
            recreate = true;
            continue;
        }
        if ((rc < 0 || (uint32_t)rc != amount) &&
            kernel_syscall(SYS_FILE_WRITE, (uint64_t)KLOG_DISK_PATH, 0, 0) >= 0) {
            file_size = 0;
            rc = kernel_syscall(SYS_FILE_APPEND, (uint64_t)KLOG_DISK_PATH, (uint64_t)buffer, amount);
        }
        if (rc < 0 || (uint32_t)rc != amount) {
            klogf(KLOG_ERROR, "klog-disk: append failed status=%d requested=%u", (int)rc, amount);
            scheduler_exit();
            return;
        }
        file_size += amount;
        pending = 0;
        last_flush = now;
        scheduler_sleep(20);
    }
}
