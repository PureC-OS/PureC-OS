[BITS 64]
section .text

global smp_ap_entry
extern smp_ap_main

; Limine enters with struct limine_smp_info * in RDI. extra_argument at
; offset 24 points at ap_boot_context, whose first field is stack_top.
smp_ap_entry:
    cli
    cld
    mov rsi, rdi
    test rsi, rsi
    jz .park
    mov rdi, [rsi + 24]
    test rdi, rdi
    jz .park
    mov rsp, [rdi]
    test rsp, rsp
    jz .park
    and rsp, -16
    xor rbp, rbp
    call smp_ap_main

.park:
    cli
    hlt
    jmp .park
