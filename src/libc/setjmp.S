// PureC hosted libc: setjmp/longjmp, raw machine code (no compiler
// prologue allowed — a C prologue would move rsp and (%rsp) would no
// longer be the caller's return address). Layout per hosted/setjmp.h:
// rbx,rbp,r12,r13,r14,r15,rsp,rip. GPRs only: safe under
// -mgeneral-regs-only, no FPU state involved.

    .text
    .globl setjmp
    .type setjmp, @function
setjmp:
    mov %rbx, 0*8(%rdi)
    mov %rbp, 1*8(%rdi)
    mov %r12, 2*8(%rdi)
    mov %r13, 3*8(%rdi)
    mov %r14, 4*8(%rdi)
    mov %r15, 5*8(%rdi)
    // Caller rsp (above our return address); resume jumps to the saved
    // rip with exactly this rsp, so the frame matches the setjmp call.
    lea 8(%rsp), %rax
    mov %rax, 6*8(%rdi)
    mov (%rsp), %rax
    mov %rax, 7*8(%rdi)
    xor %eax, %eax
    ret

    .globl longjmp
    .type longjmp, @function
longjmp:
    // Load rip first: restoring rsp invalidates stack-relative access.
    mov 7*8(%rdi), %rcx
    mov %esi, %eax
    test %eax, %eax
    jne 1f
    inc %eax
1:
    mov 0*8(%rdi), %rbx
    mov 1*8(%rdi), %rbp
    mov 2*8(%rdi), %r12
    mov 3*8(%rdi), %r13
    mov 4*8(%rdi), %r14
    mov 5*8(%rdi), %r15
    mov 6*8(%rdi), %rsp
    jmp *%rcx
