ENTRY(_start)
SECTIONS
{
    . = 0x400000;
    .text : ALIGN(0x1000) { *(.text .text.*) }
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) }
    .data : ALIGN(0x1000) { *(.data .data.*) }
    .tdata : {
        PROVIDE_HIDDEN(__tdata_start = .);
        *(.tdata .tdata.*)
        PROVIDE_HIDDEN(__tdata_end = .);
    }
    .tbss : {
        PROVIDE_HIDDEN(__tbss_start = .);
        *(.tbss .tbss.*)
        *(.tcommon)
        PROVIDE_HIDDEN(__tbss_end = .);
    }
    PROVIDE_HIDDEN(__tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)));
    .init_array : ALIGN(16) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) .init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    }
    .fini_array : ALIGN(16) {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) .fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }
    .bss : ALIGN(0x1000) { *(COMMON) *(.bss .bss.*) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
