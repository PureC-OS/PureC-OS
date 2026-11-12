#include "pty.h"
#include "../../lib/string.h"
#include "../../mm/pmm.h"
#include "../../drivers/interrupts/timer.h"
#include "../process/process.h"
#include "../process/scheduler.h"
#include "../process/signal.h"
#include "../sync/mutex.h"
#include "../syscall/syscall.h"
#include "../tty/tty.h"

#define PTY_MAX 32
#define PTY_RING 8192u
#define PTY_LINE 1024u
#define PTY_WAITERS 8

struct pty_ring {
    uint8_t *data;
    uint32_t head;
    uint32_t count;
};

struct pty {
    bool used;
    bool master_open;
    bool slave_open;
    uint64_t phys;
    struct pty_ring in;
    struct pty_ring out;
    uint8_t line[PTY_LINE];
    uint32_t line_len;
    uint32_t lines_ready;
    uint32_t eof_pending;
    struct tty_termios termios;
    struct tty_winsize winsize;
    uint32_t fg_pid;
    uint64_t generation;
    int32_t slave_waiters[PTY_WAITERS];
    int32_t master_waiters[PTY_WAITERS];
};

static mutex_t pty_mutex;
static struct pty ptys[PTY_MAX];

static void waiters_add(int32_t *list, int32_t tid)
{
    for (uint32_t i = 0; i < PTY_WAITERS; i++)
        if (list[i] == tid)
            return;
    for (uint32_t i = 0; i < PTY_WAITERS; i++)
        if (!list[i]) {
            list[i] = tid;
            return;
        }
}

static uint32_t waiters_take(int32_t *list, int32_t *out)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < PTY_WAITERS; i++)
        if (list[i]) {
            out[n++] = list[i];
            list[i] = 0;
        }
    return n;
}

static void wake_all(const int32_t *tids, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        scheduler_unblock(tids[i]);
}

static struct pty *pty_get(int32_t id)
{
    if (id < 0 || id >= PTY_MAX || !ptys[id].used)
        return 0;
    return &ptys[id];
}

static bool ring_put(struct pty_ring *r, uint8_t c)
{
    if (r->count >= PTY_RING)
        return false;
    r->data[(r->head + r->count) % PTY_RING] = c;
    r->count++;
    return true;
}

static uint8_t ring_get(struct pty_ring *r)
{
    uint8_t c = r->data[r->head];
    r->head = (r->head + 1) % PTY_RING;
    r->count--;
    return c;
}

static uint8_t ring_peek(const struct pty_ring *r, uint32_t i)
{
    return r->data[(r->head + i) % PTY_RING];
}

static void default_termios(struct tty_termios *t)
{
    memset(t, 0, sizeof(*t));
    t->iflag = TTY_IFLAG_ICRNL | TTY_IFLAG_IXON;
    t->oflag = TTY_OFLAG_OPOST | TTY_OFLAG_ONLCR;
    t->cflag = TTY_CFLAG_CS8 | TTY_CFLAG_CREAD | TTY_CFLAG_B38400;
    t->lflag = TTY_LFLAG_ISIG | TTY_LFLAG_ICANON | TTY_LFLAG_ECHO | TTY_LFLAG_ECHOE | TTY_LFLAG_ECHOK |
               TTY_LFLAG_ECHOCTL | TTY_LFLAG_ECHOKE | TTY_LFLAG_IEXTEN;
    t->cc[TTY_VINTR] = 3;
    t->cc[TTY_VQUIT] = 28;
    t->cc[TTY_VERASE] = 127;
    t->cc[TTY_VKILL] = 21;
    t->cc[TTY_VEOF] = 4;
    t->cc[TTY_VTIME] = 0;
    t->cc[TTY_VMIN] = 1;
    t->cc[TTY_VSTART] = 17;
    t->cc[TTY_VSTOP] = 19;
    t->cc[TTY_VSUSP] = 26;
    t->ispeed = 38400;
    t->ospeed = 38400;
}

int32_t pty_create(int32_t *id)
{
    mutex_lock(&pty_mutex);
    for (int32_t i = 0; i < PTY_MAX; i++) {
        if (ptys[i].used)
            continue;
        uint64_t phys = pmm_allocate_contiguous(4);
        if (!phys) {
            mutex_unlock(&pty_mutex);
            return -KERR_NOMEM;
        }
        memset(&ptys[i], 0, sizeof(ptys[i]));
        uint8_t *mem = (uint8_t *)pmm_physical_to_virtual(phys);
        ptys[i].used = true;
        ptys[i].phys = phys;
        ptys[i].in.data = mem;
        ptys[i].out.data = mem + PTY_RING;
        ptys[i].master_open = true;
        ptys[i].slave_open = true;
        default_termios(&ptys[i].termios);
        ptys[i].winsize.rows = 24;
        ptys[i].winsize.columns = 80;
        *id = i;
        mutex_unlock(&pty_mutex);
        return 0;
    }
    mutex_unlock(&pty_mutex);
    return -KERR_NFILE;
}

void pty_release(int32_t id, bool master)
{
    int32_t wake[PTY_WAITERS * 2];
    uint32_t n = 0;
    uint32_t hup_pid = 0;
    mutex_lock(&pty_mutex);
    struct pty *p = pty_get(id);
    if (p) {
        if (master) {
            p->master_open = false;
            hup_pid = p->fg_pid;
        } else {
            p->slave_open = false;
        }
        p->generation++;
        n = waiters_take(p->slave_waiters, wake);
        n += waiters_take(p->master_waiters, wake + n);
        if (!p->master_open && !p->slave_open) {
            pmm_free_contiguous(p->phys, 4);
            p->used = false;
        }
    }
    mutex_unlock(&pty_mutex);
    wake_all(wake, n);
    if (hup_pid)
        (void)signal_send_kernel(hup_pid, SIGHUP);
}

static void out_put(struct pty *p, uint8_t c)
{
    if ((p->termios.oflag & TTY_OFLAG_OPOST) && (p->termios.oflag & TTY_OFLAG_ONLCR) && c == '\n')
        (void)ring_put(&p->out, '\r');
    (void)ring_put(&p->out, c);
}

static void echo_char(struct pty *p, uint8_t c)
{
    if (!(p->termios.lflag & TTY_LFLAG_ECHO))
        return;
    if (c < 32 && c != '\n' && c != '\t' && (p->termios.lflag & TTY_LFLAG_ECHOCTL)) {
        out_put(p, '^');
        out_put(p, (uint8_t)(c + 64));
        return;
    }
    out_put(p, c);
}

static void erase_one(struct pty *p)
{
    if (!p->line_len)
        return;
    uint8_t c = p->line[--p->line_len];
    if (!(p->termios.lflag & TTY_LFLAG_ECHO))
        return;
    int width = (c < 32 && (p->termios.lflag & TTY_LFLAG_ECHOCTL)) ? 2 : 1;
    for (int i = 0; i < width; i++) {
        out_put(p, '\b');
        out_put(p, ' ');
        out_put(p, '\b');
    }
}

static void commit_line(struct pty *p)
{
    for (uint32_t i = 0; i < p->line_len; i++)
        (void)ring_put(&p->in, p->line[i]);
    p->line_len = 0;
    p->lines_ready++;
}

static uint32_t input_char(struct pty *p, uint8_t c)
{
    const struct tty_termios *t = &p->termios;
    if (c == '\r' && (t->iflag & TTY_IFLAG_IGNCR))
        return 0;
    if (c == '\r' && (t->iflag & TTY_IFLAG_ICRNL))
        c = '\n';
    else if (c == '\n' && (t->iflag & TTY_IFLAG_INLCR))
        c = '\r';
    if (t->lflag & TTY_LFLAG_ISIG) {
        int sig = 0;
        if (c == t->cc[TTY_VINTR])
            sig = SIGINT;
        else if (c == t->cc[TTY_VQUIT])
            sig = SIGQUIT;
        else if (c == t->cc[TTY_VSUSP])
            sig = SIGTSTP;
        if (sig) {
            echo_char(p, c);
            if (t->lflag & TTY_LFLAG_ECHO)
                out_put(p, '\n');
            p->line_len = 0;
            return (uint32_t)sig;
        }
    }
    if (t->lflag & TTY_LFLAG_ICANON) {
        if (c == t->cc[TTY_VERASE] || c == '\b') {
            erase_one(p);
            return 0;
        }
        if (c == t->cc[TTY_VKILL]) {
            while (p->line_len)
                erase_one(p);
            return 0;
        }
        if (c == t->cc[TTY_VEOF]) {
            if (p->line_len)
                commit_line(p);
            else
                p->eof_pending++;
            return 0;
        }
        if (p->line_len < PTY_LINE - 1)
            p->line[p->line_len++] = c;
        echo_char(p, c);
        if (c == '\n')
            commit_line(p);
        return 0;
    }
    (void)ring_put(&p->in, c);
    echo_char(p, c);
    return 0;
}

static bool slave_readable(const struct pty *p)
{
    if (p->termios.lflag & TTY_LFLAG_ICANON)
        return p->lines_ready > 0 || p->eof_pending > 0;
    return p->in.count > 0;
}

int32_t pty_read(int32_t id, bool master, void *buffer, uint32_t count, bool nonblock)
{
    if (!count)
        return 0;
    int32_t self = scheduler_current_tid();
    uint8_t *out = (uint8_t *)buffer;
    uint64_t deadline = 0;
    for (;;) {
        int32_t wake[PTY_WAITERS];
        uint32_t n = 0;
        mutex_lock(&pty_mutex);
        struct pty *p = pty_get(id);
        if (!p) {
            mutex_unlock(&pty_mutex);
            return -KERR_BADF;
        }
        if (master) {
            if (p->out.count) {
                uint32_t amount = 0;
                while (amount < count && p->out.count)
                    out[amount++] = ring_get(&p->out);
                n = waiters_take(p->slave_waiters, wake);
                mutex_unlock(&pty_mutex);
                wake_all(wake, n);
                return (int32_t)amount;
            }
            if (!p->slave_open) {
                mutex_unlock(&pty_mutex);
                return 0;
            }
        } else {
            if (!p->master_open) {
                mutex_unlock(&pty_mutex);
                return 0;
            }
            bool canon = (p->termios.lflag & TTY_LFLAG_ICANON) != 0;
            if (canon && p->lines_ready) {
                uint32_t amount = 0;
                while (amount < count && p->in.count) {
                    uint8_t c = ring_get(&p->in);
                    out[amount++] = c;
                    if (c == '\n')
                        break;
                }
                if (amount && out[amount - 1] == '\n')
                    p->lines_ready--;
                else if (!p->in.count && p->lines_ready)
                    p->lines_ready--;
                mutex_unlock(&pty_mutex);
                return (int32_t)amount;
            }
            if (canon && p->eof_pending) {
                p->eof_pending--;
                mutex_unlock(&pty_mutex);
                return 0;
            }
            if (!canon) {
                uint32_t vmin = p->termios.cc[TTY_VMIN];
                uint32_t vtime = p->termios.cc[TTY_VTIME];
                if (p->in.count && (p->in.count >= (vmin ? vmin : 1) || vtime == 0 || deadline)) {
                    uint32_t amount = 0;
                    while (amount < count && p->in.count)
                        out[amount++] = ring_get(&p->in);
                    mutex_unlock(&pty_mutex);
                    return (int32_t)amount;
                }
                if (vmin == 0 && vtime == 0) {
                    mutex_unlock(&pty_mutex);
                    return 0;
                }
                if (vtime) {
                    uint64_t now = timer_ticks();
                    if (!deadline)
                        deadline = now + (uint64_t)vtime * 100;
                    else if (now >= deadline) {
                        uint32_t amount = 0;
                        while (amount < count && p->in.count)
                            out[amount++] = ring_get(&p->in);
                        mutex_unlock(&pty_mutex);
                        return (int32_t)amount;
                    }
                }
            }
        }
        if (nonblock) {
            mutex_unlock(&pty_mutex);
            return -KERR_AGAIN;
        }
        if (signal_pending_current()) {
            mutex_unlock(&pty_mutex);
            return -KERR_INTR;
        }
        waiters_add(master ? p->master_waiters : p->slave_waiters, self);
        bool timed = !master && deadline;
        mutex_unlock(&pty_mutex);
        if (timed)
            scheduler_sleep(10);
        else
            scheduler_block();
    }
}

int32_t pty_write(int32_t id, bool master, const void *buffer, uint32_t count, bool nonblock)
{
    (void)nonblock;
    const uint8_t *in = (const uint8_t *)buffer;
    int32_t wake[PTY_WAITERS * 2];
    uint32_t n = 0;
    uint32_t sig = 0, sig_pid = 0;
    mutex_lock(&pty_mutex);
    struct pty *p = pty_get(id);
    if (!p) {
        mutex_unlock(&pty_mutex);
        return -KERR_BADF;
    }
    if (master) {
        if (!p->slave_open) {
            mutex_unlock(&pty_mutex);
            return (int32_t)count;
        }
        for (uint32_t i = 0; i < count; i++) {
            uint32_t s = input_char(p, in[i]);
            if (s) {
                sig = s;
                sig_pid = p->fg_pid;
            }
        }
    } else {
        if (!p->master_open) {
            mutex_unlock(&pty_mutex);
            return -KERR_IO;
        }
        for (uint32_t i = 0; i < count; i++)
            out_put(p, in[i]);
    }
    p->generation++;
    n = waiters_take(p->slave_waiters, wake);
    n += waiters_take(p->master_waiters, wake + n);
    mutex_unlock(&pty_mutex);
    wake_all(wake, n);
    if (sig && sig_pid)
        (void)signal_send_kernel(sig_pid, (int32_t)sig);
    return (int32_t)count;
}

int32_t pty_poll(int32_t id, bool master)
{
    int32_t ev = 0;
    mutex_lock(&pty_mutex);
    struct pty *p = pty_get(id);
    if (!p) {
        ev = KPOLLNVAL;
    } else if (master) {
        if (p->out.count)
            ev |= KPOLLIN;
        if (!p->slave_open)
            ev |= KPOLLHUP;
        ev |= KPOLLOUT;
    } else {
        if (slave_readable(p))
            ev |= KPOLLIN;
        if (!p->master_open)
            ev |= KPOLLHUP;
        if (p->master_open && p->out.count < PTY_RING - 256)
            ev |= KPOLLOUT;
    }
    mutex_unlock(&pty_mutex);
    return ev;
}

uint64_t pty_generation(int32_t id)
{
    mutex_lock(&pty_mutex);
    struct pty *p = pty_get(id);
    uint64_t g = p ? p->generation : 0;
    mutex_unlock(&pty_mutex);
    return g;
}

int64_t pty_ioctl(int32_t id, bool master, uint32_t request, uint64_t argument)
{
    (void)master;
    void *ptr = (void *)(uintptr_t)argument;
    mutex_lock(&pty_mutex);
    struct pty *p = pty_get(id);
    if (!p) {
        mutex_unlock(&pty_mutex);
        return -KERR_BADF;
    }
    int64_t rc = 0;
    switch (request) {
    case TTY_TCGETS:
        *(struct tty_termios *)ptr = p->termios;
        break;
    case TTY_TCSETS:
    case TTY_TCSETSW:
    case TTY_TCSETSF: {
        bool was_canon = (p->termios.lflag & TTY_LFLAG_ICANON) != 0;
        p->termios = *(const struct tty_termios *)ptr;
        bool canon = (p->termios.lflag & TTY_LFLAG_ICANON) != 0;
        if (request == TTY_TCSETSF) {
            p->in.count = 0;
            p->line_len = 0;
            p->lines_ready = 0;
            p->eof_pending = 0;
        } else if (was_canon && !canon) {
            for (uint32_t i = 0; i < p->line_len; i++)
                (void)ring_put(&p->in, p->line[i]);
            p->line_len = 0;
            p->lines_ready = 0;
        }
        break;
    }
    case TTY_TIOCGWINSZ:
        *(struct tty_winsize *)ptr = p->winsize;
        break;
    case TTY_TIOCSWINSZ:
        p->winsize = *(const struct tty_winsize *)ptr;
        break;
    case TTY_FIONREAD:
        *(int32_t *)ptr = (int32_t)(master ? p->out.count : p->in.count);
        break;
    case TTY_TCFLSH:
        if (argument == 0 || argument == 2) {
            p->in.count = 0;
            p->line_len = 0;
            p->lines_ready = 0;
        }
        break;
    case TTY_TIOCSPGRP_PID:
        p->fg_pid = (uint32_t)argument;
        break;
    case TTY_RAW_ACTIVE:
        rc = (p->termios.lflag & TTY_LFLAG_ICANON) ? 0 : 1;
        break;
    default:
        rc = -KERR_NOTTY;
        break;
    }
    mutex_unlock(&pty_mutex);
    return rc;
}
