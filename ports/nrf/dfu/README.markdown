# Bootloader/DFU

This is a small bootloader/DFU (device firmware updater) for nRF5x chips that
can do over-the-air firmware updates. It works at least on the nRF51822 with
s110 and on nRF52832 with s132, but other chips/SoftDevices will be easy to add
or may already work. The main advantage (besides its very small size) is that
this DFU can be installed to the MBR region of the SoftDevice, not just in the
bootloader area like the official [Nordic DFU example](https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.sdk5.v14.1.0%2Fexamples_bootloader.html&cp=4_0_0_4_4).
There it won't take up any extra space as the MBR is a required component of the
system but the main thing it does is simply forwarding interrupts.

## DFU in MBR

A feature (which as far as I'm aware doesn't exist in other nRF5x bootloaders)
is that it can also be stored in the MBR region. In this mode, it receives all
interrupts but forwards all of them to the SoftDevice (except for the reset
handler). MBR SVCalls and other bootloaders are not (yet) supported when
installed this way. The advantage is that it doesn't take up any extra space as
the MBR is a required system component of recent SoftDevices.

This is the best supported configuration. It appears to work reliably and
doesn't seem to affect SoftDevice operation.

## DFU in bootloader

Originally this DFU was written as a conventional bootloader, targeting a code
size of ≤1kB (the flash page erase size of nRF51 chips). It should still work,
but hasn't been well maintained. More importantly, MicroPython hasn't been
adjusted to reserve this space for a bootloader so using it will lead to data
corruption when the internal filesystem is used or the application is very
large.

## Usage

In normal operation, it immediately jumps to MicroPython. But from MicroPython
there is a call (`machine.reset(1)`) which resets with a flag to stay in DFU
mode. It will broadcast a DFU service UUID so nearby updaters can connect and
push the update.

## Installing

The DFU is disabled by default. It can be enabled by passing a `DFU=mbr` flag to
the Makefile.

For example:

    make BOARD=foo SD=s1XX DFU=mbr

Or, to store the DFU in the bootloader:

    make BOARD=foo SD=s1XX DFU=bootloader

You can either separately flash the DFU (`build-*/dfu_mbr.hex` or
`build-*/dfu_bootloader.hex`) or flash the DFU as part of a combined
firmware.hex file (`build-*/firmware.hex`). The latter can push the while
firmware at once.

Some notes:

  * For MBR mode, if you flash the DFU manually, you have to flash it *after*
    flashing the SoftDevice as it overwrites a part of the SoftDevice. So on
    every SoftDevice update, you'll need to re-flash the DFU.
  * For bootloader mode to work, a UICR register needs to be set. This register
    is part of the .hex file and will be set at a flash so you can usually just
    ignore it. However, if you move or remove the bootloader you'll need to
    erase this register (which usually means erasing the whole flash).
  * This should be obvious, but the DFU depends on a functioning SoftDevice.
    This means, for example, that you cannot update the SoftDevice using the DFU
    (but it should be possible to work around that by writing an application
    that overwrites the SoftDevice at first run).

## Bluetooth API

It advertizes a service (`67fc0001-83ae-f58c-f84b-ba72efb822f4`) with two
characteristics: an info characteristic with various parameters (flash size,
page size, application start address, etc.) and a write/notify characteristic
for calls and return values. With that, an application can erase pages and write
new pages. Finally, it can call a reset to enter the bootloader.

Note that the protocol also includes an internal buffer as big as a flash page.
It can be written to using the buffer characteristic or the buffer command, and
is reset with the flash write command. This buffer is required as BLE does not
support writes as big as a page.

| characteristic  | description |
| --------------- | ----------- |
| info (`0002`)   | Read-only characteristic that gives basic information about the chip (flash type and size) and DFU version. See below for a description.
| call (`0003`)   | Writable characteristic to send commands. The return value of commands is sent as a notification, where the first byte indicates success (0) or failure (>0). Other bytes are undefined at the moment.
| buffer (`0004`) | Optional buffer characteristic for faster data transfers. A write will append the given number of bytes to the internal buffer. The internal buffer is reset on a write command.

Info characteristic (all integer values in little endian):

| length (in bytes) | description |
| ----------------- | ----------- |
| 1                 | DFU version (currently 1).
| 1                 | log2 of the flash page size. For example, a page size of 4096 is described as 12.
| 2                 | Number of pages of the complete flash chip. To get the flash size in bytes, calculate page size * number of pages.
| 4                 | 4 bytes describing the chip version. For example `n52a` for the nRF52 family of chips.
| 2                 | Page number of the first application page. The byte offset can be calculated by multiplying with the page size.
| 2                 | Number of pages for the application.

Calls (writes to the call characteristic) and their arguments. The first byte
(byte 0) indicates the command. The second byte (byte 1) is currently ignored
but must be set to 0. After that, more arguments follow. The format is shown in
[Python `struct.struct` format characters](https://docs.python.org/3/library/struct.html#format-characters).

| call             | length + format | description |
| ---------------- | --------------- | ----------- |
| 1: reset         | 1 (`B`)         | Immediately reset the chip. There will be no response and the connection will break. Useful after an update, to reboot into the application.
| 2: erase page    | 4 (`BBH`)       | Erase a page. The first  integer with the page to erase. It will respond with success or failure.
| 3: write page    | 4 (`BBHH`)      | Write the internal buffer to the page as indicated in the first 16-bit integer argument (`H`). The second 16-bit integer argument is the number of words to write. For 32-bit systems there are 4 bytes per word. The command will respond with success or failure.
| 4: add to buffer | 4-20 (`BBH16s`) | Optional, only allowed when there is no buffer characteristic. Add the given bytes to the internal buffer, starting with byte 4 (meaning the 3 bytes folloing the command byte are ignored). There is no response for improved performance.

A DFU tool should do an update in the following way:

 1. Erase the first page of the application, so the reset vector is cleared.
 2. Erase + program every other page of the application, the other does not
    matter.
 3. Program the first page of the application.
 4. Reset the chip.

Using this procedure, the update can survive connection losses or even more
drastic things like a power loss. The only sensitive part is programming the
last page, which is quite fast (a few 100 milliseconds at most) and does not
depend on an intact connection.

There is no verification of any kind implemented yet. Security relies on the
fact that the DFU can only be entered via a command in the running firmware or
as long as the first page of the firmware (the ISR vector) is cleared.

## Optimizations

To get to this low size, some optimizations are implemented, some of which
are somewhat dangerous.

  * Code immediately follows the ISR vector.
  * Product anomalies are ignored. If they are relevant to the bootloader, they
    can be implemented.
  * Memory regions are not enabled by default. With the initial values (after
    reset) they are already enabled. This should be safe as long as no reset
    happens while memory regions are disabled.

For bootloader mode, some other optimizations have been implemented:

  * The ISR vector is shortened to 4 words, saving a lot of space. This includes
    the initial stack pointer and the HardFault handler. Other interrupts should
    not happen, I hope.
  * There is no `.data` section on the nRF51. Instead, all global values should
    either be constant (stored in `.text` in flash) or zero (stored in `.bss`
    and initialized by the startup code). The `.bss` segment is initialized,
    however. Most of the time, avoiding gobal non-const data saves code size
    anyway.  
    For MBR mode, it cannot use data in flash as the SoftDevice won't accept
    addresses in that area (< 0x1000), so they all have to be stored in RAM.

Still, there are some more optimizations that can be done to get to a lower
size:

  * SVC functions (all `sd_` calls) are currently implemented in a separate
    function. It is possible to inline those, or at least some of them. As a
    `svc` instruction is just 2 bytes and a function call is 4 bytes + the
    function itself (another 4 bytes), inlined SVC functions are always more
    efficient. it should be possible to save about 100 bytes this way.
  * The end of the compiled `Reset_Handler` (which contains most functions,
    inlined) contains a list of linker-inserted pointers to constants in `.data`
    and `.bss`. It should be possible to avoid some of those by putting those
    objects in a single container struct so the compiler will be able to write
    more efficient LDR instructions using a single or a few base pointers. This
    may save about 50 bytes.

Note that many features can be disabled in dfu.h for a smaller DFU image.
