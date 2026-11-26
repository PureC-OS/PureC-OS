[BITS 64]
section .text
global arch_syscall_entry
extern syscall_entry_c

%define PERCPU_KSTACK_TOP 24
%define PERCPU_USER_RSP 32
%define USER_CS 0x23
%define USER_DS 0x1B

arch_syscall_entry:
    swapgs
    mov [gs:PERCPU_USER_RSP], rsp
    mov rsp, [gs:PERCPU_KSTACK_TOP]
    push USER_DS
    push qword [gs:PERCPU_USER_RSP]
    push r11
    push USER_CS
    push rcx
    push 0
    push 0x80
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    mov rbx, rsp
    and rsp, -16
    cld
    call syscall_entry_c
    cli
    mov rsp, rbx
    test eax, eax
    jz .slow
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    mov rsp, [rsp + 5*8]
    swapgs
    o64 sysret
.slow:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    swapgs
    iretq
