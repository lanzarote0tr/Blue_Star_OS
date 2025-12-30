ENTRY(main)

SECTIONS
{
    . = 0x200000;
    
    .entry : {
        *(.entry)
    }
    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(COMMON)
        *(.bss*)
    }
}
