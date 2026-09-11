[BITS 64]
section .text
global scheduler_asm_switch

; void scheduler_asm_switch(uint64_t *old_rsp, uint64_t *new_rsp)
; rdi = old_rsp, rsi = new_rsp
scheduler_asm_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, [rsi]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
