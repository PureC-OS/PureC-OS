[BITS 64]
section .text
global ap_trampoline

extern ap_main

ap_trampoline:
    mov rcx, [rdi+24]
    mov rax, [rcx+8]
    mov cr3, rax
    mov rsp, [rcx]
    mov rdi, rcx
    call ap_main
.hang:
    cli
    hlt
    jmp .hang
