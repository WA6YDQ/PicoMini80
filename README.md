Uses a Pi Pico 2 to emulate an 8080 based computer.
Supports 3 users without additional hardware, but has the
power to support up to 7.

You will need to add an SD card adapter in hardware.
You will need the Sd card drivers at:
https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/tree/main

I added a 64Mb (8 MB) PSRAM card using spi. Currently unused.
Software supports an 8 bit I/O bus with Pico hardware lines.
