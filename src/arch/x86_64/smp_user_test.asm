[BITS 64]
section .rodata
align 16
global smp_user_elf_start
global smp_user_elf_end

; A complete tiny ELF used only by the finite boot SMP diagnostic. It executes
; CPU-bound Ring-3 code, then checks CPU placement and XMM15 across syscalls.
smp_user_elf_start:
    db 0x7f, 'ELF', 2, 1, 1, 0
    times 8 db 0
    dw 2, 62
    dd 1
    dq 0x400000, 64, 0
    dd 0
    dw 64, 56, 1, 0, 0, 0
    dd 1, 5
    dq .payload-smp_user_elf_start, 0x400000, 0
    dq .end-.payload, .end-.payload, 4096
.payload:
    mov eax, 1
    cpuid
    shr ebx, 24
    mov r12d, ebx
    mov r14, 0x123456789abcdef0
    movq xmm15, r14
    rdtsc
    shl rdx, 32
    or rax, rdx
    mov r13, rax
.busy:
    rdtsc
    shl rdx, 32
    or rax, rdx
    sub rax, r13
    cmp rax, 100000000
    jb .busy
    mov r15d, 8
.syscall:
    mov eax, 39                     ; SYS_GETPID
    int 0x80
    test rax, rax
    jle .fail
    mov eax, 1
    cpuid
    shr ebx, 24
    cmp ebx, r12d                   ; affinity restored after BSP service
    jne .fail
    movq rax, xmm15
    cmp rax, r14
    jne .fail
    dec r15d
    jnz .syscall
    xor ebx, ebx
    jmp .exit
.fail:
    mov ebx, 1
.exit:
    mov eax, 60                     ; SYS_EXIT(status in RBX)
    int 0x80
    ud2
.end:
smp_user_elf_end:
