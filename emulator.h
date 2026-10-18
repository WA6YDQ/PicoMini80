/* emulator.h */

#define MAXUSER 1		// total number of users
#define MAXRAM 0xFFFF		// for testing

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


/* FCB used in IO routines that handle SD card file */
#define FCB 0x5C
#define FILENAME FCB+1
#define FILETYPE FCB+9
#define FILENAME2 FCB+41
#define FILETYPE2 FCB+49
#define FCBSTATUS FCB+40
#define FCB_LAST_HI FCB+38
#define FCB_LAST_LO FCB+39
#define TPA 0x100
#define TPAEND 0xEFFF




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


typedef struct {
    uint16_t PC;								// Program Counter
    uint16_t SP;								// Stack Pointer
    uint8_t A, B, C, D, E, H, L;				// 8080 register pairs, accumulator
    uint16_t R0, R1, R2, R3, R4, R5, R6, R7;	// expanded (index) register pairs
    bool CY, Z, P, AC, S, INTE;					// processor (8080) flags
} I80REGS;

// routines


