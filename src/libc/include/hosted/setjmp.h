#pragma once
// PureC hosted libc: setjmp. Implemented in src/libc/setjmp.c with plain
// GPR save/restore (callee-saved + rsp/rip); no signal mask, no FPU.

typedef unsigned long long jmp_buf[8];

int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int value) __attribute__((noreturn));
