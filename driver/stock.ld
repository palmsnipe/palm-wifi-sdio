ENTRY(stockEntry)
stockCommandResume = 0xb544;
stockReadRegister = 0xb594;
stockWriteRegister = 0xb604;
stockReadBuffer = 0xb4d0;

SECTIONS
{
    .text 0xc22c : {
        KEEP(*(.text.entry))
        *(.text .text.* .rodata .rodata.* .data.rel.ro .data.rel.ro.*)
        . = ALIGN(4);
    }
    .data : { *(.data .data.* .got .got.*) }
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab*) }
    ASSERT(SIZEOF(.data) + SIZEOF(.bss) == 0, "stock shim requires position-independent code")
}
