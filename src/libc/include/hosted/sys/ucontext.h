#pragma once
// PureC hosted libc: x86_64 ucontext (Linux layout) for TCC's -run
// backtrace sources to compile. No kernel context delivery exists;
// these structures are never populated on PureC OS (phase 3+).

typedef long greg_t;
#define NGREG 23
#define REG_R8 0
#define REG_R9 1
#define REG_R10 2
#define REG_R11 3
#define REG_R12 4
#define REG_R13 5
#define REG_R14 6
#define REG_R15 7
#define REG_RDI 8
#define REG_RSI 9
#define REG_RBP 10
#define REG_RBX 11
#define REG_RDX 12
#define REG_RAX 13
#define REG_RCX 14
#define REG_RSP 15
#define REG_RIP 16
#define REG_EFL 17
#define REG_CSGSFS 18
#define REG_ERR 19
#define REG_TRAPNO 20
#define REG_OLDMASK 21
#define REG_CR2 22

typedef greg_t gregset_t[NGREG];

struct _fpstate {
    unsigned short cwd;
    unsigned short swd;
    unsigned short ftw;
    unsigned short fop;
    unsigned long long rip;
    unsigned long long rdp;
    unsigned int mxcsr;
    unsigned int mxcsr_mask;
    unsigned int st_space[32];
    unsigned int xmm_space[64];
    unsigned int padding[24];
};

struct mcontext_t {
    gregset_t gregs;
    struct _fpstate *fpstate;
    unsigned long long reserved[8];
};
typedef struct mcontext_t mcontext_t;

typedef struct ucontext_t {
    unsigned long uc_flags;
    struct ucontext_t *uc_link;
    unsigned long uc_stack_ss_sp;
    int uc_stack_ss_flags;
    unsigned long uc_stack_ss_size;
    mcontext_t uc_mcontext;
    unsigned long uc_sigmask;
} ucontext_t;

int getcontext(ucontext_t *context);
