/*
    GNU linker script for NRF52832 AA w/ S132 3.0.0 SoftDevice
*/
/* Specify the memory areas */
SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)
MEMORY
{
    FLASH_TEXT (rw) : ORIGIN = 0x0007f000, LENGTH = 4K       /* .text */
    FLASH_BOOT (r)  : ORIGIN = 0x10001014, LENGTH = 4        /* 4 bytes, UICR.NRFFW[0] */
    RAM (xrw)       : ORIGIN = 0x20002800, LENGTH = 0x002000 /* 8 KiB */
    RAM_DATA (xrw)  : ORIGIN = 0x20002000, LENGTH = 0        /* .data, disabled */
}

/* top end of the stack */

/*_stack_end = ORIGIN(RAM) + LENGTH(RAM);*/
_estack = ORIGIN(RAM) + LENGTH(RAM);

INCLUDE "dfu/common.ld"
