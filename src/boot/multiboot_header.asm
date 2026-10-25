[BITS 32]
section .multiboot
align 8
header_start:
    dd 0xe85250d6                ; magic
    dd 0                        ; architecture i386
    dd header_end - header_start ; header length
    dd -(0xe85250d6 + 0 + (header_end - header_start)) ; checksum

    ; address tag
    dw 2
    dw 0
    dd 24
    dd header_start
    dd 0x100000                  ; load_addr
    dd 0                        ; load_end
    dd 0                        ; bss_end

    ; entry tag
    dw 3
    dw 0
    dd 8
    dd multiboot_entry

    ; end tag
    dw 0
    dw 0
    dd 8
header_end:

section .text
global multiboot_entry
extern gdt_init
extern idt_init
extern serial_init
extern serial_write_string
extern fb_init_dummy
extern kernel_main_grub

multiboot_entry:
    cli
    mov esp, stack_top
    ; check multiboot magic in eax = 0x36d76289
    ; save magic/addr
    push ebx
    push eax

    ; setup paging for long mode: identity map 0-4M
    ; Используем 0x70000 чтобы не затирать BIOS память 0x0-0x1000 (критично для VirtualBox)
    mov edi, 0x70000
    mov ecx, 4096
    xor eax, eax
    rep stosd

    ; PML4 at 0x70000, PDPT at 0x71000, PD at 0x72000
    mov dword [0x70000], 0x71003      ; PML4[0] -> PDPT
    mov dword [0x71000], 0x72003      ; PDPT[0] -> PD
    ; Map 0-32M (16 x 2M) для mbi 0x1016D312 и ядра
    mov ecx, 16
    mov edi, 0x72000
    mov eax, 0x00000083
.map_loop:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map_loop

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Set CR3
    mov eax, 0x70000
    mov cr3, eax

    ; Enable EFER.LME
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable paging + PE
    mov eax, cr0
    or eax, (1 << 31) | 1
    mov cr0, eax

    ; Load GDT64
    lgdt [gdt64_ptr]

    ; Far jump to 64-bit
    jmp 0x08:long_mode

[BITS 64]
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; call grub main
    pop rdi ; multiboot magic
    pop rsi ; multiboot info addr
    ; Align stack
    and rsp, ~0xF
    call kernel_main_grub

.hang:
    cli
    hlt
    jmp .hang

section .rodata
gdt64:
    dq 0
    dq 0x00209A0000000000 ; code
    dq 0x0000920000000000 ; data
gdt64_ptr:
    dw $ - gdt64 -1
    dq gdt64

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
