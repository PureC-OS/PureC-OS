[BITS 64]
section .text
global idt_load
global isr_stub_table

extern isr_handler

idt_load:
    lidt [rdi]
    ret

%macro ISR_NOERR 1
isr_stub_%1:
    push 0          ; error dummy
    push %1         ; vector
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr_stub_%1:
    push %1
    jmp isr_common
%endmacro

%assign vector 0
%rep 256
%if vector = 8 || vector = 10 || vector = 11 || vector = 12 || vector = 13 || vector = 14 || vector = 17 || vector = 21 || vector = 29 || vector = 30
ISR_ERR vector
%else
ISR_NOERR vector
%endif
%assign vector vector + 1
%endrep

section .rodata
align 8
isr_stub_table:
%assign vector 0
%rep 256
    dq isr_stub_%+vector
%assign vector vector + 1
%endrep

section .text

isr_common:
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
    mov rdi, [rsp + 15*8]      ; vector
    mov rsi, [rsp + 16*8]      ; err
    mov rdx, [rsp + 17*8]      ; rip
    mov rcx, [rsp + 18*8]      ; cs
    mov r8,  [rsp + 19*8]      ; rflags
    mov r9, rsp                 ; regs pointer (r15 at top)
    ; align stack
    sub rsp, 8
    call isr_handler
    add rsp, 8
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
    add rsp, 16 ; drop vector+err
    iretq
