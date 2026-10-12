ENTRY(_start)
PHDRS
{
    headers PT_PHDR FILEHDR PHDRS;
    text PT_LOAD FILEHDR PHDRS;
    data PT_LOAD;
    dynamic PT_DYNAMIC;
    stack PT_GNU_STACK FLAGS(6);
}
SECTIONS
{
    . = SIZEOF_HEADERS;
    .text : { *(.text.start) *(.text .text.*) } :text
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) } :text
    .got : ALIGN(0x1000) { *(.got .got.*) } :data
    .got.plt : ALIGN(0x1000) { *(.got.plt .got.plt.*) } :data
    .plt : ALIGN(0x1000) { *(.plt .plt.*) } :text
    .dynamic : ALIGN(0x1000) { *(.dynamic) } :data :dynamic
    .dynsym : ALIGN(0x1000) { *(.dynsym) } :data
    .dynstr : ALIGN(0x1000) { *(.dynstr) } :data
    .hash : ALIGN(0x1000) { *(.hash) } :data
    .rel.dyn : ALIGN(0x1000) { *(.rel.dyn .rel.dyn.*) } :data
    .rela.dyn : ALIGN(0x1000) { *(.rela.dyn .rela.dyn.*) } :data
    .rel.plt : ALIGN(0x1000) { *(.rel.plt .rel.plt.*) } :data
    .rela.plt : ALIGN(0x1000) { *(.rela.plt .rela.plt.*) } :data
    .data : ALIGN(0x1000) { *(.data .data.*) } :data
    .bss : ALIGN(0x1000) { *(COMMON) *(.bss .bss.*) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
