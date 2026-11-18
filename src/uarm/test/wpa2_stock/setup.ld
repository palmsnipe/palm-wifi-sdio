ENTRY(setupEntry)
SECTIONS
{
    .text 0x57de : {
        KEEP(*(.text.entry))
        *(.text .text.* .rodata .rodata.*)
        . = ALIGN(2);
    }
    .data : { *(.data .data.* .got .got.*) }
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    ASSERT(SIZEOF(.data) + SIZEOF(.bss) == 0, "setup shim requires position-independent code")
}
