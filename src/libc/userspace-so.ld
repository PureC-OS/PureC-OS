ENTRY(_start)
SECTIONS
{
    . = 0x0;
    .text : ALIGN(0x1000) { *(.text.start) *(.text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
    .eh_frame : ALIGN(0x1000) { *(.eh_frame*) }
    .got : ALIGN(0x1000) { *(.got .got.*) }
    .got.plt : ALIGN(0x1000) { *(.got.plt .got.plt.*) }
    .plt : ALIGN(0x1000) { *(.plt .plt.*) }
    .dynamic : ALIGN(0x1000) { *(.dynamic) }
    .dynsym : ALIGN(0x1000) { *(.dynsym) }
    .dynstr : ALIGN(0x1000) { *(.dynstr) }
    .hash : ALIGN(0x1000) { *(.hash) }
    .gnu.hash : ALIGN(0x1000) { *(.gnu.hash) }
    .rel.dyn : ALIGN(0x1000) { *(.rel.dyn .rel.dyn.*) }
    .rela.dyn : ALIGN(0x1000) { *(.rela.dyn .rela.dyn.*) }
    .rel.plt : ALIGN(0x1000) { *(.rel.plt .rel.plt.*) }
    .rela.plt : ALIGN(0x1000) { *(.rela.plt .rela.plt.*) }
    .data : ALIGN(0x1000) { *(.data .data.*) }
    .bss : ALIGN(0x1000) { *(COMMON) *(.bss .bss.*) }
    /DISCARD/ : { *(.comment) *(.note*) }
}
