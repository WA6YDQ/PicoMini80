/* emulator_2.c (C) 2026 Kurt Theis <theis.kurt@gmail.com>

	This turns a Pi Pico 2 into a multi-user 8080 computer
	system. In a RAM only system, each user gets 64K of protected
	RAM with a max of 5 users. In a RAMROM system, each user (max 8) has
	64K of protected memory, but the top 8K is shared ROM for all users and
	holds the OS.
	
	User 0 uses the USB serial port
	Users 1 and 2 use the on-board UART serial ports
	For additional users provisions will need to be made for additional
	serial ports.
	
	This uses a superset of the 8080 processor instruction set with
	additional instructions available.
	
	(C) 2026 Kurt Theis
*/




/* Determine if this is a RAM only system, or RAM/ROM */
/* RAM only: RAM=0 thru 0xFFFF, no ROM */
/* RAMROM: RAM=0 thru 0xEFFF, ROM=0xF000 thru 0xFFFF */

// select ONLY one of the following.
//#define RAMONLY
//#define RAMROM

#ifdef RAMONLY
#define MAXRAM 0xFFFF
#define MAXROM 0x0FFF
#endif

#ifdef RAMROM
#define MAXRAM 0xEFFF
#define MAXROM 0x0FFF
#endif



// avoid stdio timeout after stdio_init_all()
#define PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 0 

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "emulator.h"


// define local and external routines 
void abort_routine(void);					// abort the processor
int decode(uint8_t);						// 8080 instruction decoder
static int parity(int, int);				// calculate parity
void output(uint8_t, uint8_t, uint8_t);		// output(user, A, port) - send A to port (in ports.c)
uint8_t input(uint8_t, uint8_t);			// A = input(user, port) - read from a port (in ports.c)
char* pico_fgets(char*, int, FILE*); 		// in routines.c
int load_hexfile_to_ram(int, char *);		// in routines.c (loads a *.hex file)
int load_file_to_ram(int, uint16_t, char*); // in routines.c (loads a *.com file)
void service_interrupt(void);				// service processor interrupt

// globals

I80REGS regs[MAXUSER];

#ifdef RAMONLY
uint8_t ram[MAXUSER][MAXRAM];
#endif

#ifdef RAMROM
uint8_t ram[MAXUSER][MAXRAM];
uint8_t rom[MAXROM];
#endif

uint8_t memory[MAXUSER][MAXRAM];		// for testing

int user;		// current user number (0-MAXUSER)
bool loadrom;	// 0 = no write to ROM, 1 = OK to write to rom (loading bootloader for example)





/* ************************* */
/* ********* MAIN() ******** */
/* ************************* */

int main(void) {

	int res = 0;
	user = 0;
	uint8_t opcode;
	loadrom = 0;
	
	
	// Set Up Hardware Buttons and Lites
	
	// reset push button
	gpio_init(RESETSW);
    gpio_set_dir(RESETSW, GPIO_IN);
    gpio_pull_up(RESETSW);
    
    // abort push button
    gpio_init(ABORTSW);
    gpio_set_dir(ABORTSW, GPIO_IN);
    gpio_pull_up(ABORTSW);
    
    // halt LED
    gpio_init(HALTLED);
    gpio_set_dir(HALTLED, GPIO_OUT);
    gpio_put(HALTLED,1);		// turn ON until processor is running
    							// alerts user if USB port is not attached
    // fault LED
    gpio_init(FAULTLED);
    gpio_set_dir(FAULTLED, GPIO_OUT);
    gpio_put(FAULTLED,0);
    
    
	// set up processor to
	// allow usb serial comms
	stdio_init_all();
	while (!stdio_usb_connected()) {	// wait for user to plug in
        sleep_ms(10); 
    }
    
    // set up uart0
    uart_init(uart0, BAUD_RATE);
    gpio_set_function(UART0_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART0_RX_PIN, GPIO_FUNC_UART);

	// set up uart1
	uart_init(uart1, BAUD_RATE);
    gpio_set_function(UART1_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART1_RX_PIN, GPIO_FUNC_UART);
    
    // Turn off hardware flow control (RTS/CTS) explicitly
    // for both UARTS
    uart_set_hw_flow(uart0, false, false);
    uart_set_hw_flow(uart1, false, false);
    
    // clear/home the console
    printf("\033[2J \033[H");
	printf("\n\n\n\n### System Coming Up ###\n\n");
	printf("Serial port Setup Complete\n");
	
	// clear memory
	opcode = 0;
	for (user = 0; user < MAXUSER; user++) {
		printf("Clearing memory for User %d\n",user);
		for (int n=0; n<MAXRAM; n++) {
			memory[user][n] = opcode;
		}
	}
	
	// clear registers
	for (user = 0; user < MAXUSER; user++) {
		printf("Clearing Registers for User %d\n",user);
		regs[user].A = regs[user].B = regs[user].C = regs[user].D = regs[user].E = regs[user].H = regs[user].L = 0;
		regs[user].PC = regs[user].SP = 0;
		// non 8080 regs
		regs[user].R0 = regs[user].R1 = regs[user].R2 = regs[user].R3 = regs[user].R4 = regs[user].R5 = regs[user].R6 = regs[user].R7 = 0;
		// flags
		regs[user].CY = regs[user].AC = regs[user].Z = regs[user].P = regs[user].S = regs[user].INTE = 0;
	}
	
	// Load boot file for each user
    user = 0;
    //char bootcode[100] = {0x3e, 0xaa, 0xd6, 0x01, 0x10};	// default boot code
    /*
    for (int n=0; n<100; n++)
    	memory[user][n] = bootcode[n];
    */
    
    load_hexfile_to_ram(0,"diag.hex");	// 8080 diagnostic program
    
    
    // Bring up the processor
	user = 0;
	printf("Starting Execution Loop\n");
	gpio_put(HALTLED,0);
	
	// execution loop
	while (1) {
	
		// test RESET switch
		if (gpio_get(RESETSW)==0) {
			gpio_put(HALTLED,1);
			for (int n=0; n<MAXUSER; n++) 
				regs[n].PC = 0;
			printf("RESET Switch Pressed\n");
			printf("PC set to 00\n");
			while (gpio_get(RESETSW)==0) sleep_ms(40);
			sleep_ms(40);
			gpio_put(HALTLED,0);
			continue;
		}
		
		// test ABORT switch
		if (gpio_get(ABORTSW)==0) {
			abort_routine();
			continue;
		}
		
		// test the INT line (active low, 8080 INTE must be set)
		if ((gpio_get(INT)==0) && (regs[user].INTE==1)) {
			// handle interrupt
			regs[user].INTE = 0;		// clear flag
			while (gpio_get(INT)==0) sleep_us(2);
			sleep_us(2);
			service_interrupt();	// do some interrupt function
			continue;
		}
		
		// read memory addressed by PC, execute opcode
		opcode = memory[user][regs[user].PC];	// testing
		// opcode = read_memory(user, regs[user].PC);

		res = decode(opcode);
		if (res != 0) {
			printf("\nUser %d Bad Opcode %02X at address %04X\n",user,opcode,regs[user].PC-1); // -1 due to PC+1 in decode() loop
			abort_routine();
			continue;
		}
		
		//sleep_ms(50);
		continue;
	}	// end of instruction loop
	
	
	
}	// end of emulation routine
/*--------------------------------*/

#ifdef RAMONLY
/* read a byte from RAM memory */
uint8_t read_memory(uint8_t user, uint16_t addr) {
	if (user >= MAXUSER) {
		printf("EXCEPTION: Bad user# (%d) in read_memory()\n",user);
		return 1;
	}
	
	return ram[user][addr];

}

/* write a byte to memory */
bool write_memory(int8_t user, uint16_t addr, uint8_t byte) {
	if (user >= MAXUSER) {
		printf("EXCEPTION: Bad user# (%d) in write_memory()\n",user);
		return 1;
	}

	ram[user][addr] = byte;
	return 0;
}	
#endif


#ifdef RAMROM
/* 
	Read a byte from memory (ram/rom)
	If addr=0000 thru EFFF return byte from ram[user]
    If addr>=F000 then return a byte from rom[no user]
*/
uint8_t read_memory(uint8_t user, uint16_t addr) {
	if (user >= MAXUSER) {
		printf("EXCEPTION: Bad user# (%d) in read_memory()\n",user);
		return 1;
	}
	
	if (addr <= MAXRAM)
		return ram[user][addr];
	else
		return rom[addr];
}




/*
	Write a byte to memory 
	If addr < 0xEFFF write a byte to ram
	If addr is 0xF000 thru 0xFFFF do not
	write anything (it's rom).
*/
bool write_memory(int8_t user, uint16_t addr, uint8_t byte) {
	if (user >= MAXUSER) {
		printf("EXCEPTION: Bad user# (%d) in write_memory()\n",user);
		return 1;
	}
		
	if (addr <= MAXRAM) {
		ram[user][addr] = byte;
		return 0;
	} else if (addr > MAXRAM && loadrom == 1) {
		rom[addr] = byte;
		return 0;
	} else
		return 1;
}	
#endif	




	
	
	
	

// 8080 Instruction Decoder
int decode(uint8_t opcode) {
	
	// NOTE: The unused 8080 opcodes are used here. See AddedInstructions.txt
	
	int res = 0;
	bool FLAG = 0;
	
	switch(opcode) {
	
		// NOP
		case 0x00:
			break;
			
		// LXI B,word
		case 0x01:
			regs[user].C = memory[user][regs[user].PC + 1];
			regs[user].B = memory[user][regs[user].PC + 2];
			regs[user].PC += 2;
			break;
			
		// STAX B
		case 0x02: 
			{
			uint16_t pair = (regs[user].B << 8) | regs[user].C;
			memory[user][pair] = regs[user].A;
			break;
			}
			
		// INX B
		case 0x03: 
			{
			uint16_t pair = (regs[user].B << 8) | regs[user].C;
			pair = pair + 1;
			regs[user].B = (pair & 0xff00) >> 8;
			regs[user].C = pair & 0xff;
			break;
			}
			
		// INR B
		case 0x04:
			{
    	    // AC is set if there is a carry out of bit 3.
    	    // This happens exactly when the lower nibble is 0x0F before the increment.
    	    regs[user].AC = ((regs[user].B & 0x0F) == 0x0F) ? 1 : 0;

    	    // Perform the increment
    	    regs[user].B += 1;

    	    // Update remaining flags (leaving CY untouched)
	        regs[user].Z = (regs[user].B == 0);
	        regs[user].S = ((regs[user].B & 0x80) == 0x80);
	        regs[user].P = parity(regs[user].B, 8);
    
	       break;
	       }
			
		// DCR B
		case 0x05:
			{
    		// AC is set if there is a borrow from bit 3 into the lower nibble.
    		// This happens exactly when the lower nibble is 0x00 BEFORE the decrement.
    		regs[user].AC = ((regs[user].B & 0x0F) == 0x00) ? 1 : 0;

    		// Perform the decrement
    		regs[user].B -= 1;

    		// Update remaining flags (leaving CY untouched)
    		regs[user].Z = (regs[user].B == 0);
    		regs[user].S = ((regs[user].B & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].B, 8);
    
    		break;
		}
			
		// MVI B, byte
		case 0x06:
			regs[user].B = memory[user][regs[user].PC + 1];
			regs[user].PC += 1;		// for byte
			break;
			
		// RLC
		case 0x07: 
			{
			uint8_t x = regs[user].A;
			regs[user].A = ((x << 1) | (x & 0x80) >> 7);
			regs[user].CY = ((x & 0x80) == 0x80);
			break;
			}
			

		case 0x08: // FADD (Custom Floating Point Add: HL = BC + DE)
		{
    		// 1. Pack the 16-bit register pairs into 32-bit storage variables
    		// Assumes BC holds a full 32-bit float and DE holds a full 32-bit float,
    		// or you are decoding a 16-bit half-precision float (binary16).
    		// If you are using 32-bit floats across pairs, you might utilize your R-registers!
    
    		// Assuming BC and DE each contain a 32-bit float split across them and an index pair,
    		// OR if they hold 16-bit floats. Let's assume standard C 32-bit float type punning:
    		uint32_t raw_op1 = ((uint32_t)regs[user].B << 24) | ((uint32_t)regs[user].C << 16) | (regs[user].R0); // Example using R0 for lower 16 bits
    		uint32_t raw_op2 = ((uint32_t)regs[user].D << 24) | ((uint32_t)regs[user].E << 16) | (regs[user].R1); 

    		float val1, val2;
    		memcpy(&val1, &raw_op1, sizeof(float));
    		memcpy(&val2, &raw_op2, sizeof(float));

    		// 2. Perform native, blazing-fast C floating-point math
    		float result = val1 + val2;

    		// 3. Unpack the result back into the HL pair (and an extension register if 32-bit)
    		uint32_t raw_res;
    		memcpy(&raw_res, &result, sizeof(float));

    		regs[user].H = (raw_res >> 24) & 0xFF;
    		regs[user].L = (raw_res >> 16) & 0xFF;
    		regs[user].R2 = raw_res & 0xFFFF; // Lower 16 bits to an index register

    		// 4. Update core flags based on the float result if your design requires it
    		regs[user].Z = (result == 0.0f);
    		regs[user].S = (result < 0.0f);
    		break;
		}
			
		// DAD B
		case 0x09: 
		{
    		// Combine H:L and B:C into 32-bit integers to safely capture 16-bit overflow
    		uint32_t pairHL = ((uint32_t)regs[user].H << 8) | regs[user].L;
    		uint32_t pairBC = ((uint32_t)regs[user].B << 8) | regs[user].C;
    
    		uint32_t result = pairHL + pairBC;

    		// 16-bit carry occurs if the result exceeds 0xFFFF (bit 16 is set)
    		regs[user].CY = (result > 0xFFFF) ? 1 : 0;

    		// Write back the upper and lower bytes to the H and L registers
    		regs[user].H = (result >> 8) & 0xFF;
            regs[user].L = result & 0xFF;
    
            break;
		}
			
		// LDAX B
		case 0x0a: 
			{
			uint16_t pair = (regs[user].B << 8) | regs[user].C;
			regs[user].A = memory[user][pair];
			break;
			}
			
		// DCX B
		case 0x0b: 
			{
			uint16_t pair = (regs[user].B << 8) | regs[user].C;
			pair = pair - 1;
			regs[user].B = (pair & 0xff00) >> 8;
			regs[user].C = pair & 0xff;
			break;
			}
			
		// INR C
		case 0x0c:
		{
    	    // AC is set if there is a carry out of bit 3.
    	    // This happens exactly when the lower nibble is 0x0F before the increment.
    	    regs[user].AC = ((regs[user].C & 0x0F) == 0x0F) ? 1 : 0;

    	    // Perform the increment
    	    regs[user].C += 1;

    	    // Update remaining flags (leaving CY untouched)
	        regs[user].Z = (regs[user].C == 0);
	        regs[user].S = ((regs[user].C & 0x80) == 0x80);
	        regs[user].P = parity(regs[user].C, 8);
    
	        break;
	    }
			
		// DCR C
		case 0x0d:
		{
    		// AC is set if there is a borrow from bit 3 into the lower nibble.
    		// This happens exactly when the lower nibble is 0x00 BEFORE the decrement.
    		regs[user].AC = ((regs[user].C & 0x0F) == 0x00) ? 1 : 0;

    		// Perform the decrement
    		regs[user].C -= 1;

    		// Update remaining flags (leaving CY untouched)
    		regs[user].Z = (regs[user].C == 0);
    		regs[user].S = ((regs[user].C & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].C, 8);
    
    		break;
		}
			
		// MVI C, byte
		case 0x0e:
			regs[user].C = memory[user][regs[user].PC + 1];
			regs[user].PC += 1;
			break;
			
		// RRC
		case 0x0f: 
			{
			uint8_t x = regs[user].A;
			regs[user].A = ((x & 1) << 7 | (x >> 1));
			regs[user].CY = ((x & 1) == 1);
			break;
			}
			
		// 0x10 assigned to BREAK (Software Breakpoint)
		// halt the processor, show registers, option for commands 
		// ONLY WORKS ON CONSOLE! Stops processor for ALL users!
		// Processor continues from stop point unless otherwise changed via command line
		case 0x10:
			abort_routine();
			break;	
			
		// LXI D, word
		case 0x11:
			regs[user].E = memory[user][regs[user].PC + 1];
			regs[user].D = memory[user][regs[user].PC + 2];
			regs[user].PC += 2;
			break;
			
		// STAX D
		case 0x12:
			uint16_t pair = (regs[user].D << 8) | regs[user].E;
			memory[user][pair] = regs[user].A;
			break;
			
		// INX D
		case 0x13: 
		{
		    uint16_t pair = (regs[user].D << 8) | (regs[user].E);
            pair = pair + 1;
            regs[user].D = (pair & 0xff00) >> 8;
            regs[user].E = pair & 0xff;
            break;
	    }
			
		// INR D
		case 0x14:
		{
    	  // AC is set if there is a carry out of bit 3.
    	  // This happens exactly when the lower nibble is 0x0F before the increment.
    	  regs[user].AC = ((regs[user].D & 0x0F) == 0x0F) ? 1 : 0;

    	  // Perform the increment
    	  regs[user].D += 1;

    	  // Update remaining flags (leaving CY untouched)
	      regs[user].Z = (regs[user].D == 0);
	      regs[user].S = ((regs[user].D & 0x80) == 0x80);
	      regs[user].P = parity(regs[user].D, 8);
    
	      break;
	    }
			
		// DCR D
		case 0x15:
			{
    		// AC is set if there is a borrow from bit 3 into the lower nibble.
    		// This happens exactly when the lower nibble is 0x00 BEFORE the decrement.
    		regs[user].AC = ((regs[user].D & 0x0F) == 0x00) ? 1 : 0;

    		// Perform the decrement
    		regs[user].D -= 1;

    		// Update remaining flags (leaving CY untouched)
    		regs[user].Z = (regs[user].D == 0);
    		regs[user].S = ((regs[user].D & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].D, 8);
    
    		break;
		}
			
		// MVI D, byte
		case 0x16:
			regs[user].D = memory[user][regs[user].PC + 1];
			regs[user].PC += 1;
			break;
			
		// RAL
		case 0x17: 
			{
			uint8_t x = regs[user].A;
			regs[user].A = ((x << 1) | regs[user].CY);
			regs[user].CY = ((x & 0x80) == 0x80);
			break;
			}
			
		// 0x18 unassigned
			
		// DAD D
		case 0x19: 
			{
    		// Combine H:L and D:E into 32-bit integers to safely capture 16-bit overflow
    		uint32_t pairHL = ((uint32_t)regs[user].H << 8) | regs[user].L;
    		uint32_t pairDE = ((uint32_t)regs[user].D << 8) | regs[user].E;
    
    		uint32_t result = pairHL + pairDE;

    		// 16-bit carry occurs if the result exceeds 0xFFFF (bit 16 is set)
    		regs[user].CY = (result > 0xFFFF) ? 1 : 0;

    		// Write back the upper and lower bytes to the H and L registers
    		regs[user].H = (result >> 8) & 0xFF;
    		regs[user].L = result & 0xFF;
    
    		break;
		}
			
		// LDAX D
		case 0x1a: 
			{
			uint16_t pair = (regs[user].D << 8) | regs[user].E;
			regs[user].A = memory[user][pair];
			break;
			}
			
		// DCX D	
		case 0x1B: 
        {
            uint16_t pair = (regs[user].D << 8) | (regs[user].E);
            pair = pair - 1;
            regs[user].D = (pair & 0xff00) >> 8;
            regs[user].E = pair & 0xff;
            break;
        }	
        
        // INR E
        case 0x1C:
        {
    	    // AC is set if there is a carry out of bit 3.
    	    // This happens exactly when the lower nibble is 0x0F before the increment.
    	    regs[user].AC = ((regs[user].E & 0x0F) == 0x0F) ? 1 : 0;

    	    // Perform the increment
    	    regs[user].E += 1;

    	    // Update remaining flags (leaving CY untouched)
	        regs[user].Z = (regs[user].E == 0);
	        regs[user].S = ((regs[user].E & 0x80) == 0x80);
	        regs[user].P = parity(regs[user].E, 8);
    
	        break;
	    }
            
		// DCR E
		case 0x1D: 
            {
    		// AC is set if there is a borrow from bit 3 into the lower nibble.
    		// This happens exactly when the lower nibble is 0x00 BEFORE the decrement.
    		regs[user].AC = ((regs[user].E & 0x0F) == 0x00) ? 1 : 0;

    		// Perform the decrement
    		regs[user].E -= 1;

    		// Update remaining flags (leaving CY untouched)
    		regs[user].Z = (regs[user].E == 0);
    		regs[user].S = ((regs[user].E & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].E, 8);
    
    		break;
		}
            
        // MVI E, byte
         case 0x1E: // MVI E, byte
            regs[user].E = memory[user][regs[user].PC+1];
            regs[user].PC += 1; // for the data byte
            break;
            
        // RAR
        case 0x1F: 
        {
            uint8_t x = regs[user].A;
            regs[user].A = ((regs[user].CY << 7) | (x >> 1));
            regs[user].CY = ((x & 1) == 1);
            break;
        } 
        
        // opcode 0x20 unused
        
        // LXI H, word
        case 0x21: // LXI H, word
            regs[user].L = memory[user][regs[user].PC+1];
            regs[user].H = memory[user][regs[user].PC+2];
            regs[user].PC += 2; // Advance 2 more bytes
            break;
            
        // SHLD
        case 0x22: // SHLD adr
        {
            uint16_t adr = (memory[user][regs[user].PC+2] << 8) | (memory[user][regs[user].PC+1]);
            memory[user][adr] = regs[user].L;
            memory[user][adr+1] = regs[user].H;
            regs[user].PC += 2;
            break;
        }        	
			
		// INX H
		case 0x23: // INX H
        {
            uint16_t pair = (regs[user].H << 8) | (regs[user].L);
            pair = pair + 1;
            regs[user].H = (pair & 0xff00) >> 8;
            regs[user].L = pair & 0xff;
            break;
        }
        
        // INR H
        case 0x24: // INR H
        {
    	   // AC is set if there is a carry out of bit 3.
    	   // This happens exactly when the lower nibble is 0x0F before the increment.
    	   regs[user].AC = ((regs[user].H & 0x0F) == 0x0F) ? 1 : 0;

    	   // Perform the increment
    	   regs[user].H += 1;

    	   // Update remaining flags (leaving CY untouched)
	       regs[user].Z = (regs[user].H == 0);
	       regs[user].S = ((regs[user].H & 0x80) == 0x80);
	       regs[user].P = parity(regs[user].H, 8);
    
	       break;
	    }
		
		// DCR H
		case 0x25: // DCR H
           {
    		// AC is set if there is a borrow from bit 3 into the lower nibble.
    		// This happens exactly when the lower nibble is 0x00 BEFORE the decrement.
    		regs[user].AC = ((regs[user].H & 0x0F) == 0x00) ? 1 : 0;

    		// Perform the decrement
    		regs[user].H -= 1;

    		// Update remaining flags (leaving CY untouched)
    		regs[user].Z = (regs[user].H == 0);
    		regs[user].S = ((regs[user].H & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].H, 8);
    
    		break;
		}
		
		// MVI H, byte
		case 0x26: // MVI H, byte
            regs[user].H = memory[user][regs[user].PC+1];
            regs[user].PC += 1; // for the data byte
            break;
        
		// DAA
	    case 0x27: 
	    { 
    		uint8_t act_val = regs[user].A;
    		uint8_t correction = 0;

    		// 1. Lower nibble adjustment
    		if (((act_val & 0x0F) > 9) || (regs[user].AC == 1)) {
    		    correction |= 0x06;
    		}

    	    // 2. Upper nibble adjustment
    	    // Note: The 8080 checks the ORIGINAL upper nibble or if a carry out of 
            // the lower nibble pushes the upper nibble over 9. 
            if (((act_val >> 4) > 9) || (regs[user].CY == 1) || (((act_val >> 4) >= 9) && ((act_val & 0x0F) > 9))) {
              correction |= 0x60;
              regs[user].CY = 1; // Force Carry to 1
            } else {
              regs[user].CY = 0; // Force Carry to 0 if condition fails
            }

            // 3. Calculate new Auxiliary Carry flag before modifying A
            // (AC is set if there is a carry out of bit 3)
            if (((act_val & 0x0F) + (correction & 0x0F)) > 0x0F) {
               regs[user].AC = 1;
           } else {
               regs[user].AC = 0;
           } 

           // 4. Apply corrections and update remaining flags
           regs[user].A += correction;
           regs[user].Z = (regs[user].A == 0) ? 1 : 0;
           regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
           regs[user].P = parity(regs[user].A, 8);

           break;
        }
                   
        // opcode 0x28 is unused
        
        // DAD H
        case 0x29: // DAD H
        {
    		// Combine H:L and H:L into 32-bit integers to safely capture 16-bit overflow
    		uint32_t pairHL = ((uint32_t)regs[user].H << 8) | regs[user].L;
    		uint32_t pairXY = ((uint32_t)regs[user].H << 8) | regs[user].L;
    
    		uint32_t result = pairHL + pairXY;

    		// 16-bit carry occurs if the result exceeds 0xFFFF (bit 16 is set)
    		regs[user].CY = (result > 0xFFFF) ? 1 : 0;

    		// Write back the upper and lower bytes to the H and L registers
    		regs[user].H = (result >> 8) & 0xFF;
    		regs[user].L = result & 0xFF;
    
    		break;
		}
        
        // LHLD
        case 0x2A: //LHLD addr
        {
            uint16_t adr = (memory[user][regs[user].PC+2] << 8) | (memory[user][regs[user].PC+1]);
            regs[user].L = memory[user][adr];
            regs[user].H = memory[user][adr+1];
            regs[user].PC += 2;
            break;
        }
        
        // DCX H
        case 0x2B: // DCX H
        {
            uint16_t pair = (regs[user].H << 8) | (regs[user].L);
            pair = pair - 1;
            regs[user].H = (pair & 0xff00) >> 8;
            regs[user].L = pair & 0xff;
            break;
        }
        
        // INR L
        case 0x2C: // INR L
		{
    	   // AC is set if there is a carry out of bit 3.
    	   // This happens exactly when the lower nibble is 0x0F before the increment.
    	   regs[user].AC = ((regs[user].L & 0x0F) == 0x0F) ? 1 : 0;

    	   // Perform the increment
    	   regs[user].L += 1;

    	   // Update remaining flags (leaving CY untouched)
	       regs[user].Z = (regs[user].L == 0);
	       regs[user].S = ((regs[user].L & 0x80) == 0x80);
	       regs[user].P = parity(regs[user].L, 8);
    
	       break;
	    }
            
        // DCR L
        case 0x2D:
		{
    		// AC is set if there is a borrow from bit 3 into the lower nibble.
    		// This happens exactly when the lower nibble is 0x00 BEFORE the decrement.
    		regs[user].AC = ((regs[user].L & 0x0F) == 0x00) ? 1 : 0;

    		// Perform the decrement
    		regs[user].L -= 1;

    		// Update remaining flags (leaving CY untouched)
    		regs[user].Z = (regs[user].L == 0);
    		regs[user].S = ((regs[user].L & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].L, 8);
    
    		break;
		}
            
        // MVI L, byte
        case 0x2E: // MVI l, byte
            regs[user].L = memory[user][regs[user].PC+1];
            regs[user].PC += 1; // for the data byte
            break;
            
        // CMA 
        case 0x2F: // CMA (not)
            regs[user].A = ~regs[user].A;
            break; 
            
            
        // opcode 0x30 is unused
      
        // LXI SP, word
        case 0x31: // LXI SP, word
            regs[user].SP = (memory[user][regs[user].PC + 2] << 8) | (memory[user][regs[user].PC + 1]);
            regs[user].PC += 2; // Advance 2 bytes
            break;
            
        // STA address
        case 0x32: // STA adr
        {
            uint16_t adr = (memory[user][regs[user].PC + 2] << 8) | (memory[user][regs[user].PC + 1]);
            memory[user][adr] = regs[user].A;
            regs[user].PC += 2;
            break;
        } 
        
        // INX SP
        case 0x33: // INX SP
        {
            regs[user].SP += 1;
            break;
        }
        
		// INR M
		case 0x34: 
        {
          uint16_t offset = (regs[user].H << 8) | (regs[user].L);
           
    	  // AC is set if there is a carry out of bit 3.
    	  // This happens exactly when the lower nibble is 0x0F before the increment.
    	  regs[user].AC = ((memory[user][offset] & 0x0F) == 0x0F) ? 1 : 0;

    	  // Perform the increment
    	  memory[user][offset] += 1;

    	  // Update remaining flags (leaving CY untouched)
	      regs[user].Z = (memory[user][offset] == 0);
	      regs[user].S = ((memory[user][offset] & 0x80) == 0x80);
	      regs[user].P = parity(memory[user][offset], 8);
    
	      break;
	    }
        
		// DCR M
		case 0x35: 
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
    		// AC is set if there is a borrow from bit 3 into the lower nibble.
    		// This happens exactly when the lower nibble is 0x00 BEFORE the decrement.
    		regs[user].AC = ((memory[user][offset] & 0x0F) == 0x00) ? 1 : 0;

    		// Perform the decrement
    		memory[user][offset] -= 1;

    		// Update remaining flags (leaving CY untouched)
    		regs[user].Z = (memory[user][offset] == 0);
    		regs[user].S = ((memory[user][offset] & 0x80) == 0x80);
    		regs[user].P = parity(memory[user][offset], 8);
    
    		break;
        }
		
		// MVI M, byte
		case 0x36: // MVI M, byte
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            memory[user][offset] = memory[user][regs[user].PC + 1];
            regs[user].PC += 1; // for the data byte
            break;
        }
		
		// STC
		case 0x37:
			regs[user].CY = 1;
			break;
			
		// opcode 0x38 is unused
		
		// DAD SP
		case 0x39:
        {
    		// Combine H:L and SP into 32-bit integers to safely capture 16-bit overflow
    		uint32_t pairHL = ((uint32_t)regs[user].H << 8) | regs[user].L;
    		uint32_t pairXY = regs[user].SP;
    
    		uint32_t result = pairHL + pairXY;

    		// 16-bit carry occurs if the result exceeds 0xFFFF (bit 16 is set)
    		regs[user].CY = (result > 0xFFFF) ? 1 : 0;

    		// Write back the upper and lower bytes to the H and L registers
    		regs[user].H = (result >> 8) & 0xFF;
    		regs[user].L = result & 0xFF;
    
    		break;
        }
		
		// LDA word
		case 0x3A: 
        {
            uint16_t adr = ((memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1]);
            regs[user].A = memory[user][adr];
            regs[user].PC += 2;
            break;
        }
		
		// DCX SP
		case 0x3B:
			regs[user].SP += 1;
			break;
		
		// INR A
		case 0x3C:
        {
    	   // AC is set if there is a carry out of bit 3.
    	   // This happens exactly when the lower nibble is 0x0F before the increment.
    	   regs[user].AC = ((regs[user].A & 0x0F) == 0x0F) ? 1 : 0;

    	   // Perform the increment
    	   regs[user].A += 1;

    	   // Update remaining flags (leaving CY untouched)
	       regs[user].Z = (regs[user].A == 0);
	       regs[user].S = ((regs[user].A & 0x80) == 0x80);
	       regs[user].P = parity(regs[user].A, 8);
    
	       break;
	    }
		
		// DCR A
		case 0x3D:
        {
    		// AC is set if there is a borrow from bit 3 into the lower nibble.
    		// This happens exactly when the lower nibble is 0x00 BEFORE the decrement.
    		regs[user].AC = ((regs[user].A & 0x0F) == 0x00) ? 1 : 0;

    		// Perform the decrement
    		regs[user].A -= 1;

    		// Update remaining flags (leaving CY untouched)
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		break;
		}
		
		// MVI A, byte
		case 0x3E:
			regs[user].A = memory[user][regs[user].PC + 1];
			regs[user].PC += 1;
			break;
		
		// CMC
		case 0x3F:
			regs[user].CY = !regs[user].CY;
			break;
			
		// MOV B,B
		case 0x40:
			regs[user].B = regs[user].B;
			break;
			
		// MOV B,C
		case 0x41:
			regs[user].B = regs[user].C;
			break;
			
		// MOV B,D
		case 0x42:
			regs[user].B = regs[user].D;
			break;
			
		// MOV B,E
		case 0x43:
			regs[user].B = regs[user].E;
			break;
			
		// MOV B,H
		case 0x44:
			regs[user].B = regs[user].H;
			break;
			
		// MOV B,L
		case 0x45:
			regs[user].B = regs[user].L;
			break;
			
		// MOV B,M
		case 0x46:
			{
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].B = memory[user][offset];
            break;
        }
		
		// MOV B,A
		case 0x47:
			regs[user].B = regs[user].A;
			break;
			
		// MOV C,B
		case 0x48:
			regs[user].C = regs[user].B;
			break;
		
		// MOV C,C
		case 0x49:
			regs[user].C = regs[user].C;
			break;
			
		// MOV C,D
		case 0x4A:
			regs[user].C = regs[user].D;
			break;
			
		// MOV C,E
		case 0x4B:
			regs[user].C = regs[user].E;
			break;
			
		// MOV C,H
		case 0x4C:
			regs[user].C = regs[user].H;
			break;
			
		// MOV C,L
		case 0x4D:
			regs[user].C = regs[user].L;
			break;
			
		// MOV C,M
		case 0x4E:
			{
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].C = memory[user][offset];
            break;
        }
		
		// MOV C,A
		case 0x4F:
			regs[user].C = regs[user].A;
			break;
			
		// MOV D,B
		case 0x50:
			regs[user].D = regs[user].B;
			break;
		
		// MOV D,C
		case 0x51:
			regs[user].D = regs[user].C;
			break;
			
		// MOV D,D
		case 0x52:
			regs[user].D = regs[user].D;
			break;
			
		// MOV D,E
		case 0x53:
			regs[user].D = regs[user].E;
			break;
			
		// MOV D,H
		case 0x54:
			regs[user].D = regs[user].H;
			break;
			
		// MOV D,L
		case 0x55:
			regs[user].D = regs[user].L;
			break;
			
		// MOV D,M
		case 0x56:
			{
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].D = memory[user][offset];
            break;
        }
		
		// MOV D,A
		case 0x57:
			regs[user].D = regs[user].A;
			break;
		
		// MOV E,B
		case 0x58:
			regs[user].E = regs[user].B;
			break;
		
		// MOV E,C
		case 0x59:
			regs[user].E = regs[user].C;
			break;
			
		// MOV E,D
		case 0x5A:
			regs[user].E = regs[user].D;
			break;
			
		// MOV E,E
		case 0x5B:
			regs[user].E = regs[user].E;
			break;
			
		// MOV E,H
		case 0x5C:
			regs[user].E = regs[user].H;
			break;
			
		// MOV E,L
		case 0x5D:
			regs[user].E = regs[user].L;
			break;
			
		// MOV E,M
		case 0x5E:
			{
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].E = memory[user][offset];
            break;
        }
		
		// MOV E,A
		case 0x5F:
			regs[user].E = regs[user].A;
			break;
		
		// MOV H,B
		case 0x60:
			regs[user].H = regs[user].B;
			break;
		
		// MOV H,C
		case 0x61:
			regs[user].H = regs[user].C;
			break;
			
		// MOV H,D
		case 0x62:
			regs[user].H = regs[user].D;
			break;
			
		// MOV H,E
		case 0x63:
			regs[user].H = regs[user].E;
			break;
			
		// MOV H,H
		case 0x64:
			regs[user].H = regs[user].H;
			break;
			
		// MOV H,L
		case 0x65:
			regs[user].H = regs[user].L;
			break;
			
		// MOV H,M
		case 0x66:
			{
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].H = memory[user][offset];
            break;
        }
		
		// MOV H,A
		case 0x67:
			regs[user].H = regs[user].A;
			break;	
		
		// MOV L,B
		case 0x68:
			regs[user].L = regs[user].B;
			break;
		
		// MOV L,C
		case 0x69:
			regs[user].L = regs[user].C;
			break;
			
		// MOV L,D
		case 0x6A:
			regs[user].L = regs[user].D;
			break;
			
		// MOV L,E
		case 0x6B:
			regs[user].L = regs[user].E;
			break;
			
		// MOV L,H
		case 0x6C:
			regs[user].L = regs[user].H;
			break;
			
		// MOV L,L
		case 0x6D:
			regs[user].L = regs[user].L;
			break;
			
		// MOV L,M
		case 0x6E:
			{
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].L = memory[user][offset];
            break;
        }
		
		// MOV L,A
		case 0x6F:
			regs[user].L = regs[user].A;
			break;
		
		// MOV M,B
		case 0x70:
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            memory[user][offset] = regs[user].B;
            break;
        }
		
		// MOV M,C
		case 0x71:
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            memory[user][offset] = regs[user].C;
            break;
        }
        
        // MOV M,D
		case 0x72:
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            memory[user][offset] = regs[user].D;
            break;
        }
		
		// MOV M,E
		case 0x73:
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            memory[user][offset] = regs[user].E;
            break;
        }
        
        // MOV M,H
		case 0x74:
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            memory[user][offset] = regs[user].H;
            break;
        }
        
        // MOV M,L
		case 0x75:
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            memory[user][offset] = regs[user].L;
            break;
        }
		
		// HLT
		case 0x76:
			regs[user].PC -= 1;
			gpio_put(HALTLED,1);
			break;
		
		// MOV M,A
		case 0x77:
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            memory[user][offset] = regs[user].A;
            break;
        }
		
		// MOV A,B
		case 0x78:
			regs[user].A = regs[user].B;
			break;
		
		// MOV A,C
		case 0x79:
			regs[user].A = regs[user].C;
			break;
			
		// MOV A,D
		case 0x7A:
			regs[user].A = regs[user].D;
			break;
			
		// MOV A,E
		case 0x7B:
			regs[user].A = regs[user].E;
			break;
			
		// MOV A,H
		case 0x7C:
			regs[user].A = regs[user].H;
			break;
			
		// MOV A,L
		case 0x7D:
			regs[user].A = regs[user].L;
			break;
		
		// MOV A,M
		case 0x7E:
			{
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].A = memory[user][offset];
            break;
        	}
		
		// MOV A,A
		case 0x7F:
			regs[user].A = regs[user].A;
			break;
			
		// ADD B
		case 0x80:
        {
            // do math with higher precision so we can capture the
            // carry out
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) regs[user].B;

            // Zero flag: if the result is zero,
            // set the flag to zero
            // else clear the flag
            if ((answer & 0xff) == 0)
                regs[user].Z = 1;
            else
                regs[user].Z = 0;

            // Sign flag: if bit 7 is set,
            // set the sign flag
            // else clear the sign flag
            if (answer & 0x80)
                regs[user].S = 1;
            else
                regs[user].S = 0;

            // Carry flag
            if (answer > 0xff)
                regs[user].CY = 1;
            else
                regs[user].CY = 0;

            // Parity is handled by a subroutine
            regs[user].P = parity(answer & 0xff, 8);

            // Auxiliary Carry
            if ((regs[user].A & 0xf) + (regs[user].B & 0xf) > 0xf)
                regs[user].AC = 1;
            else
                regs[user].AC = 0;

            regs[user].A = answer & 0xff;
            break;
        }
        

        case 0x81: // ADD C
        {
            // do math with higher precision so we can capture the
            // carry out
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) regs[user].C;

            // Zero flag: if the result is zero,
            // set the flag to zero
            // else clear the flag
            if ((answer & 0xff) == 0)
                regs[user].Z = 1;
            else
                regs[user].Z = 0;

            // Sign flag: if bit 7 is set,
            // set the sign flag
            // else clear the sign flag
            if (answer & 0x80)
                regs[user].S = 1;
            else
                regs[user].S = 0;

            // Carry flag
            if (answer > 0xff)
                regs[user].CY = 1;
            else
                regs[user].CY = 0;

            // Parity is handled by a subroutine
            regs[user].P = parity(answer & 0xff, 8);

            // Auxiliary Carry
            if ((regs[user].A & 0xf) + (regs[user].C & 0xf) > 0xf)
                regs[user].AC = 1;
            else
                regs[user].AC = 0;

            regs[user].A = answer & 0xff;
            break;
        }
		
		// ADD D
		case 0x82:
        {
            // do math with higher precision so we can capture the
            // carry out
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) regs[user].D;

            // Zero flag: if the result is zero,
            // set the flag to zero
            // else clear the flag
            if ((answer & 0xff) == 0)
                regs[user].Z = 1;
            else
                regs[user].Z = 0;

            // Sign flag: if bit 7 is set,
            // set the sign flag
            // else clear the sign flag
            if (answer & 0x80)
                regs[user].S = 1;
            else
                regs[user].S = 0;

            // Carry flag
            if (answer > 0xff)
                regs[user].CY = 1;
            else
                regs[user].CY = 0;

            // Parity is handled by a subroutine
            regs[user].P = parity(answer & 0xff, 8);

            // Auxiliary Carry
            if ((regs[user].A & 0xf) + (regs[user].D & 0xf) > 0xf)
                regs[user].AC = 1;
            else
                regs[user].AC = 0;

            regs[user].A = answer & 0xff;
            break;
        }
		
		// ADD E
		case 0x83:
        {
            // do math with higher precision so we can capture the
            // carry out
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) regs[user].E;

            // Zero flag: if the result is zero,
            // set the flag to zero
            // else clear the flag
            if ((answer & 0xff) == 0)
                regs[user].Z = 1;
            else
                regs[user].Z = 0;

            // Sign flag: if bit 7 is set,
            // set the sign flag
            // else clear the sign flag
            if (answer & 0x80)
                regs[user].S = 1;
            else
                regs[user].S = 0;

            // Carry flag
            if (answer > 0xff)
                regs[user].CY = 1;
            else
                regs[user].CY = 0;

            // Parity is handled by a subroutine
            regs[user].P = parity(answer & 0xff, 8);

            // Auxiliary Carry
            if ((regs[user].A & 0xf) + (regs[user].E & 0xf) > 0xf)
                regs[user].AC = 1;
            else
                regs[user].AC = 0;

            regs[user].A = answer & 0xff;
            break;
        }
		
		// ADD H
		case 0x84:
        {
            // do math with higher precision so we can capture the
            // carry out
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) regs[user].H;

            // Zero flag: if the result is zero,
            // set the flag to zero
            // else clear the flag
            if ((answer & 0xff) == 0)
                regs[user].Z = 1;
            else
                regs[user].Z = 0;

            // Sign flag: if bit 7 is set,
            // set the sign flag
            // else clear the sign flag
            if (answer & 0x80)
                regs[user].S = 1;
            else
                regs[user].S = 0;

            // Carry flag
            if (answer > 0xff)
                regs[user].CY = 1;
            else
                regs[user].CY = 0;

            // Parity is handled by a subroutine
            regs[user].P = parity(answer & 0xff, 8);

            // Auxiliary Carry
            if ((regs[user].A & 0xf) + (regs[user].H & 0xf) > 0xf)
                regs[user].AC = 1;
            else
                regs[user].AC = 0;

            regs[user].A = answer & 0xff;
            break;
        }
		
		// ADD L
		case 0x85:
        {
            // do math with higher precision so we can capture the
            // carry out
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) regs[user].L;

            // Zero flag: if the result is zero,
            // set the flag to zero
            // else clear the flag
            if ((answer & 0xff) == 0)
                regs[user].Z = 1;
            else
                regs[user].Z = 0;

            // Sign flag: if bit 7 is set,
            // set the sign flag
            // else clear the sign flag
            if (answer & 0x80)
                regs[user].S = 1;
            else
                regs[user].S = 0;

            // Carry flag
            if (answer > 0xff)
                regs[user].CY = 1;
            else
                regs[user].CY = 0;

            // Parity is handled by a subroutine
            regs[user].P = parity(answer & 0xff, 8);

            // Auxiliary Carry
            if ((regs[user].A & 0xf) + (regs[user].L & 0xf) > 0xf)
                regs[user].AC = 1;
            else
                regs[user].AC = 0;

            regs[user].A = answer & 0xff;
            break;
        }
		
		// ADD M
		case 0x86:
       	{
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);

            // do math with higher precision so we can capture the
            // carry out
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) memory[user][offset];

            // Zero flag: if the result is zero,
            // set the flag to zero
            // else clear the flag
            if ((answer & 0xff) == 0)
                regs[user].Z = 1;
            else
                regs[user].Z = 0;

            // Sign flag: if bit 7 is set,
            // set the sign flag
            // else clear the sign flag
            if (answer & 0x80)
                regs[user].S = 1;
            else
                regs[user].S = 0;

            // Carry flag
            if (answer > 0xff)
                regs[user].CY = 1;
            else
                regs[user].CY = 0;

            // Parity is handled by a subroutine
            regs[user].P = parity(answer & 0xff, 8);

            // Auxiliary Carry
            if ((regs[user].A & 0xf) + (memory[user][offset] & 0xf) > 0xf)
                regs[user].AC = 1;
            else
                regs[user].AC = 0;

            regs[user].A = answer & 0xff;
            break;
        }
		
		// ADD A
		case 0x87:
        {
            // do math with higher precision so we can capture the
            // carry out
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) regs[user].A;

            // Zero flag: if the result is zero,
            // set the flag to zero
            // else clear the flag
            if ((answer & 0xff) == 0)
                regs[user].Z = 1;
            else
                regs[user].Z = 0;

            // Sign flag: if bit 7 is set,
            // set the sign flag
            // else clear the sign flag
            if (answer & 0x80)
                regs[user].S = 1;
            else
                regs[user].S = 0;

            // Carry flag
            if (answer > 0xff)
                regs[user].CY = 1;
            else
                regs[user].CY = 0;

            // Parity is handled by a subroutine
            regs[user].P = parity(answer & 0xff, 8);

            // Auxiliary Carry
            if ((regs[user].A & 0xf) + (regs[user].A & 0xf) > 0xf)
                regs[user].AC = 1;
            else
                regs[user].AC = 0;

            regs[user].A = answer & 0xff;
            break;
        }
		
		// ADC B
		case 0x88:
        {
    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)regs[user].B + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (regs[user].B & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		break;
		}
		
		// ADC C
		case 0x89: 
        {
    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)regs[user].C + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (regs[user].C & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		break;
		}
        
        // ADC D
        case 0x8A: 
        {
    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)regs[user].D + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (regs[user].D & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		break;
		}
        
        // ADC E
        case 0x8B: // ADC E
        {
    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)regs[user].E + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (regs[user].E & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		break;
		}
        
		// ADC H
		case 0x8C:
        {
    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)regs[user].H + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (regs[user].H & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		break;
		}
        
        // ADC L
        case 0x8D: // ADC L
        {
    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)regs[user].L + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (regs[user].L & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		break;
		}
		
		// ADC M
		case 0x8E: // ADC M
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);

    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)memory[user][offset] + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (memory[user][offset] & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		break;
        }
		
		// ADC A
		case 0x8F: // ADC A
        {
    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)regs[user].A + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (regs[user].A & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		break;
		}
		

        case 0x90: // SUB B
		{
    		uint8_t a = regs[user].A;
    		uint8_t b = regs[user].B;
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    	    break;
	  	}
        
		// SUB C
		case 0x91: 
		{
    		uint8_t a = regs[user].A;
    		uint8_t b = regs[user].C;
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    	    break;
	  	}

        
        // SUB D
        case 0x92:
		{
    		uint8_t a = regs[user].A;
    		uint8_t b = regs[user].D;
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    	    break;
	  }
        
        // SUB E
        case 0x93:
		{
    		uint8_t a = regs[user].A;
    		uint8_t b = regs[user].E;
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    	    break;
	  }
        
        // SUB H
        case 0x94:
		{
    		uint8_t a = regs[user].A;
    		uint8_t b = regs[user].H;
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    	    break;
	  }
        
        // SUB L
        case 0x95: 
		{
    		uint8_t a = regs[user].A;
    		uint8_t b = regs[user].L;
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    	    break;
	  }
        
        // SUB M  
        case 0x96:
        {
    		uint8_t a = regs[user].A;
    		uint16_t offset = (regs[user].H << 8) | (regs[user].L);
    		uint8_t b = memory[user][offset];
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    	    break;
	  }
        
        // SUB A
        case 0x97: 
		{
    		uint8_t a = regs[user].A;
    		uint8_t b = regs[user].A;
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    	    break;
	  }
        
 
        case 0x98: // SBB B
		{
    		uint32_t a = regs[user].A;
    		uint32_t b = regs[user].B;
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);
	
    		break;
		}
        
        // SBB C
        case 0x99:
		{
    		uint32_t a = regs[user].A;
    		uint32_t b = regs[user].C;
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);

    		break;
		}
        
        // SBB D
       case 0x9A:
		{
    		uint32_t a = regs[user].A;
    		uint32_t b = regs[user].D;
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);

    		break;
		}
        
        // SBB E
        case 0x9B:
		{
    		uint32_t a = regs[user].A;
    		uint32_t b = regs[user].E;
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);

    		break;
		}
        
        // SBB H
        case 0x9C:
		{
    		uint32_t a = regs[user].A;
    		uint32_t b = regs[user].H;
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);

    		break;
		}
        
        // SBB L
        case 0x9D:
		{
    		uint32_t a = regs[user].A;
    		uint32_t b = regs[user].L;
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);

    		break;
		}
        
        // SBB M   
        case 0x9E:
		{
    		uint32_t a = regs[user].A;
    		uint16_t offset = (regs[user].H << 8) | (regs[user].L);
    		uint32_t b = memory[user][offset];
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);

    		break;
		}
        
        // SBB A
        case 0x9F:
		{
    		uint32_t a = regs[user].A;
    		uint32_t b = regs[user].A;
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);

    		break;
		}
        
        // ANA B
        case 0xA0:
		{
    		uint8_t original_a = regs[user].A;
    		uint8_t b = regs[user].B;

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    
    		break;
		}
            
       // ANA C
       case 0xA1:
		{
    		uint8_t original_a = regs[user].A;
    		uint8_t b = regs[user].C;

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    
    		break;
		}
        
        // ANA D
        case 0xA2:
		{
    		uint8_t original_a = regs[user].A;
    		uint8_t b = regs[user].D;

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    
    		break;
		}
        
        // ANA E
        case 0xA3:
		{
    		uint8_t original_a = regs[user].A;
    		uint8_t b = regs[user].E;

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    
    		break;
		}
        
        // ANA H
        case 0xA4:
		{
    		uint8_t original_a = regs[user].A;
    		uint8_t b = regs[user].H;

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    
    		break;
		}
        
        // ANA L
        case 0xA5: 
		{
    		uint8_t original_a = regs[user].A;
    		uint8_t b = regs[user].L;

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    
    		break;
		}
        
        // ANA M
        case 0xA6:
		{
    		uint8_t original_a = regs[user].A;
    		uint16_t offset = (regs[user].H << 8) | (regs[user].L);
    		uint8_t b = memory[user][offset];

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    
    		break;
		}
        
        // ANA A
        case 0xA7:
		{
    		uint8_t original_a = regs[user].A;
    		uint8_t b = regs[user].A;

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    
    		break;
		}
        
        // XRA B
        case 0xA8: // XRA B
            regs[user].A = regs[user].A ^ regs[user].B;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
            
        // XRA C
        case 0xA9: 
            regs[user].A = regs[user].A ^ regs[user].C;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
            
            
       	// XRA D
       	case 0xAA:
            regs[user].A = regs[user].A ^ regs[user].D;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
            
      	// XRA E
      	case 0xAB:
            regs[user].A = regs[user].A ^ regs[user].E;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
            
      	// XRA H
      	case 0xAC:
            regs[user].A = regs[user].A ^ regs[user].H;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;  
        
        // XRA L
        case 0xAD: 
            regs[user].A = regs[user].A ^ regs[user].L;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
        
        // XRA M
        case 0xAE: // XRA M
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].A = regs[user].A ^ memory[user][offset];
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
        }
        
        // XRA A
        case 0xAF:		// A^A = 0 
            regs[user].A = 0;	// regs[user].A = regs[user].A ^ regs[user].A;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
            
        // ORA B
        case 0xB0: 
            regs[user].A = regs[user].A | regs[user].B;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
                
       	// ORA C
        case 0xB1:
            regs[user].A = regs[user].A | regs[user].C;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
        
        // ORA D
        case 0xB2:
            regs[user].A = regs[user].A | regs[user].D;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
            
       	// ORA E
        case 0xB3:
            regs[user].A = regs[user].A | regs[user].E;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;     
            
		// ORA H
        case 0xB4:
            regs[user].A = regs[user].A | regs[user].H;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
		
		// ORA L
        case 0xB5:
            regs[user].A = regs[user].A | regs[user].L;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
		
		// ORA M
		case 0xB6: 
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);
            regs[user].A = regs[user].A | memory[user][offset];
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
        }
        
        // ORA A
        case 0xB7:
            regs[user].A = regs[user].A | regs[user].A;
            regs[user].Z = (regs[user].A == 0);
            regs[user].S = ((regs[user].A & 0x80) == 0x80);
            regs[user].P = parity(regs[user].A, 8);
            regs[user].CY = 0; // clears CY
            regs[user].AC = 0; // clears AC
            break;
		
		// CMP B
		case 0xB8:
        {
    		// Like SUB B but without storing the final result in A
    		uint16_t res = (uint16_t)regs[user].A - (uint16_t)regs[user].B;
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < regs[user].B);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (regs[user].B & 0xf));
    
    		break;
		}
		
		// CMP C
		case 0xB9:
        {
    		uint16_t res = (uint16_t)regs[user].A - (uint16_t)regs[user].C;
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < regs[user].C);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (regs[user].C & 0xf));
    
    		break;
		}
        
		// CMP D
		case 0xBA:
        {
    		uint16_t res = (uint16_t)regs[user].A - (uint16_t)regs[user].D;
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < regs[user].D);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (regs[user].D & 0xf));
    
    		break;
		}
        
		// CMP E
		case 0xBB:
        {
    		uint16_t res = (uint16_t)regs[user].A - (uint16_t)regs[user].E;
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < regs[user].E);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (regs[user].E & 0xf));
    
    		break;
		}
        
		// CMP H
		case 0xBC:
        {
    		uint16_t res = (uint16_t)regs[user].A - (uint16_t)regs[user].H;
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < regs[user].H);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (regs[user].H & 0xf));
    
    		break;
		}
		
		// CMP L
		case 0xBD:
        {
    		uint16_t res = (uint16_t)regs[user].A - (uint16_t)regs[user].L;
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < regs[user].L);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (regs[user].L & 0xf));
    
    		break;
		}
        
        // CMP M
        case 0xBE: 
        {
            uint16_t offset = (regs[user].H << 8) | (regs[user].L);

    		uint16_t res = (uint16_t)regs[user].A - (uint16_t)memory[user][offset];
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < memory[user][offset]);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (memory[user][offset]& 0xf));
    
    		break;
        }
        
        // CMP A
        case 0xBF:
        {
    		uint16_t res = (uint16_t)regs[user].A - (uint16_t)regs[user].A;
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < regs[user].A);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (regs[user].A & 0xf));
    
    		break;
		}
        
        // RNZ
        case 0xC0: // RNZ
            if (regs[user].Z == 0)
            {
                regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP+1] << 8);
                regs[user].SP += 2;
                FLAG = 1;
            }
            
            break;
       
       // POP B 
       case 0xC1:
            regs[user].C = memory[user][regs[user].SP];
            regs[user].B = memory[user][regs[user].SP+1];
            regs[user].SP += 2;
            break;
            
       // JNZ word
       case 0xC2:
            if (regs[user].Z == 0) {
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
              
            } else
                // branch not taken
                regs[user].PC += 2;
                
            break;      
        
		// JMP word
		case 0xC3:
			regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
			FLAG = 1;
			break;
		
		// CNZ address	
		case 0xC4: 
        {
            if (regs[user].Z == 0)
            {
                uint16_t ret = regs[user].PC + 2;
                memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
                memory[user][regs[user].SP - 2] = (ret & 0xff);
                regs[user].SP = regs[user].SP - 2;
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            }
            else
                regs[user].PC += 2;

            break;
        }
		
		// PUSH B
		case 0xC5: 
            memory[user][regs[user].SP - 1] = regs[user].B;
            memory[user][regs[user].SP - 2] = regs[user].C;
            regs[user].SP = regs[user].SP - 2;
            break;
		
		// ADI byte
		case 0xC6: // ADI byte
        {
            uint16_t answer = (uint16_t) regs[user].A + (uint16_t) memory[user][regs[user].PC + 1];
            regs[user].Z = ((answer & 0xff) == 0);
            regs[user].S = ((answer & 0x80) == 0x80);
            regs[user].CY = (answer > 0xff);
            regs[user].P = parity(answer & 0xff, 8);
            regs[user].AC = (((regs[user].A & 0xf) + (memory[user][regs[user].PC + 1] & 0xf)) > 0xf);
            regs[user].A = answer & 0xff;
            regs[user].PC += 1;	// for byte 
            break;
        }
		
		// RST 0
		case 0xC7:	// call to 0x0000
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = 0x0;
            
            FLAG = 1;
            break;
        }
        
		// RZ
		case 0xC8:
            if (regs[user].Z == 1)
            {
                regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP + 1] << 8);
                regs[user].SP += 2;
                FLAG = 1;
            }
            
            break;
            
    	// RET
    	case 0xC9:
            regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP + 1] << 8);
            regs[user].SP += 2;
            
            FLAG = 1;
            break;
            
		// JZ word
		case 0xCA: 
            if (regs[user].Z == 1) {
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            } else
                // branch not taken
                regs[user].PC += 2;

            break;
            
        // 0xCB ***** opcode unused *****
        
        // CZ word
        case 0xCC: 
        {
            if (regs[user].Z == 1)
            {
                uint16_t ret = regs[user].PC + 2;
                memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
                memory[user][regs[user].SP - 2] = (ret & 0xff);
                regs[user].SP = regs[user].SP - 2;
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            }
            else
                regs[user].PC += 2;

            break;
        }
            
        // CALL word
        case 0xCD: 
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
            
            FLAG = 1;
            break;
        }    
            
		// ACI byte
		case 0xCE:
        {
            uint16_t value = (memory[user][regs[user].PC + 1]);

    		// 1. Capture the initial incoming carry flag immediately
    		uint8_t incoming_cy = regs[user].CY;

    		// 2. Perform math with higher precision
    		uint16_t answer = (uint16_t)regs[user].A + (uint16_t)value + incoming_cy;

    		// Zero flag
    		if ((answer & 0xff) == 0)
        		regs[user].Z = 1;
    		else
        		regs[user].Z = 0;

    		// Sign flag
    		if (answer & 0x80)
        		regs[user].S = 1;
    		else
        		regs[user].S = 0;

    		// Auxiliary Carry (Using the captured incoming_cy inside the sum)
    		if (((regs[user].A & 0xf) + (value & 0xf) + incoming_cy) > 0xf)
        		regs[user].AC = 1;
    		else
        		regs[user].AC = 0;

    		// Carry flag
    		if (answer > 0xff)
        		regs[user].CY = 1;
    		else
    		    regs[user].CY = 0;

    		// Parity flag
    		regs[user].P = parity(answer & 0xff, 8);

    		// Update accumulator
    		regs[user].A = answer & 0xff;
    		
    		regs[user].PC += 1;	// byte offset
    		break;
        }
		
		// RST 1
		case 0xCF:
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = 0x8;
            
            FLAG = 1;
            break;
        }
        
        // RNC
        case 0xD0:
            if (regs[user].CY == 0)
            {
                regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP + 1] << 8);
                regs[user].SP += 2;
                FLAG = 1;
            }

            break;
        
        // POP D
        case 0xD1:
            regs[user].E = memory[user][regs[user].SP];
            regs[user].D = memory[user][regs[user].SP + 1];
            regs[user].SP += 2;
            break;
		
		// JNC word
		case 0xD2:
            if (regs[user].CY == 0) {
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            } else
                // branch not taken
                regs[user].PC += 2;

            break;
       
       // OUT byte
       case 0xD3:
       		output(user, regs[user].A, memory[user][regs[user].PC + 1]);	// output A, port#
       		regs[user].PC += 1;		// for byte
       		break;
       		
       	// CNC word
       	case 0xD4: // CNC address
       	{
            if (regs[user].CY == 0)
            {
                uint16_t ret = regs[user].PC + 2;
                memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
                memory[user][regs[user].SP - 2] = (ret & 0xff);
                regs[user].SP = regs[user].SP - 2;
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            }
            else
                regs[user].PC += 2;

            break;
       	}
       
        // PUSH D
        case 0xD5:
            memory[user][regs[user].SP - 1] = regs[user].D;
            memory[user][regs[user].SP - 2] = regs[user].E;
            regs[user].SP = regs[user].SP - 2;
            break;
            
        // SUI byte
        case 0xD6: // SUI byte
        {
    		uint8_t a = regs[user].A;
    		uint8_t b = memory[user][regs[user].PC + 1];
    		uint16_t answer = (uint16_t)a - (uint16_t)b;

    		// 1. Carry Flag (CY) is a Borrow flag for subtraction
    		regs[user].CY = (a < b) ? 1 : 0;

    		// 2. Auxiliary Carry (AC) is a Borrow from bit 3
    		regs[user].AC = ((a & 0x0F) < (b & 0x0F)) ? 1 : 0;

    		// 3. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 4. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    	    regs[user].P = parity(regs[user].A, 8);
    
    		regs[user].PC += 1;	// offset byte
    	    break;
	  }   
            
        // RST 2
        case 0xD7: // RST 2
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = 0x10;
            
            FLAG = 1;
            break;
        }
            
		// RC
		case 0xD8: // RC
            if (regs[user].CY == 1)
            {
                regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP + 1] << 8);
                regs[user].SP += 2;
                FLAG = 1;
            }

            break;
		
		// JC word
		case 0xDA: // JC address
            if (regs[user].CY == 1) {
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            } else
                // branch not taken
                regs[user].PC += 2;

            break;
		
		// IN byte
		case 0xDB:
		{
			uint8_t port = memory[user][regs[user].PC + 1];
			regs[user].A = input(user, port);
			regs[user].PC += 1;		// for byte
			break;
		}
		
		// CC word
		case 0xDC: // CC address
        {
            if (regs[user].CY == 1)
            {
                uint16_t ret = regs[user].PC + 2;
                memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
                memory[user][regs[user].SP - 2] = (ret & 0xff);
                regs[user].SP = regs[user].SP - 2;
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            }
            else
                regs[user].PC += 2;

            break;
        }
		
		// SCI byte
		case 0xDE: // SCI byte
        {
    		uint32_t a = regs[user].A;
    		uint32_t b = memory[user][regs[user].PC + 1];
    		uint32_t cy = regs[user].CY; // Incoming carry/borrow flag (0 or 1)
    
    		// 1. Perform full 32-bit subtraction to safely track underflows
    		uint32_t answer = a - b - cy;

    		// 2. Auxiliary Carry (AC) checks if a borrow was needed out of bit 3.
    		// We check if the lower nibble of 'a' is less than the lower nibble of 'b' plus the carry bit.
    		regs[user].AC = ((a & 0x0F) < ((b & 0x0F) + cy)) ? 1 : 0;

    		// 3. Carry Flag (CY) is set if the overall result underflowed 8 bits
    		regs[user].CY = (answer > 0xFF) ? 1 : 0; 
    		// Alternative check: regs[user].CY = (a < (b + cy)) ? 1 : 0; (watch out for b+cy overflow if b=255 and cy=1!)

    		// 4. Update the Accumulator
    		regs[user].A = answer & 0xFF;

    		// 5. Update standard flags
    		regs[user].Z = (regs[user].A == 0) ? 1 : 0;
    		regs[user].S = ((regs[user].A & 0x80) != 0) ? 1 : 0;
    		regs[user].P = parity(regs[user].A, 8);

			regs[user].PC += 1;	// byte offset
    		break;
		}
		
		// RST 3
		case 0xDF:
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].S - 1] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = 0x18;
            
            FLAG = 1;
            break;
        }
        
        // RPO
        case 0xE0: 
            if (regs[user].P == 0)
            {
                regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP + 1] << 8);
                regs[user].SP += 2;
                FLAG = 1;
            }

            break;
            
        // POP H
        case 0xE1:
            regs[user].L = memory[user][regs[user].SP];
            regs[user].H = memory[user][regs[user].SP + 1];
            regs[user].SP += 2;
            break;
            
        // JPO word
        case 0xE2: 
            if (regs[user].P == 0) {
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            } else
                // branch not taken
                regs[user].PC += 2;

            break;
            
        // XTHL
        case 0xE3: 
        {
            uint8_t swap = regs[user].L;
            regs[user].L = memory[user][regs[user].SP];
            memory[user][regs[user].SP] = swap;

            swap = regs[user].H;
            regs[user].H = memory[user][regs[user].SP + 1];
            memory[user][regs[user].SP + 1] = swap;
            break;
        }        
		
		// CPO word
		case 0xE4:
        {
            if (regs[user].P == 0)
            {
                uint16_t ret = regs[user].PC + 2;
                memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
                memory[user][regs[user].SP - 2] = (ret & 0xff);
                regs[user].SP = regs[user].SP - 2;
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            }
            else
                regs[user].PC += 2;

            break;
        }
		
		// PUSH H
		case 0xE5: // PUSH H
            memory[user][regs[user].SP - 1] = regs[user].H;
            memory[user][regs[user].SP - 2] = regs[user].L;
            regs[user].SP = regs[user].SP - 2;
            break;
		
		// ANI byte
		case 0xE6: // ANI byte
        {
    		uint8_t original_a = regs[user].A;
    		uint8_t b = memory[user][regs[user].PC + 1];

    		regs[user].A = original_a & b;
    
    		// Update standard flags
    		regs[user].Z = (regs[user].A == 0);
    		regs[user].S = ((regs[user].A & 0x80) == 0x80);
    		regs[user].P = parity(regs[user].A, 8);
    
    		// Clear Carry flag
    		regs[user].CY = 0; 
    
    		// Quirky 8080 behavior: AC is the logical OR of bit 3 of both original operands
    		regs[user].AC = (((original_a | b) & 0x08) != 0) ? 1 : 0;
    		regs[user].PC += 1;	// for byte
    		break;
		}
        
        // RST 4
        case 0xE7: // RST 4
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = 0x20;
            
            FLAG = 1;
            break;
        }
        
        // RPE
        case 0xE8: // RPE
            if (regs[user].P == 1)
            {
                regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP + 1] << 8);
                regs[user].SP += 2;
                FLAG = 1;
            }

            break;
            
        // PCHL
        case 0xE9: // PCHL
            regs[user].PC = (regs[user].H << 8) | (regs[user].L); 
            FLAG = 1;
            break;
            
        // JPE word
        case 0xEA: // JPE address
            if (regs[user].P == 1) {
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            } else
                // branch not taken
                regs[user].PC += 2;
            
            break;    
            
        // XCHG
        case 0xEB: // XCHG
        {
            uint8_t swap = regs[user].L;
            regs[user].L = regs[user].E;
            regs[user].E = swap;

            swap = regs[user].H;
            regs[user].H = regs[user].E;
            regs[user].D = swap;
            break;
        }
        
        // CPE word
        case 0xEC: // CPE address
        {
            if (regs[user].P == 1)
            {
                uint16_t ret = regs[user].PC + 2;
                memory[user][regs[user].SP-1] = (ret >> 8) & 0xff;
                memory[user][regs[user].SP-2] = (ret & 0xff);
                regs[user].SP = regs[user].SP - 2;
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            }
            else
                regs[user].PC += 2;

            break;
        }
        
        // XRI byte

        case 0xEE:
        {
            uint8_t x = regs[user].A ^ memory[user][regs[user].PC + 1];
            regs[user].Z = (x == 0);
            regs[user].S = ((x & 0x80) == 0x80);
            regs[user].P = parity(x, 8);
            regs[user].CY = 0; // Data book says op clears carry flags
            regs[user].AC = 0;
            regs[user].A = x;
            
            regs[user].PC++; // for byte
            break;
        }

        // RST 5
        case 0xEF: // RST 5
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].SP- 1 ] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = 0x28;
            
            FLAG = 1;
            break;
        }
        
        // RP
        case 0xF0: // RP
            if (regs[user].S == 0)
            {
                regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP + 1] << 8);
                regs[user].SP += 2;
                FLAG = 1;
            }

            break;
        
        // POP PSW
        case 0xF1: // POP PSW
        {
    		// Fetch the packed status byte from the stack
    		uint8_t psw = memory[user][regs[user].SP];
    
    		// Unpack bits straight into bool fields
    		regs[user].CY = (psw & 0x01) != 0; // Bit 0
    		regs[user].P  = (psw & 0x04) != 0; // Bit 2
    		regs[user].AC = (psw & 0x10) != 0; // Bit 4
    		regs[user].Z  = (psw & 0x40) != 0; // Bit 6
    		regs[user].S  = (psw & 0x80) != 0; // Bit 7

    		// Fetch the Accumulator from SP+1
    		regs[user].A = memory[user][regs[user].SP + 1];
    
    		// Update Stack Pointer
    		regs[user].SP += 2;
    		break;
		}
        
        // JP word
        case 0xF2: // JP address
            if (regs[user].S == 0) {
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
             }else
                // branch not taken
                regs[user].PC += 2;

            break;
        
        // DI
        case 0xF3:
        	regs[user].INTE = 0;
        	break;
        	
        // CP word
        case 0xF4: // CP address
        {
            if (regs[user].S == 0)
            {
                uint16_t ret = regs[user].PC + 2;
                memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
                memory[user][regs[user].SP - 2] = (ret & 0xff);
                regs[user].SP = regs[user].SP - 2;
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            }
            else
                regs[user].PC  += 2;

            break;
        }
        
        // PUSH PSW
        case 0xF5: // PUSH PSW
        {
    		// Write Accumulator to SP-1
    		memory[user][regs[user].SP - 1] = regs[user].A;
    
    		// Explicitly convert bool flags to integers for shifting.
    		// Bit 1 is strictly hardwired to 1 on the physical 8080.
    		uint8_t psw = (regs[user].S  ? 0x80 : 0x00) |
            (regs[user].Z  ? 0x40 : 0x00) |
            (regs[user].AC ? 0x10 : 0x00) |
            (regs[user].P  ? 0x04 : 0x00) |
                             0x02 | // Bit 1 is ALWAYS 1
            (regs[user].CY ? 0x01 : 0x00);
                  
    		// Write packed flags to SP-2 and decrement Stack Pointer
    		memory[user][regs[user].SP - 2] = psw;
    		regs[user].SP -= 2;
    		break;
		}
        
        // ORI byte
        case 0xF6: // ORI byte
        {
            uint8_t x = regs[user].A | memory[user][regs[user].PC + 1];
            regs[user].Z = (x == 0);
            regs[user].S = ((x & 0x80) == 0x80);
            regs[user].P = parity(x, 8);
            regs[user].CY = 0; // Data book says op clears carry flags
            regs[user].AC = 0;
            regs[user].A = x;
            
            regs[user].PC++; // offset byte
            break;
        }
        
        // RST 6
        case 0xF7: // RST 6
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = 0x30;
            
            FLAG = 1;
            break;
        }
        
        // RM
        case 0xF8: // RM
            if (regs[user].S == 1)
            {
                regs[user].PC = memory[user][regs[user].SP] | (memory[user][regs[user].SP + 1] << 8);
                regs[user].SP += 2;
                FLAG = 1;
            }

            break;
            
        // SPHL
        case 0xF9: // SPHL
            regs[user].SP = (regs[user].H << 8) | (regs[user].L);
            break;
            
        // JM word
        case 0xFA: // JM address
            if (regs[user].S == 1) {
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            } else
                // branch not taken
                regs[user].PC += 2;

            break;
            
        // EI
        case 0xFB:
        	regs[user].INTE = 1;
        	break;        
        
        // CM
        case 0xFC: // CM address
        {
            if (regs[user].S == 1)
            {
                uint16_t ret = regs[user].PC + 2;
                memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
                memory[user][regs[user].SP - 2] = (ret & 0xff);
                regs[user].SP = regs[user].SP - 2;
                regs[user].PC = (memory[user][regs[user].PC + 2] << 8) | memory[user][regs[user].PC + 1];
                FLAG = 1;
            }
            else
                regs[user].PC += 2;

            break;
        }
        
        // 0xfd is an unused opcode
        
        // CPI byte
        case 0xFE: // CPI byte
        {
            uint16_t res = (uint16_t) regs[user].A - (uint16_t) memory[user][regs[user].PC + 1];
    
    		// Zero flag
    		regs[user].Z = ((res & 0xff) == 0);
    
    		// Sign flag
    		regs[user].S = ((res & 0x80) == 0x80);
    
    		// Carry flag: Sets if A is less than B (requires a borrow)
    		regs[user].CY = (regs[user].A < memory[user][regs[user].PC + 1]);
    
    		// Parity flag
    		regs[user].P = parity(res & 0xff, 8);
    
    		// Auxiliary Carry: Sets if lower nibble of A is less than lower nibble of B
    		regs[user].AC = ((regs[user].A & 0xf) < (memory[user][regs[user].PC + 1] & 0xf));
    
    		regs[user].PC += 1; // for byte 
    		
    		break;
		}
        
        // RST 7
        case 0xFF: // RST 7
        {
            uint16_t ret = regs[user].PC + 2;
            memory[user][regs[user].SP - 1] = (ret >> 8) & 0xff;
            memory[user][regs[user].SP - 2] = (ret & 0xff);
            regs[user].SP = regs[user].SP - 2;
            regs[user].PC = 0x38;
            
            FLAG = 1;
            break;
        }
        	
		default:	// unknown opcode - return 1 (error)
			res = 1;
			break;

	}
		// break jumps to here	
		// Jumps, calls, anything that sets PC also sets the FLAG to 1
		if (FLAG == 0)
			regs[user].PC += 1;
		return res;
}	
		
	
/* test for parity */
static int parity(int ptest, int sz) {   // sz is unused  
    int p=0;
	ptest = (unsigned char)ptest;
    if (ptest==0)   /* odd parity/no parity */
        return(0);
    
    while (ptest != 0) {
        p ^= ptest;
        ptest >>= 1;
    }
    if ((p & 0x1)==0)   /* 0=even parity */
        return(1);  	/* parity set */
    else
        return(0);  	/* parity not set */
}	
	


// handle processor interrupts
void service_interrupt(void) {	

	// future use
	return;
}


