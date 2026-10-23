OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : {
        *(.text*)
    }
    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata*)
    }
    . = ALIGN(0x1000);
    .data : {
        *(.data*)
    }
    . = ALIGN(0x1000);
    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(0x1000);
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
