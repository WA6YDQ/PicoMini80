/* emulator.h */


/* ********************************************************************* */
/* ******** These next values can be changed to tune operation ********* */
/* ********************************************************************* */

// total number of users
#define MAXUSER 8		

// Time in microseconds each user gets
// before switching to another user
#define CONTEXT_TIME 20000	// 20 msec, time in uSeconds

// boot file to load at power up unless another file is named
// during the start-up sequence
#define DEFAULT_BOOT_FILE "boot.hex"	

#define RAMSTART 0x0000
#define RAMEND 0xEFFF		// RAM 60K

#define ROMSTART (RAMEND + 1)
#define ROMEND 0xFFFF		// ROM 4K

#define COMRAMSIZE 256		// small block of RAM common to all

#define PASSCOUNT 6
extern char PASSLIST[PASSCOUNT][32];	// PASSWORD list, set up in emulator_3.c


/* ************************************************************************ */
/* ******** Nothing below needs to be changed for normal operation ******** */
/* ************************************************************************ */

// led's and switches
#define HALTLED 22		// gpio pin numbers used
#define FAULTLED 26
#define RESETSW 27
#define ABORTSW 28
#define DEFAULT_LED 25	// on-board green led

// port and control pins
#define ALE 7			// GPIO numbers, not pin numbers
#define INT 21
#define INTA 20
#define IORD 2
#define IOWR 3

#define BAUD_RATE 115200
#define UART0_TX_PIN 0		// gpio pin numbers
#define UART0_RX_PIN 1
#define UART1_TX_PIN 4
#define UART1_RX_PIN 5


/* Define memory locations for IO routines */
/* these are in each users RAM, either zero page or near ramtop */
#define DEFAULT_STACK RAMEND  	// 80 bytes (40 levels deep)
#define OSRAM 0xEE00			// OS registers, buffers, etc start here, go UP (511 bytes)

/* TIMEBUF used for RTC */
#define TIMEBUF  (OSRAM+82)	// 80 bytes for time/date, below the stack

/* LOGNAME used in login routine */
#define LOGNAME (OSRAM+162)		// 32 bytes, below TIMEBUF-1

/* directory listing */
#define DIRMODE (OSRAM+196)		// 1 byte, sets dir wide (=0) or long (=1)

/* FCB used in IO routines that handle SD card file, in page 0 */
#define FCB 0x5C
#define FILENAME FCB+1
#define FILETYPE FCB+9
#define FILENAME2 FCB+41
#define FILETYPE2 FCB+49
#define FCBSTATUS FCB+40
#define FCB_LAST_HI FCB+38
#define FCB_LAST_LO FCB+39

// User memory space
#define TPA 0x100
#define TPAEND 0xEDFF	/* all above used by OS for buffers, storage, etc */




/* includes for all source code */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "hardware/watchdog.h"


typedef struct {
    uint16_t PC;								// Program Counter
    uint16_t SP;								// Stack Pointer
    uint8_t A, B, C, D, E, H, L;				// 8080 register pairs, accumulator
    uint16_t R0, R1, R2, R3, R4, R5, R6, R7;	// expanded (index) register pairs
    bool CY, Z, P, AC, S, INTE;					// processor (8080) flags
} I80REGS;




