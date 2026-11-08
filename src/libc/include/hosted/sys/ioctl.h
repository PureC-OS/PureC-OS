#pragma once
// PureC hosted libc: ioctl on the kernel console tty.

#ifdef __cplusplus
extern "C" {
#endif
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCFLSH 0x540B
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONREAD 0x541B

int ioctl(int fd, unsigned long request, ...);
#ifdef __cplusplus
}
#endif
