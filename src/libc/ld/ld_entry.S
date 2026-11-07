    .text
    .globl _pc_dl_entry
    .type _pc_dl_entry, @function
_pc_dl_entry:
    xorl %ebp, %ebp
    movq %rsp, %rbx
    movq %rsp, %rdi
    andq $-16, %rsp
    call pc_dl_start
    movq %rbx, %rsp
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d
    jmp *%rax
    .size _pc_dl_entry, .-_pc_dl_entry

    .globl _pc_dl_resolve
    .hidden _pc_dl_resolve
    .type _pc_dl_resolve, @function
_pc_dl_resolve:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    subq $128, %rsp
    movups %xmm0, 0(%rsp)
    movups %xmm1, 16(%rsp)
    movups %xmm2, 32(%rsp)
    movups %xmm3, 48(%rsp)
    movups %xmm4, 64(%rsp)
    movups %xmm5, 80(%rsp)
    movups %xmm6, 96(%rsp)
    movups %xmm7, 112(%rsp)
    movq 184(%rsp), %rdi
    movq 192(%rsp), %rsi
    call pc_dl_fixup
    movq %rax, %r11
    movups 0(%rsp), %xmm0
    movups 16(%rsp), %xmm1
    movups 32(%rsp), %xmm2
    movups 48(%rsp), %xmm3
    movups 64(%rsp), %xmm4
    movups 80(%rsp), %xmm5
    movups 96(%rsp), %xmm6
    movups 112(%rsp), %xmm7
    addq $128, %rsp
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rax
    addq $16, %rsp
    jmp *%r11
    .size _pc_dl_resolve, .-_pc_dl_resolve

    .section .note.GNU-stack,"",@progbits
