#include "kernel/tty/tty.c"
#include <stdio.h>
#include <string.h>

static uint64_t mock_ticks = 1000;
static bool signal_flag;
static bool legacy_flushed;

struct arrival {
    uint64_t at;
    const char *text;
    bool fed;
};
static struct arrival arrivals[8];
static unsigned arrival_count;

uint64_t timer_ticks(void) { return mock_ticks; }
void keyboard_poll(void) {
    for (unsigned i = 0; i < arrival_count; i++) {
        if (!arrivals[i].fed && mock_ticks >= arrivals[i].at) {
            arrivals[i].fed = true;
            for (const char *p = arrivals[i].text; *p; p++)
                tty_feed_char(*p);
        }
    }
}
void keyboard_flush_legacy(void) { legacy_flushed = true; }
bool keyboard_has_key(void) { return false; }
void scheduler_sleep(uint32_t ms) { mock_ticks += ms ? ms : 1; }
bool signal_pending_current(void) { return signal_flag; }
bool gop_console_get_size(uint32_t *columns, uint32_t *rows, uint32_t *width,
                          uint32_t *height) {
    *columns = 100;
    *rows = 40;
    *width = 800;
    *height = 400;
    return true;
}

static int latency_logged;
void klogf(enum klog_level level, const char *format, ...) {
    (void)level;
    (void)format;
    latency_logged++;
}

static int failures;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                             \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void reset(void) {
    tty_process_exit(owner_pid ? owner_pid : 0);
    struct tty_termios mode;
    tty_get_termios(&mode);
    mode = default_termios;
    tty_set_termios(&mode, true, 0);
    tty_flush_input();
    arrival_count = 0;
    memset(arrivals, 0, sizeof(arrivals));
    signal_flag = false;
    legacy_flushed = false;
    mock_ticks = 1000;
}

static void make_raw(uint8_t minimum, uint8_t time_deciseconds, uint32_t pid) {
    struct tty_termios mode;
    tty_get_termios(&mode);
    mode.iflag &= ~(TTY_IFLAG_ICRNL | TTY_IFLAG_IXON | TTY_IFLAG_ISTRIP);
    mode.oflag &= ~TTY_OFLAG_OPOST;
    mode.lflag &= ~(TTY_LFLAG_ICANON | TTY_LFLAG_ECHO | TTY_LFLAG_ISIG |
                    TTY_LFLAG_IEXTEN);
    mode.cc[TTY_VMIN] = minimum;
    mode.cc[TTY_VTIME] = time_deciseconds;
    tty_set_termios(&mode, false, pid);
}

static void arrive(uint64_t delay, const char *text) {
    arrivals[arrival_count].at = mock_ticks + delay;
    arrivals[arrival_count].text = text;
    arrivals[arrival_count].fed = false;
    arrival_count++;
}

static void test_defaults(void) {
    reset();
    CHECK(!tty_raw_mode());
    CHECK(tty_output_nl_cr());
    struct tty_termios mode;
    tty_get_termios(&mode);
    CHECK(mode.lflag & TTY_LFLAG_ICANON);
    CHECK(mode.cc[TTY_VERASE] == 127);
    tty_feed_char('x');
    tty_feed_special(KEYBOARD_SPECIAL_UP);
    CHECK(tty_pending() == 0);
}

static void test_raw_switch(void) {
    reset();
    make_raw(1, 0, 7);
    CHECK(tty_raw_mode());
    CHECK(!tty_output_nl_cr());
    CHECK(!legacy_flushed);
    tty_feed_char('a');
    CHECK(tty_pending() == 1);
    struct tty_termios mode;
    tty_get_termios(&mode);
    mode = default_termios;
    tty_set_termios(&mode, false, 7);
    CHECK(!tty_raw_mode());
    CHECK(tty_pending() == 0);
    CHECK(legacy_flushed);
    CHECK(owner_pid == 0);
}

static void test_enter_and_input_flags(void) {
    reset();
    make_raw(1, 0, 7);
    tty_feed_char('a');
    tty_feed_char('\n');
    char buffer[8];
    CHECK(tty_read(buffer, sizeof(buffer)) == 2);
    CHECK(buffer[0] == 'a' && buffer[1] == '\r');
    struct tty_termios mode;
    tty_get_termios(&mode);
    mode.iflag |= TTY_IFLAG_ICRNL;
    tty_set_termios(&mode, false, 7);
    tty_feed_char('\n');
    CHECK(tty_read(buffer, sizeof(buffer)) == 1);
    CHECK(buffer[0] == '\n');
    mode.iflag = (mode.iflag & ~TTY_IFLAG_ICRNL) | TTY_IFLAG_IGNCR;
    tty_set_termios(&mode, false, 7);
    tty_feed_char('\n');
    tty_feed_char('q');
    CHECK(tty_read(buffer, sizeof(buffer)) == 1);
    CHECK(buffer[0] == 'q');
    mode.iflag = (mode.iflag & ~TTY_IFLAG_IGNCR) | TTY_IFLAG_ISTRIP;
    tty_set_termios(&mode, false, 7);
    tty_feed_char((char)0xC1);
    CHECK(tty_read(buffer, sizeof(buffer)) == 1);
    CHECK((uint8_t)buffer[0] == 0x41);
}

static void test_special_keys(void) {
    reset();
    make_raw(1, 0, 7);
    char buffer[16];
    tty_feed_special(KEYBOARD_SPECIAL_UP);
    int32_t got = tty_read(buffer, sizeof(buffer));
    CHECK(got == 3 && memcmp(buffer, "\033[A", 3) == 0);
    tty_set_application_cursor(true);
    tty_feed_special(KEYBOARD_SPECIAL_LEFT);
    got = tty_read(buffer, sizeof(buffer));
    CHECK(got == 3 && memcmp(buffer, "\033OD", 3) == 0);
    tty_feed_special(KEYBOARD_SPECIAL_PAGE_DOWN);
    got = tty_read(buffer, sizeof(buffer));
    CHECK(got == 4 && memcmp(buffer, "\033[6~", 4) == 0);
    tty_feed_special(KEYBOARD_SPECIAL_DELETE);
    got = tty_read(buffer, sizeof(buffer));
    CHECK(got == 4 && memcmp(buffer, "\033[3~", 4) == 0);
    tty_feed_sequence("\033[15~");
    got = tty_read(buffer, sizeof(buffer));
    CHECK(got == 5 && memcmp(buffer, "\033[15~", 5) == 0);
}

static void test_nonblocking(void) {
    reset();
    make_raw(0, 0, 7);
    char buffer[4];
    uint64_t before = mock_ticks;
    CHECK(tty_read(buffer, sizeof(buffer)) == 0);
    CHECK(mock_ticks == before);
    tty_feed_char('z');
    CHECK(tty_read(buffer, sizeof(buffer)) == 1);
}

static void test_blocking_minimum(void) {
    reset();
    make_raw(1, 0, 7);
    char buffer[4];
    arrive(50, "k");
    CHECK(tty_read(buffer, sizeof(buffer)) == 1);
    CHECK(latency_logged == 0);
    CHECK(buffer[0] == 'k');
    CHECK(mock_ticks >= 1050);

    make_raw(3, 0, 7);
    arrive(10, "a");
    arrive(20, "b");
    arrive(30, "c");
    CHECK(tty_read(buffer, sizeof(buffer)) == 3);
    CHECK(memcmp(buffer, "abc", 3) == 0);
}

static void test_read_timeout(void) {
    reset();
    make_raw(0, 2, 7);
    char buffer[4];
    uint64_t before = mock_ticks;
    CHECK(tty_read(buffer, sizeof(buffer)) == 0);
    CHECK(mock_ticks - before >= 200 && mock_ticks - before < 260);

    arrive(50, "x");
    before = mock_ticks;
    CHECK(tty_read(buffer, sizeof(buffer)) == 1);
    CHECK(mock_ticks - before >= 50 && mock_ticks - before < 100);
}

static void test_inter_byte_timeout(void) {
    reset();
    make_raw(4, 1, 7);
    char buffer[8];
    arrive(500, "a");
    arrive(550, "b");
    uint64_t before = mock_ticks;
    CHECK(tty_read(buffer, sizeof(buffer)) == 2);
    CHECK(memcmp(buffer, "ab", 2) == 0);
    CHECK(mock_ticks - before >= 650 && mock_ticks - before < 720);
}

static void test_signal_interrupts(void) {
    reset();
    make_raw(1, 0, 7);
    signal_flag = true;
    char buffer[4];
    CHECK(tty_read(buffer, sizeof(buffer)) == -KERR_INTR);
    tty_feed_char('a');
    CHECK(tty_read(buffer, sizeof(buffer)) == 1);
}

static void test_buffer_limit(void) {
    reset();
    make_raw(1, 0, 7);
    for (int i = 0; i < 700; i++)
        tty_feed_char('a');
    CHECK(tty_pending() == TTY_BUFFER);
    tty_flush_input();
    CHECK(tty_pending() == 0);
}

static void test_exit_restores(void) {
    reset();
    make_raw(1, 0, 42);
    tty_set_application_cursor(true);
    tty_feed_char('a');
    tty_process_exit(41);
    CHECK(tty_raw_mode());
    tty_process_exit(42);
    CHECK(!tty_raw_mode());
    CHECK(tty_output_nl_cr());
    CHECK(legacy_flushed);
    CHECK(tty_pending() == 0);
    tty_feed_special(KEYBOARD_SPECIAL_UP);
    CHECK(tty_pending() == 0);
}

static void test_reply_injection(void) {
    reset();
    tty_inject("\033[1;1R");
    CHECK(tty_pending() == 0);
    make_raw(1, 0, 7);
    tty_inject("\033[1;1R");
    CHECK(tty_pending() == 6);
}

static void test_latency_report(void) {
    reset();
    make_raw(1, 0, 7);
    latency_logged = 0;
    tty_feed_char('a');
    mock_ticks += 50;
    char buffer[4];
    CHECK(tty_read(buffer, sizeof(buffer)) == 1);
    CHECK(latency_logged == 1);
}

static void test_winsize(void) {
    struct tty_winsize size;
    tty_get_winsize(&size);
    CHECK(size.columns == 100 && size.rows == 40);
    CHECK(size.xpixel == 800 && size.ypixel == 400);
}

static void test_flush_on_flag(void) {
    reset();
    make_raw(1, 0, 7);
    tty_feed_char('a');
    struct tty_termios mode;
    tty_get_termios(&mode);
    tty_set_termios(&mode, false, 7);
    CHECK(tty_pending() == 1);
    tty_set_termios(&mode, true, 7);
    CHECK(tty_pending() == 0);
}

int main(void) {
    test_defaults();
    test_raw_switch();
    test_enter_and_input_flags();
    test_special_keys();
    test_nonblocking();
    test_blocking_minimum();
    test_read_timeout();
    test_inter_byte_timeout();
    test_signal_interrupts();
    test_buffer_limit();
    test_exit_restores();
    test_reply_injection();
    test_latency_report();
    test_winsize();
    test_flush_on_flag();
    if (failures) {
        fprintf(stderr, "tty_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("tty_test: all passed");
    return 0;
}
