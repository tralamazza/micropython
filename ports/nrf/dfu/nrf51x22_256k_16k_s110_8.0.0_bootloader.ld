/*
    GNU linker script for NRF51822 AA w/ S110 8.0.0 SoftDevice
*/
/* Specify the memory areas */
SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)
MEMORY
{
    FLASH (rx)      : ORIGIN = 0x00000000, LENGTH = 256K     /* entire flash */
    FLASH_TEXT (rw) : ORIGIN = 0x0003FC00, LENGTH = 1K       /* .text - increase to ~4kB for debug */
    FLASH_BOOT (r)  : ORIGIN = 0x10001014, LENGTH = 4        /* 4 bytes, UICR.BOOTLOADERADDR */
    RAM (xrw)       : ORIGIN = 0x20002000, LENGTH = 0x002000 /* 8 KiB */
    RAM_DATA (xrw)  : ORIGIN = 0x20002000, LENGTH = 0        /* 0 bytes - disable .data */
}

/* top end of the stack */

/*_stack_end = ORIGIN(RAM) + LENGTH(RAM);*/
_estack = ORIGIN(RAM) + LENGTH(RAM);

INCLUDE "dfu/common.ld"
