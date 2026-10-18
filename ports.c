/* ports.c - hold uint8_t input(uint8_t) and void output(uint8_t, uint8_t) */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "emulator.h"

extern void write_byte_to_bus(uint8_t, uint8_t);	// in routines.c
extern uint8_t read_byte_from_bus(int8_t);			// in routines.c
extern void write_byte_to_bus(uint8_t, uint8_t);	// in routines.c
extern uint8_t read_byte_from_bus(int8_t);			// in routines.c


uint8_t input(uint8_t active_user, uint8_t port) {

	//printf("IN: port %02X\n",port);
	//return 0;
	
	/* IN port 0: non-blocking keyboard input - returns 0xfe if no data available */
    if (port == 0) { 
    	uint8_t val = 0xfe;     			// default (no data available) is 0xfe
    	
    	if (active_user == 0) {				// read from USB
        	val = getchar_timeout_us(0);	// read byte from USB serial port if avail
        }
        if (active_user == 1) { 			// read from uart 0
       		if (uart_is_readable(uart0))
        		val = uart_getc(uart0);
       	}
       	if (active_user == 2) {				// read from uart 1
       		if (uart_is_readable(uart1))
        		val = uart_getc(uart1);
       	}
       	// use the processor IO bus to talk to external devices
       	if (active_user > 2 && active_user < 8) {
       		val = read_byte_from_bus(active_user);	// user => address
		}
       	
       	// now return the char just got
       	return val;
    }
	
}









void output(uint8_t active_user, uint8_t acc, uint8_t port) {

	//printf("USER: %d  OUT: A=%02X  PORT=%02X\n",active_user, acc, port);
	// return;
	
	/* ********* OUT port 0 - console output ************** */
	if (port == 0) {
	
		if (active_user == 0) {
			printf("%c",acc); stdio_flush();
			return;
		}

		
		if (active_user == 1) {
			uart_putc(uart0, acc); 
			return;
		}
		
		if (active_user == 2) {
			uart_putc(uart1, acc);
			return;
		}
		// use the processor IO bus to talk to external devices
		if (active_user > 2 && active_user < 8) {
			write_byte_to_bus(active_user, acc);	// user => address
			return;
		}
			
		// no other users
		return;
	}
	
	
	
	
}
