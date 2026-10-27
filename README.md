Not another cp/m emulator.

Uses a Pi Pico 2 creating an actual multiuser mini computer.
Supports 3 users without additional hardware, but has the
power to support up to 8 users. This uses a superset of
the intel i8080 instruction set.

You will need to add an SD card adapter in hardware.
You will need the Sd card drivers at:
https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/tree/main


Software supports an 8 bit I/O bus with Pico hardware lines.

NOTES: 

emulator_3.c is the current program to build/use. 

ports.c are the I/O ports

routines.c contain the non-machine routines.

Floating point routines are in float.c

See the comments at the head of emulator_3.c for details.
