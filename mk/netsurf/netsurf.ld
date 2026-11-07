ENTRY(_start)
SECTIONS
{
    . = 0x400000;
    .text : ALIGN(0x1000) { *(.text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
    .data : ALIGN(0x1000) {
        *(.data .data.*)
        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(.init_array .init_array.*))
        KEEP(*(.ctors .ctors.*))
        __init_array_end = .;
    }
    .bss : ALIGN(0x1000) { *(COMMON) *(.bss .bss.*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
