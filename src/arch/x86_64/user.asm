[BITS 64]
section .text
global arch_enter_user
global arch_enter_user_frame

; void arch_enter_user(uint64_t rip, uint64_t rsp)
arch_enter_user:
    cli
    mov ax,0x23
    mov ds,ax
    mov es,ax
    mov fs,ax
    mov gs,ax
    push 0x23
    push rsi
    pushfq
    pop rax
    or rax,0x200
    push rax
    push 0x1B
    push rdi
    iretq

; void arch_enter_user_frame(const uint64_t *frame)
arch_enter_user_frame:
    cli
    mov ax,0x23
    mov ds,ax
    mov es,ax
    mov fs,ax
    mov gs,ax
    mov rsp,rdi
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
    add rsp,16
    iretq
