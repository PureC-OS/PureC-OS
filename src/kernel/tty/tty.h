#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TTY_NCCS 32

#define TTY_IFLAG_ISTRIP 0x0020u
#define TTY_IFLAG_INLCR 0x0040u
#define TTY_IFLAG_IGNCR 0x0080u
#define TTY_IFLAG_ICRNL 0x0100u
#define TTY_IFLAG_IXON 0x0400u
#define TTY_OFLAG_OPOST 0x0001u
#define TTY_OFLAG_ONLCR 0x0004u
#define TTY_CFLAG_CS8 0x0030u
#define TTY_CFLAG_CREAD 0x0080u
#define TTY_CFLAG_B38400 0x000Fu
#define TTY_LFLAG_ISIG 0x0001u
#define TTY_LFLAG_ICANON 0x0002u
#define TTY_LFLAG_ECHO 0x0008u
#define TTY_LFLAG_ECHOE 0x0010u
#define TTY_LFLAG_ECHOK 0x0020u
#define TTY_LFLAG_ECHOCTL 0x0200u
#define TTY_LFLAG_ECHOKE 0x0800u
#define TTY_LFLAG_IEXTEN 0x8000u

#define TTY_VINTR 0
#define TTY_VQUIT 1
#define TTY_VERASE 2
#define TTY_VKILL 3
#define TTY_VEOF 4
#define TTY_VTIME 5
#define TTY_VMIN 6
#define TTY_VSTART 8
#define TTY_VSTOP 9
#define TTY_VSUSP 10

#define TTY_TCGETS 0x5401u
#define TTY_TCSETS 0x5402u
#define TTY_TCSETSW 0x5403u
#define TTY_TCSETSF 0x5404u
#define TTY_TCFLSH 0x540Bu
#define TTY_TIOCGWINSZ 0x5413u
#define TTY_TIOCSWINSZ 0x5414u
#define TTY_FIONREAD 0x541Bu
#define TTY_RAW_ACTIVE 0x7F01u

struct tty_termios {
    uint32_t iflag;
    uint32_t oflag;
    uint32_t cflag;
    uint32_t lflag;
    uint8_t cc[TTY_NCCS];
    uint32_t ispeed;
    uint32_t ospeed;
};

struct tty_winsize {
    uint16_t rows;
    uint16_t columns;
    uint16_t xpixel;
    uint16_t ypixel;
};

bool tty_raw_mode(void);
bool tty_output_nl_cr(void);
void tty_get_termios(struct tty_termios *out);
void tty_set_termios(const struct tty_termios *in, bool flush, uint32_t pid);
void tty_get_winsize(struct tty_winsize *out);
void tty_set_application_cursor(bool enable);
void tty_feed_char(char character);
void tty_feed_special(uint8_t key);
void tty_feed_sequence(const char *text);
void tty_inject(const char *text);
void tty_flush_input(void);
uint32_t tty_pending(void);
bool tty_input_ready(void);
int32_t tty_read(void *buffer, uint32_t count);
void tty_process_exit(uint32_t pid);
