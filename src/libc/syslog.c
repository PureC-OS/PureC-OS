#include "include/hosted/syslog.h"
#include <stdarg.h>
#include "include/hosted/stdio.h"
#include "include/purec.h"

static char log_ident[32];

void openlog(const char *ident, int option, int facility)
{
    (void)option;
    (void)facility;
    snprintf(log_ident, sizeof(log_ident), "%s", ident ? ident : "");
}

void syslog(int priority, const char *format, ...)
{
    char text[240];
    int at = 0;
    if (log_ident[0])
        at = snprintf(text, sizeof(text), "%s: ", log_ident);
    va_list ap;
    va_start(ap, format);
    vsnprintf(text + at, sizeof(text) - (size_t)at, format, ap);
    va_end(ap);
    int level = priority & 7;
    uint64_t kernel = level <= LOG_ERR ? 3 : level == LOG_WARNING ? 2 : level == LOG_NOTICE ? 1 : 0;
    pc_syscall(SYS_KLOG_WRITE, kernel, (uint64_t)(uintptr_t)text, 0);
}

void closelog(void) { log_ident[0] = 0; }
