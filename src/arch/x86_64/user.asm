[BITS 64]
section .text
global arch_enter_user

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
