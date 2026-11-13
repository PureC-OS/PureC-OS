#include "../../libc/include/purec.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DHCPC_PATH "/bin/dhcpc"
#define RESOLVED_PATH "/bin/resolved"
#define RESOLVED_SLOT "@resolved"
#define MAX_CHILDREN (NET_IF_MAX_COUNT + 1)

struct child {
    char name[NET_IF_NAME_CAPACITY];
    pid_t pid;
    unsigned failures;
    time_t retry_at;
    time_t started_at;
    bool given_up;
};

static struct child children[MAX_CHILDREN];
static volatile sig_atomic_t stop;

static void on_term(int sig)
{
    (void)sig;
    stop = 1;
}

static struct child *slot_for(const char *name, bool create)
{
    for (int i = 0; i < MAX_CHILDREN; i++)
        if (children[i].name[0] && strcmp(children[i].name, name) == 0)
            return &children[i];
    if (!create)
        return 0;
    for (int i = 0; i < MAX_CHILDREN; i++)
        if (!children[i].name[0]) {
            memset(&children[i], 0, sizeof(children[i]));
            snprintf(children[i].name, sizeof(children[i].name), "%s", name);
            return &children[i];
        }
    return 0;
}

static void kernel_dhcp(const char *name, uint32_t op)
{
    struct net_if_config c;
    memset(&c, 0, sizeof(c));
    snprintf(c.name, sizeof(c.name), "%s", name);
    c.op = op;
    (void)pc_net_configure(&c);
}

static bool is_resolver(const struct child *c) { return strcmp(c->name, RESOLVED_SLOT) == 0; }

static void start(struct child *c)
{
    bool resolver = is_resolver(c);
    if (!resolver)
        kernel_dhcp(c->name, NET_CFG_DHCP_STOP);
    c->started_at = time(0);
    pid_t pid = fork();
    if (pid == 0) {
        const char *path = resolver ? RESOLVED_PATH : DHCPC_PATH;
        char *args[3] = {(char *)(resolver ? "resolved" : "dhcpc"), resolver ? 0 : c->name, 0};
        execv(path, args);
        printf("netd: cannot run %s: %s\n", path, strerror(errno));
        _exit(127);
    }
    if (pid < 0) {
        printf("netd: fork failed for %s\n", c->name);
        c->retry_at = time(0) + 5;
        return;
    }
    c->pid = pid;
    printf("netd: started %s%s%s (pid %d)\n", resolver ? "resolved" : "dhcpc for ",
           resolver ? "" : c->name, resolver ? " on 127.0.0.1:53" : "", (int)pid);
}

static void reap(void)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_CHILDREN; i++) {
            struct child *c = &children[i];
            if (c->pid != pid)
                continue;
            c->pid = 0;
            if (time(0) - c->started_at > 60)
                c->failures = 0;
            c->failures++;
            if (c->failures >= 3 && is_resolver(c)) {
                c->given_up = true;
                printf("netd: resolved keeps failing, programs fall back to kernel DNS\n");
                continue;
            }
            if (c->failures >= 3) {
                c->given_up = true;
                kernel_dhcp(c->name, NET_CFG_DHCP_START);
                printf("netd: dhcpc for %s keeps failing, giving %s back to kernel DHCP\n",
                       c->name, c->name);
                continue;
            }
            unsigned delay = 2u << (c->failures < 5 ? c->failures : 5);
            if (delay > 60)
                delay = 60;
            c->retry_at = time(0) + delay;
            printf("netd: dhcpc for %s exited (status %d), restart in %u s\n", c->name,
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1, delay);
        }
    }
}

static void scan(void)
{
    struct net_if_info list[NET_IF_MAX_COUNT];
    int32_t n = pc_net_if_list(list, NET_IF_MAX_COUNT);
    time_t now = time(0);
    for (int32_t i = 0; i < n; i++) {
        if (strcmp(list[i].name, "lo") == 0)
            continue;
        struct child *c = slot_for(list[i].name, true);
        if (!c || c->pid || c->given_up || now < c->retry_at)
            continue;
        start(c);
    }
    struct child *r = slot_for(RESOLVED_SLOT, true);
    if (r && !r->pid && !r->given_up && now >= r->retry_at)
        start(r);
}

int main(void)
{
    setvbuf(stdout, 0, _IOLBF, 0);
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    printf("netd: started (pid %d)\n", (int)getpid());
    while (!stop) {
        reap();
        scan();
        sleep(2);
    }
    for (int i = 0; i < MAX_CHILDREN; i++)
        if (children[i].pid)
            kill(children[i].pid, SIGTERM);
    for (int i = 0; i < 50; i++) {
        reap();
        sleep(0);
    }
    printf("netd: stopped\n");
    return 0;
}
