/* Entry point */
ENTRY(_start)

/* Limine library */
PROVIDE(hhdm = 0xffff800000000000);

SECTIONS
{
    . = 0xffffffff80000000;

    .multiboot : {
        *(.multiboot)
    }

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(0x1000) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(0x1000) {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(0x1000) {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}