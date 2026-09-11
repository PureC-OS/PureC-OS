; gdt.asm - load GDT64 and reload segments
[BITS 64]
section .text
global gdt_flush

; void gdt_flush(uint64_t gdt_ptr)
gdt_flush:
    lgdt [rdi]
    ; reload CS via far return
    push 0x08
    lea rax, [rel .reload]
    push rax
    retfq
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
