/* 
multi user computer using the pi pico 2
(C) 2026 Kurt Theis

This is a multi-user 8 bit computer. 

The instructions are a superset of the 8080 
processor.

Estimated throughput is 448000 instructions per second
running the pi pico 2 at default clock speed (about 2x 
faster than an 8080 running at 2 MHz giving 290K instructions 
per second). This is with 3 users at 20 ms per time slice. More
or less users will affect the throughput. So 6x for single user.

Serial ports are:
0 - the usb port at 11500 baud
1 - uart0 at 115200 baus
2 - uart1 at 115200 baus

Each user has their own private ram block of 65536 bytes

---------------------------------------------
To build:
edit CMakeLists.txt
mkdir build
cd build
cmake ../
make

Then copy the .uf2 file to the pico device


---------------------------------------------
Device Addresses:

Output Port 0x00 is Console output for each user 
(port number is translated to specific hardware
port based on current user number)

Input Port 0x00 is Console input for each user
(port number is translated to specific hardware
port based on current user number)



---------------------------------------------

Build datails:
SD Card drivers: https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/tree/main
Command summary for SD Card: https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/tree/main/examples/command_line
File system routines: http://elm-chan.org/fsw/ff/00index_e.html


	Versions:
	
	0.3 psram routines implimented
	0.2 SD Card routines written	
	0.1 Basic Functionality

*/


/*                                      */
/* System Defaults and Program Settings */
/*                                      */

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

/* SD Card */
#include "ff.h"             // FatFS
#include "hw_config.h"		// part of SD card init
#include "f_util.h"

/* Define Hardware */
// led's and switches
#define HALTLED 22		// gpio pin numbers used
#define FAULTLED 26
#define RESET 27
#define ABORT 28
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

// SD Card
#define SPI_PORT 0	
#define PIN_MOSI 19		// gpio pin numbers
#define PIN_SCK 18
#define PIN_CS 17
#define PIN_MISO 16

// RAM chip definitions
#ifndef PSRAM_H
#define PSRAM_H

// Hardware Pin Definitions
#define PSRAM_SPI      spi0
#define PSRAM_PIN_MISO 16
#define PSRAM_PIN_SCK  18
#define PSRAM_PIN_MOSI 19
#define PSRAM_PIN_CS   6

// Command Codes for Adafruit 4677 / ESP-PSRAM64
#define PSRAM_CMD_WRITE      0x02
#define PSRAM_CMD_READ       0x03
#define PSRAM_CMD_RESET_EN   0x66
#define PSRAM_CMD_RESET      0x99

#endif // PSRAM_H


/* Define System Constants */
#define VERSION "0.3"
#define NUM_USERS 5				// one higher than assigned users (starting at 0)
#define CONTEXT_TIME 20000		// microsecond timeout (this should be dynamic: 320msec/NUM_USERS for 10msec/user based on logins)
#define DEBOUNCE 40				// switch debounce time in msec
#define USER_RAM_SIZE 65536


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

/* Misc Pointers */
#define TIMEBUF 0xFE52			// 80 bytes for time/date

/* Define External Routines */
int decode(uint8_t);
uint16_t getreg(uint16_t reg);
void putreg(uint16_t reg, uint16_t val);
void putpair(int16_t reg, uint16_t val);
int16_t getpair(int16_t reg);
int parity(unsigned char ptest);
void putpush(int reg, int data);
int getpush(int reg);
int cond(int con);
void setinc(int reg);
void setlogical(int32_t reg);
void setarith(int val);
int16_t getpair(int16_t reg);
void putpair(int16_t reg, uint16_t val);
void putreg(uint16_t reg, uint16_t val);
uint16_t getreg(uint16_t reg);
void output(uint8_t val, uint8_t address);
uint8_t input(uint8_t address);
void abort_routine(void);
void ram_usage(void);
int load_hexfile_to_ram(int, char *);
void spi_setup(void);
int type_file(char *);
int load_file_to_ram(int , uint16_t , char *);
void write_byte_to_bus(uint8_t , uint8_t);
uint8_t read_byte_from_bus(int8_t);

// Function Declarations for psram
void psram_init(void);
void psram_write(uint32_t address, const uint8_t *data, size_t len);
void psram_read(uint32_t address, uint8_t *buffer, size_t len);

// default bootrom (may be overridden during setup)
uint8_t bootrom0[100] = {0x3e,0x41,0xd3,0x00,0x3c,0xfe,0x46,0xc2,0x02,0x00,0xc3,0x0a,0x00};
uint8_t bootrom1[100] = {0x3e,0x46,0xd3,0x00,0x3c,0xfe,0x4b,0xc2,0x02,0x00,0xc3,0x0a,0x00};
uint8_t bootrom2[100] = {0x3e,0x4b,0xd3,0x00,0x3c,0xfe,0x50,0xc2,0x02,0x00,0xc3,0x0a,0x00};





// Structure to hold CPU states for each 8080 user instance
typedef struct {
    uint16_t PC;								// Program Counter
    uint16_t SP;								// Stack Pointer
    uint16_t A, BC, DE, HL;						// 8080 register pairs, accumulator
    uint16_t R0, R1, R2, R3, R4, R5, R6, R7;	// expanded register pairs
    uint8_t FLAGS;								// (unused)
    bool C, Z, P, AC, S, INTE;					// processor (8080) flags
} Intel8080_Regs;


    
// these are global variables 
volatile int active_user;
Intel8080_Regs user_context[NUM_USERS];			// structure holding registers
uint16_t temp, carry, hi, lo;					// used in decode()
uint8_t user_ram[NUM_USERS][USER_RAM_SIZE]; 	// User RAM
uint64_t software_timer;						// context switching timer
// for rtc functions
static time_t start_epoch = 0;
static int64_t start_us = 0;

/* for SD Card */
FRESULT fr;
FATFS fs;
FIL fil;


/*                                                  * 
/* ************** Routines Start Here ************* */
/*                                                  */


/* SD Card Setup */
void spi_setup(void) {

	spi_init(SPI_PORT, 1000 * 1000); // 1MHz baud rate
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);		// RX SPI
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);		// TX SPI
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);		// SCK SPI
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1); // Deselect CS initially

    return;    
}	
	
/* psram routines */
static inline void psram_cs_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PSRAM_PIN_CS, 0);  // Active low
    asm volatile("nop \n nop \n nop");
}

static inline void psram_cs_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PSRAM_PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}


void psram_select_bus(void) {
    // 1. Force ensure the SD card releases its MISO line by driving its CS HIGH
    gpio_put(PIN_CS, 1);	// pin 17
    
    // 2. Re-apply the high-speed PSRAM formatting overrides
    // This repairs any clock slowing or phase changes caused by the SD library
    spi_set_baudrate(PSRAM_SPI, 24000000); // Back to 24 MHz high speed
    spi_set_format(PSRAM_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

void psram_init(void) {
    // 1. Initialize SPI0 at 24 MHz (Stable target within boundaries for raw SPI)
    spi_init(PSRAM_SPI, 24000000);		// 24 mhz - lower speeds also work
    spi_set_format(PSRAM_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // 2. Configure SPI Pin Functions
    gpio_set_function(PSRAM_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PSRAM_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PSRAM_PIN_MOSI, GPIO_FUNC_SPI);

    // 3. Configure CS Pin as standard software-controlled GPIO
    gpio_init(PSRAM_PIN_CS);
    gpio_set_dir(PSRAM_PIN_CS, GPIO_OUT);
    psram_cs_deselect();

    // 4. Power-On Reset sequence required by the PSRAM chip
    uint8_t cmd_reset_en = PSRAM_CMD_RESET_EN;
    uint8_t cmd_reset = PSRAM_CMD_RESET;

    psram_cs_select();
    spi_write_blocking(PSRAM_SPI, &cmd_reset_en, 1);
    psram_cs_deselect();
    
    sleep_us(150); // Minimal gap between reset enablement and execution

    psram_cs_select();
    spi_write_blocking(PSRAM_SPI, &cmd_reset, 1);
    psram_cs_deselect();
    
    sleep_us(10); // Allow initialization time after reset
}

void psram_write(uint32_t address, const uint8_t *data, size_t len) {
    // Structure 4-byte header: Command + 24-bit Address
    uint8_t header[4];
    header[0] = PSRAM_CMD_WRITE;
    header[1] = (address >> 16) & 0xFF;
    header[2] = (address >> 8) & 0xFF;
    header[3] = address & 0xFF;

    psram_cs_select();
    // Send standard write command framing
    spi_write_blocking(PSRAM_SPI, &header[0], 4);
    // Stream data payload
    spi_write_blocking(PSRAM_SPI, data, len);
    psram_cs_deselect();
}

void psram_read(uint32_t address, uint8_t *buffer, size_t len) {
    uint8_t header[4];
    header[0] = PSRAM_CMD_READ;
    header[1] = (address >> 16) & 0xFF;
    header[2] = (address >> 8) & 0xFF;
    header[3] = address & 0xFF;

    psram_cs_select();
    // Send read command framing
    spi_write_blocking(PSRAM_SPI, header, 4);
    // Read directly into destination buffer
    spi_read_blocking(PSRAM_SPI, 0, buffer, len);
    psram_cs_deselect();
}

	
	
	
/* fgets routine for pi pico */
char* pico_fgets(char* str, int num, FILE* stream) {
    int i = 0;
    
    // Read characters until buffer is full (minus 1 for null-terminator)
    while (i < num - 1) {
        // Fetch a char. -1 means no character was received within the timeout
        int c = getchar();	// _timeout_us(50000); // NOTE: timeouts disabled
        
        if (c != -1) {
            // Echo character back to minicom so you can see what you are typing
            putchar(c); 
            
            // Handle both CR (\r) and LF (\n) as end-of-line signals
            if (c == '\r' || c == '\n') {
                str[i++] = '\n'; // Keep standard fgets behavior by ending with \n
                break;
            }
            
            // handle backspace
    		if (c == '\b') {
    		  if (i == 0) continue;
    		  str[i] = '\0';
    		  i--;
    		  printf(" ");
    		  printf("\b");
    		  continue;  
    		}
    		
    		// handle ^u (delete line)
    		if (c == 0x15) {
    			i = 0;
    			str[0] = '\0';
    			printf("^U\n");
    			continue;
    		}
    		
            
            // save char to buffer
            str[i++] = (char)c;
        }
    }
    
    if (i == 0) return NULL; // No data read
    
    str[i] = '\0'; // Properly terminate the string
    return str;
}


/* write an address to the bus, then write a byte to the bus (GP8 thru GP15) */
void write_byte_to_bus(uint8_t address, uint8_t data_byte) {
	
	// 1. Create a mask for GP8 through GP15 (0xFF shifted left by 8)
    //    0xFF00 translates to binary bits 8 to 15 set to 1.
    uint32_t mask = 0xFF00;
    uint32_t value;		// data to write
	
	// Initialize the pins to output
	#define GP8_15_MASK 0x0000FF00
	gpio_init_mask(GP8_15_MASK);
	// Set the direction to output
	gpio_set_dir_out_masked(GP8_15_MASK);
	
    
    // output address to the bus
    // shift address to GP8-GP15
    value = ((uint32_t)address) << 8;
    // put the address on the bus
    gpio_put_masked(mask, value);
    // strobe ALE 
    gpio_put(ALE,0);
    sleep_us(10);
    gpio_put(ALE,1);
    sleep_us(10);
    
    

    // Shift your 8-bit data into the GP8-GP15 position
    value = ((uint32_t)data_byte) << 8;
    // Atomically apply the value only to the masked pins
    gpio_put_masked(mask, value);
    // strobe IOWR
    gpio_put(IOWR,0);
    sleep_us(10);
    gpio_put(IOWR,1);
    
}

uint8_t read_byte_from_bus(int8_t address) {

	uint32_t mask = 0xFF00;
    uint32_t value;		// data to write

	// Initialize the pins to output (prepare for sending address)
	//#define GP8_15_MASK 0x0000FF00
	gpio_init_mask(GP8_15_MASK);
	// Set the direction to output
	gpio_set_dir_out_masked(GP8_15_MASK);

	// output address to the bus
    // shift address to GP8-GP15
    value = ((uint32_t)address) << 8;
    // put the address on the bus
    gpio_put_masked(mask, value);
    // strobe ALE 
    gpio_put(ALE,0);
    sleep_us(10);
    gpio_put(ALE,1);
    sleep_us(10);

	// prepare to read the bus
	// Set the direction to input
	gpio_set_dir_in_masked(GP8_15_MASK);
	// strobe IORD
	gpio_put(IORD,0);
	sleep_us(10);
	// Read all 32 GPIO pins at the exact same snapshot in time
    uint32_t all_pins = gpio_get_all();
	gpio_put(IORD,1);
	
	// Isolate pins 8-15 using a bitwise AND with 0xFF00, 
    // then shift them right by 8 positions to form a single 8-bit byte.
    uint8_t data_byte = (uint8_t)((all_pins & 0x0000FF00) >> 8);
	return data_byte;
	
}



/* Read the data bus into a single byte */
uint8_t read_byte_from_gp8_15(void) {
	// Initialize the pins to input
	#define GP8_15_MASK 0x0000FF00
	gpio_init_mask(GP8_15_MASK);
	// Set the direction to input
	gpio_set_dir_in_masked(GP8_15_MASK);
    // 1. Read all 32 GPIO pins at the exact same snapshot in time
    uint32_t all_pins = gpio_get_all();

    // 2. Isolate pins 8-15 using a bitwise AND with 0xFF00, 
    //    then shift them right by 8 positions to form a single 8-bit byte.
    uint8_t data_byte = (uint8_t)((all_pins & 0x0000FF00) >> 8);

    return data_byte;
}

/* initialize and set time using pi pico 2 xtal clock reference */
void init_current_time(void) {
    struct tm user_time = {0};
    int input;

    printf("\n--- Enter Current Local Date/Time ---\n");
    printf("Enter Year (e.g. 2026): ");   
    scanf("%d", &input); user_time.tm_year = input - 1900;
    printf("%d\n",input);
    
    printf("Enter Month (1-12): ");      
    scanf("%d", &input); user_time.tm_mon = input - 1;
    printf("%d\n",input);
    
    printf("Enter Day (1-31): ");        
    scanf("%d", &user_time.tm_mday);
    printf("%d\n",user_time.tm_mday);
    
    printf("Enter Hour (0-23): ");       
    scanf("%d", &user_time.tm_hour);
    printf("%d\n",user_time.tm_hour);
    
    printf("Enter Minute (0-59): ");     
    scanf("%d", &user_time.tm_min);
    printf("%d\n",user_time.tm_min);

    
    user_time.tm_sec = 0;
    user_time.tm_isdst = -1;

    // Save values globally
    start_epoch = mktime(&user_time);
    start_us = to_us_since_boot(get_absolute_time());
    
    printf("\nTime Set\n");
}

/* read and display on console current clock time/date */
void get_current_time(void) {
    // 1. Get the real elapsed time since the Pico booted up
    absolute_time_t now = get_absolute_time();
    int64_t elapsed_secs = (to_us_since_boot(now) - start_us) / 1000000;

    // 2. Add the elapsed seconds to our original base time
    time_t current_epoch = start_epoch + elapsed_secs;
    
    // 3. Break it back out into a readable structure
    struct tm *current_time = localtime(&current_epoch);

    // 4. Print it
    printf("Current Date/Time: \n%04d-%02d-%02d %02d:%02d:%02d\n", 
        current_time->tm_year + 1900, 
        current_time->tm_mon + 1, 
        current_time->tm_mday, 
        current_time->tm_hour, 
        current_time->tm_min, 
        current_time->tm_sec);
}

// display a file on the SD card to the console (aborts TYPE command)
int type_file(char *filename) {
	static FIL sdcard_file;
	uint br;
	uint8_t buffer[128];
	
	// init SD card
    //sd_card_t *pSD = sd_get_by_num(0);
    FATFS fs;
    
    /* Mount Card */
	FRESULT fr = f_mount(&fs, "", 1);
	if (FR_OK != fr) {
        printf("Error mounting card\n");
        return 1;
	}
	
	// open the file
	if (f_open(&sdcard_file, filename, FA_READ) != FR_OK) {
		printf("No file found\n");
        // unmount sd card
		f_unmount("");	// unmount card
		printf("SD card unmounted\n");
        return 1;
    }
    
    // view the file
    while (1) {
    	if (!f_gets(buffer, 128, &sdcard_file))
    		break;		// exit loop on nothing returned
    	printf("%s",buffer);
    }
    
    printf("\nEnd of File Reached\n");
	// close file
	f_close(&sdcard_file);
	
	// unmount sd card
	printf("Unmounting card\n");
	f_unmount("");	// unmount card
	
	return 0;
	
}


	
	
// load a given .hex file into ram for specified user
int load_hexfile_to_ram(int usernumber, char *filename) {

	static FIL sdcard_file;
	uint br;
	uint8_t buffer[128];
	int count = 0;		// running count of buffer[]
	uint8_t inchar;
	
	uint8_t tdat[4] = {0,0,0,0};
	int cnt, bytenum, recordtype, cat, cd=0;
  	uint8_t bytecount, addrhi, addrlo, recordType, checksum_read, checksum_derived;
  	uint16_t addr;
  	int ENDFLAG=0;    // set when recordtype != 0
  	
    FATFS fs;
    
    /* Mount Card */
	FRESULT fr = f_mount(&fs, "", 1);
	if (FR_OK != fr) {
        printf("Error mounting card\n");
        return 1;
	}
	
	// open the file
	if (f_open(&sdcard_file, filename, FA_READ) != FR_OK) {
		printf("No file found\n");
        // unmount sd card
		f_unmount("0:");
        return 1;
    }
    
	// loop to read and save record
	while (1) {
		memset(buffer,0,128);
		count = 1;	// skip initial ':'
		// read in a line from the file
		if (!f_gets(buffer, 128, &sdcard_file))
    		break;		// exit loop on nothing returned
    	if (f_eof(&sdcard_file)) break;

   	
   		// got start char, get the byte count to see how long the record is
		inchar = buffer[count++];
		if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	bytecount = (inchar-0x30) * 16;		
    	
    	inchar = buffer[count++];
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	bytecount += (inchar-0x30);       // we have the record length in bytecount
    	
    
    	// get the address where this record starts. First the high byte
    	inchar = buffer[count++];
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	addrhi = (inchar-0x30) * 16;
    	inchar = buffer[count++];
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	addrhi += (inchar-0x30);
    
    	// now the low byte
    	inchar = buffer[count++]; 
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	addrlo = (inchar-0x30) * 16;
    	inchar = buffer[count++];
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	addrlo += (inchar-0x30);
    	// add the two for absolute address
    	addr = (addrhi * 256) + addrlo;
    	
    	
    	/* get the record type (00:data, 01:end of file) */
    	inchar = buffer[count++];
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	recordType = (inchar-0x30) * 16;
    	inchar = buffer[count++];
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	recordType += (inchar-0x30);
    	if (recordType != 0x00) ENDFLAG=1;   // last line (01) or unknown (>1)
    	
    
    	// get bytecount chars
    	cd = 0; memset(tdat,0,4);
   		while (bytecount > 0) {
      		inchar = buffer[count++];
      		tdat[cd++] = inchar & 0xff;
      		if (cd < 2) continue;       // combine bytes
      		cd = 0;
      		// convert from ascii
      		if (tdat[0] > 0x39) tdat[0] -= 7;
      		tdat[0] -= 0x30;
      		if (tdat[1] > 0x39) tdat[1] -= 7;
      		tdat[1] -= 0x30;
      		// save byte
      		user_ram[usernumber][addr++] = (16 * tdat[0]) + tdat[1];
      		bytecount--;
    	}
    	
    	// get the checksum
    	inchar = buffer[count++];
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	checksum_read = (inchar-0x30) * 16;
    	inchar = buffer[count++];
    	if (inchar > 0x39) inchar -= 7;   // convert from ascii
    	checksum_read += (inchar-0x30);
    	
    
		// finished with record
		continue;
	}


	// close file
	fr = f_close(&sdcard_file);     

	/* close the mount point */
	f_unmount("0:");
	
	return 0;

}


/* load a binary file (typically .com) to defined ram memory */
int load_file_to_ram(int usernumber, uint16_t start_address, char *filename) {

	static FIL sdcard_file;
	uint br;
	FATFS fs;
	
	/* Mount Card */
	FRESULT fr = f_mount(&fs, "", 1);
	if (FR_OK != fr) {
        printf("Error mounting card\n");
        return 1;
	}
	
	// open the file
	if (f_open(&sdcard_file, filename, FA_READ) != FR_OK) {
		//printf("No file found\n");
        // unmount sd card
		f_unmount("0:");
        return 1;
    }
    
    FSIZE_t filesize;
    filesize = f_size(&sdcard_file);
    if (filesize >= (TPAEND - TPA)) {	// file too large *** HARDCODED VALUE F000
    	// close the file
    	fr = f_close(&sdcard_file);
    	// unmount the card
    	f_unmount("0:");
    	printf("Error - File Too Large\n");
    	return 1;
    }
    	
    //printf("load_file_to_ram(): TPA %04X  filesize %04X\n",TPA, filesize);
    
    // save end address to FBC
    // FCB+38 = last used address high, FCB+39 = last used address low
	user_ram[usernumber][FCB_LAST_HI] = ((filesize + TPA) & 0xff00) >> 8;
	user_ram[usernumber][FCB_LAST_LO] = (filesize + TPA) & 0x00ff;

    uint8_t file_buffer[filesize];
    memset(file_buffer,0,filesize);
    
    // read the file
    fr = f_read(&sdcard_file, &file_buffer, filesize, &br);
    
    int addr, count;
    // save buffer to RAM
    count = 0;
    for (addr = TPA; addr <= TPA + filesize; addr++) 
    	user_ram[usernumber][addr] = file_buffer[count++];
    	
    // close the file
    fr = f_close(&sdcard_file);
    // unmount the card
    f_unmount("0:");
	
	// done
	return 0;	
}


/* display the directory of the sd card drive */
/* NOTE: There are much better directory listing programs for FatFs formats */
int show_dir() {

	/* Mount Card */
	FRESULT fr = f_mount(&fs, "", 1);
	if (FR_OK != fr) {
        printf("Error mounting card\n");
        return 1;
	}
	
	ls("/");	// the root level files
	
	// unmount the card
    f_unmount("0:");
    
    return 0;
}
	

/*                  */
/* ***** Main ***** */
/*                  */

void main (void) {
	gpio_init(DEFAULT_LED);
    gpio_set_dir(DEFAULT_LED, GPIO_OUT);
    gpio_put(DEFAULT_LED,0);		// initially OFF

	
	
	// Local Variables
	uint8_t opcode;
	int error;
	
	// allow usb serial comms
	stdio_init_all();
	while (!stdio_usb_connected()) {
        sleep_ms(10); 
    }
    
    // clear/home
    printf("\033[2J \033[H");
	printf("\n\n\n\n### System Coming Up ###\n\n");
	printf("Serial port Setup Complete\n");
	sleep_ms(150);
	
	// init RAM chip
	printf("initializing PSRAM\n");
	psram_init();
	// Data test payloads
    uint8_t test_write[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
    uint8_t test_read[8] = {0};
    uint32_t target_addr = 0x001000; // Array target address in the 64Mb scope

	psram_select_bus();
    // printf("Writing test data to address 0x%06X...\n", target_addr);
    psram_write(target_addr, test_write, 8);

    // printf("Reading back data...\n");
    psram_read(target_addr, test_read, 8);
    // Verify consistency
    bool success = true;
    for(int i = 0; i < 8; i++) {
        // printf("Byte %d - Written: 0x%02X, Read: 0x%02X\n", i, test_write[i], test_read[i]);
        if(test_write[i] != test_read[i]) success = false;
    }

    if(success) {
        printf("PSRAM Read/Write Successful!\n");
    } else {
        printf("PSRAM Read/Write Error: Data Mismatch.\n");
    }
    
    #ifdef DEAD
    // tested speeds are 
    //		Write Completed in: 33213 us (0.033 seconds)
	//		Write Throughput:   1926.96 KB/s
	//		Read Completed in:  33220 us (0.033 seconds)
	//		Read Throughput:    1926.55 KB/s

    printf("testing psram speed\n");
    uint64_t ramtime = time_us_64();
 	static uint8_t test_buffer[65536];
 	memset(test_buffer, 'X', 65536);
    uint32_t target_address = 0x000000;
    
    // write time
    absolute_time_t write_start = get_absolute_time();
    psram_write(target_address, test_buffer, 65536);
    absolute_time_t write_end = get_absolute_time();
    int64_t write_duration_us = absolute_time_diff_us(write_start, write_end);
    float write_seconds = (float)write_duration_us / 1000000.0f;
    float write_speed = ((float)65536 / 1024.0f) / write_seconds;
    printf("Write Completed in: %lld us (%.3f seconds)\n", write_duration_us, write_seconds);
    printf("Write Throughput:   %.2f KB/s\n", write_speed);
    
    // 3. Measure Read Time
    absolute_time_t read_start = get_absolute_time();
    psram_read(target_address, test_buffer, 65536);
    absolute_time_t read_end = get_absolute_time();

    int64_t read_duration_us = absolute_time_diff_us(read_start, read_end);
    float read_seconds = (float)read_duration_us / 1000000.0f;
    float read_speed = ((float)65536 / 1024.0f) / read_seconds;

    printf("Read Completed in:  %lld us (%.3f seconds)\n", read_duration_us, read_seconds);
    printf("Read Throughput:    %.2f KB/s\n", read_speed);
    #endif
	
    
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
    
    /*
    // --- Writing Data ---
    // Send a string or raw bytes out of UART0
    uart_puts(uart0, "Hello from UART0!\r\n");
    
    // Send individual bytes out of UART1
    uart_putc(uart1, 'A'); 
    
    // --- Reading Data ---
    // Check if UART0 has incoming data waiting in its RX FIFO buffer
    if (uart_is_readable(uart0)) {
        char ch0 = uart_getc(uart0);
        // Process character (e.g., forward it to the USB serial console)
        printf("Received on UART0: %c\n", ch0);
    }
    
     // Check if UART1 has incoming data
    if (uart_is_readable(uart1)) {
        char ch1 = uart_getc(uart1);
        printf("Received on UART1: %c\n", ch1);
    }
    
    FIFO Buffers: Each UART block on the Pico 2 has a built-in 32-byte 
    deep hardware FIFO for transmit and receive. If your loop runs fast, 
    polling uart_is_readable() is more than fast enough for 115200 baud.
    
    
    On the usb serial port: 
    getchar(): Reads a single character from the USB serial input buffer.
    
    puts(const char *s): Writes a null-terminated string followed by a newline to the USB port.
    
    scanf(const char *format, ...): Reads and parses formatted input data from the USB input stream.
    
    gets() / fgets(): Reads a line of characters from the USB input stream.
    
    vprintf() / fprintf(): Formatted printing variants that target the USB output stream when configured as standard output
    
    printf: c standard
    
    getchar_timeout_us(uint32_t timeout_us): Attempts to read a character from the USB input buffer with a specified 
    microsecond timeout, 		 returning an error code if no data arrives.
    
    putchar_raw(int c) / puts_raw(const char *s): Output variants that bypass automatic carriage return (\r) and line feed (\n) translation
    
    stdio_flush(): Forces any buffered output data to immediately clear and send over the USB connection
    
    */
    
    

	/* ***** This is the RTC setup routine ***** */
	printf("Initializing the System Clock:\n");
	init_current_time();
	get_current_time();
	sleep_ms(200);
	

	// clear RAM memory for all users on start up
	printf("Clearing RAM Memory (user)\n");
    for (int count = 0; count < NUM_USERS; count++) {
    	// personal user ram
    	for (int addr = 0; addr < USER_RAM_SIZE; addr++) 
    		user_ram[count][addr] = 0; 
    }
    sleep_ms(200);
    
    // load bootrom
    
    #ifdef DEAD
    printf("Loading Backup Bootrom: ");
    for (int n=0; n<30; n++) 
    		user_ram[0][n] = bootrom0[n];
    printf("User0 ");
    
	for (int n=0; n<30; n++) 
    		user_ram[1][n] = bootrom1[n];
    printf("User1 ");
    
    for (int n=0; n<30; n++) 
    		user_ram[2][n] = bootrom2[n];
    printf("User2 ");
    
    printf("\nLoading boot file to user0\n");
    
    // user,filename
    load_hexfile_to_ram(0,"boot.hex");		// multiuser OS program
	//load_hexfile_to_ram(0,"diag.hex");	// 8080 diagnostic program
	#endif

	for (int n=0; n<NUM_USERS; n++) {
		printf("Loading boot.hex for user %d\n",n);
		load_hexfile_to_ram(n,"boot.hex");
	}

    printf("\nRAM setup complete\n");
    sleep_ms(200);
    
    
	// initialize PC, SP and registers for each user
	printf("Initializing Registers USER ");
    for (int count = 0; count < NUM_USERS; count++) {
    	user_context[count].PC = 0x0000;		// default initia; program counter
    	user_context[count].SP = 0xff00;		// default initial stack pointer
    	user_context[count].A = 0x00;
    	user_context[count].BC = 0x0000;
    	user_context[count].DE = 0x0000;
    	user_context[count].HL = 0x0000;
    	user_context[count].R0 = 0;
    	user_context[count].R1 = 0;
    	user_context[count].R2 = 0;
    	user_context[count].R3 = 0;
    	user_context[count].R4 = 0;
    	user_context[count].R5 = 0;
    	user_context[count].R6 = 0;
    	user_context[count].R7 = 0;
    	printf("%d  ",count);
    }
    printf("\n");
    sleep_ms(200);


	
	// Active runtime pointers used inside the main emulator execution loop
	// initial conditions at powerup
	printf("Setting up Ports and Hardware\n");
	active_user = 0;
	
	// set up hardware ports and lines
	
	// reset push button
	gpio_init(RESET);
    gpio_set_dir(RESET, GPIO_IN);
    gpio_pull_up(RESET);
    
    // abort push button
    gpio_init(ABORT);
    gpio_set_dir(ABORT, GPIO_IN);
    gpio_pull_up(ABORT);
    
    // halt LED
    gpio_init(HALTLED);
    gpio_set_dir(HALTLED, GPIO_OUT);
    gpio_put(HALTLED,0);
    
    // fault LED
    gpio_init(FAULTLED);
    gpio_set_dir(FAULTLED, GPIO_OUT);
    gpio_put(FAULTLED,0);
    
    
    #ifdef SDCARD_USED
    // Reassign SPI0 pins to the 16-19 cluster
  	SPI.setRX(MISO); // MISO
  	SPI.setCS(CS); // CS
  	SPI.setSCK(SCK); // SCK
  	SPI.setTX(MOSI); // MOSI
  	#endif
  	
  	spi_setup();
  	// Initialize the SD card


  	
  	gpio_init(IORD);
  	gpio_set_dir(IORD, GPIO_OUT);
  	gpio_put(IORD,1);		// active low
  	
  	gpio_init(IOWR);
  	gpio_set_dir(IOWR, GPIO_OUT);
  	gpio_put(IOWR,1);		// active low
  	
  	gpio_init(ALE);
  	gpio_set_dir(ALE, GPIO_OUT);
  	gpio_put(ALE, 1);		// active low
  	
  	gpio_init(INT);
  	gpio_set_dir(INT, GPIO_IN);
  	gpio_pull_up(INT);		// active low
  	
  	gpio_init(INTA);
  	gpio_set_dir(INTA, GPIO_OUT);
  	gpio_put(INTA, 1);		// active low
  	
    
    printf("Initial Setup Complete\n");
    sleep_ms(200);

    
    // start software timer for context switching
    printf("Entering Multi-user Mode\n");
    sleep_ms(200);
    printf("Starting Context Switching\n");
    sleep_ms(200);
    // emulation loop
    printf("Entering Execution Loop\n");
	printf("### System Running ###\n\n");
    
    // turn on CPU LED as activity indicator
    gpio_put(DEFAULT_LED,1);

	// start the context switching timer
	software_timer = time_us_64();
	
	
	/*                    */
	/*** EXECUTION LOOP ***/
	/*                    */
    while (1) {
    
    	// test interrupt pin for LOW
    	if (gpio_get(INT) == 0) {
    		if (user_context[active_user].INTE == 0) break;		// int flag not set - ignore INTS for now
    		// interrupts are enabled - handle them
    		gpio_put(FAULTLED,1);
    		sleep_ms(150);
    		gpio_put(FAULTLED,0);
    		if (gpio_get(INT)==0) {
    			while (gpio_get(INT)==0) sleep_ms(DEBOUNCE);
    			sleep_ms(DEBOUNCE);
    		}
    		continue;
    	}
    	
    	// test RESET switch
    	if (gpio_get(RESET) == 0) {
    		// turn OFF activity indicator
    		gpio_put(DEFAULT_LED,0);
    		// turn on HALT led while reset
    		gpio_put(HALTLED,1);
    		// reset PC's for all users to 0
    		for (int count = 0; count < NUM_USERS; count++)
    			user_context[count].PC = 0x0000;
    		// wait for reset release
    		if (gpio_get(RESET) == 0) {
    			while (gpio_get(RESET) == 0) ;
    			sleep_ms(DEBOUNCE);
    		}
    		sleep_ms(DEBOUNCE);
    		gpio_put(HALTLED,0);
    		// turn ON activity indicator
    		gpio_put(DEFAULT_LED,1);
    		continue;
    	}
    	
    	// test ABORT switch
    	if (gpio_get(ABORT)==0) {
    		// turn OFF activity indicator
    		gpio_put(DEFAULT_LED,0);
    		// turn on HALT led while reset
    		gpio_put(HALTLED,1);
    		// wait for switch release
    		if (gpio_get(ABORT) == 0) {
    			while (gpio_get(ABORT) == 0) ;
    			sleep_ms(DEBOUNCE);
    		}
    		sleep_ms(DEBOUNCE);
    		abort_routine();
    		// start to bring the processor back up
    		// reset PC's for all users to 0
    		
    		gpio_put(HALTLED,0);
    		// turn ON activity indicator
    		gpio_put(DEFAULT_LED,1);
    		continue;
    	}
    		
    	
    	// test context switch clock
    	if (time_us_64() > (software_timer + CONTEXT_TIME)) {
    		// we timed out
    		gpio_put(DEFAULT_LED,0);	// turn OFF activity indicator
    		
    		/*
    		psram_select_bus();
    		uint8_t *ramread, *ramwrite;
    		*ramwrite = 0xaa;
    		psram_write(0x100, ramwrite, 1);
    		psram_read(0x100, ramread, 1);
    		if (*ramread != *ramwrite) printf(".");
    		*/
    		
    		active_user = (active_user + 1) % NUM_USERS;	// switch user
            gpio_put(DEFAULT_LED,1);	// turn ON activity indicator
            software_timer = time_us_64();	// reset the timer
            continue;		// continue execution loop from the top
        }

    
    	// read byte, decode opcode
    	opcode = user_ram[active_user][user_context[active_user].PC];
    	error = decode(opcode);
    	if (error == 0) continue;	// continue with normal operation
    	

    	/* read OPCODE is bad (wrongly entered opcode or bad instruction decode) */
    	// 08, 10, 18, 20, 28, 30, 38, cb, d9, dd, ed, fc
    	// cb and d9 unused in 8085
    	// turn OFF activity indicator
    	gpio_put(DEFAULT_LED,0);
    	printf("\n### OPCODE TRAP ###\nUser %02d  ADDR:OPCODE %04X:%02X\n", active_user, user_context[active_user].PC, opcode);
    	printf("System Halted.\nWaiting for Manual RESET\n");
    	gpio_put(FAULTLED,1);
    	gpio_put(HALTLED,1);
    	
    	// drop to abort()
    	abort_routine();
    	

    	// turn on HALT led while reset
    	gpio_put(HALTLED,1);
    	// reset PC's for all users to 0
    	for (int count = 0; count < NUM_USERS; count++)
    		user_context[count].PC = 0x0000;
    	// wait for reset release
    	if (gpio_get(RESET) == 0) {
    		while (gpio_get(RESET) == 0) ;
    		sleep_ms(DEBOUNCE);
    	}
    	sleep_ms(DEBOUNCE);
    	
    	gpio_put(FAULTLED,0);	// clear fault led
    	gpio_put(HALTLED,0);	// clear halt led
    	gpio_put(DEFAULT_LED,1);	// turn ON activity indicator
    	software_timer = time_us_64();	// reset the timer
    	// continue after system reset
    	continue;
    
    }
 
    // end of loop
}





/* ****************************************************** */
/*           This is the Instruction Decoder              */
/* It is mostly 8080 opcodes with additional instructions */
/* ****************************************************** */

int decode(uint8_t opcode) {

	temp = hi = lo = 0;
	
	//return 0;
	
	// user_context[active_user].reg describes the registers and flags w/reg is A,BC,DE,HL,SP,FLAGS
	// user_context[active_user].flag describes the flags - type bool C, Z, P, AC, S, INTE
	// user_ram[active_user][user_context[active_user].PC] describes the memory at PC
	// user_ram[active_user][temp] when temp is the memory address
	
	/* Custom instruction set with 0xCB as there prefix code   */
	/* (CB is unused in the 8080 and 8085, but used in the Z80 */
	
	/* BREAKPOINT */
	if (opcode == 0xcb) {
		printf("Breakpoint at address %04X\n",user_context[active_user].PC);
		abort_routine();
		printf("Exiting abort()\n");
		user_context[active_user].PC += 1;
		return 0;
	}
	
	// NOP
	if (opcode == 0x00) {
		user_context[active_user].PC += 1;
		return 0;
	}
	
	// HLT
	if (opcode == 0x76) {
		gpio_put(HALTLED,1);
		return 0;		// do NOT increment address - just keep running the instruction
	}
	
	// MOV DEST,SRC
	if ((opcode & 0xC0)==0x40) {                    
    	temp = getreg(opcode & 0x07);               // read from reg
        putreg((opcode >> 3) & 0x07, temp);         // save to reg
        user_context[active_user].PC += 1;
        return 0;
    }
	
	// MVI nn
	if ((opcode & 0xC7)==0x06) {                    
    	putreg((opcode >> 3) & 0x07, user_ram[active_user][user_context[active_user].PC+1]);
        user_context[active_user].PC += 2;
    	return 0;
    }
    
     // LXI nn
    if ((opcode & 0xCF) == 0x01) {  
    	user_context[active_user].PC++;               
    	temp = user_ram[active_user][user_context[active_user].PC] & 0x00ff;
        user_context[active_user].PC++; 
        temp = temp | (user_ram[active_user][user_context[active_user].PC] << 8) & 0xff00;
        putpair((opcode >> 4) & 0x03, temp);
        user_context[active_user].PC += 1;
        return 0;
    }
        
    // LDAX    
	if ((opcode & 0xEF) == 0x0A) {                  
    	temp = getpair((opcode >> 4) & 0x03);
        putreg(7, user_ram[active_user][temp]);	// [user_context[active_user].PC]);
        user_context[active_user].PC += 1;
        return 0;
    }
        
    // STAX ***
    if ((opcode & 0xEF) == 0x02) {                  
    	temp = getpair((opcode >> 4) & 0x03);
        user_ram[active_user][temp] = getreg(7);
        user_context[active_user].PC += 1;
        return 0;
   	}
    
    // CMP
    if ((opcode & 0xF8) == 0xB8) {                  
    	temp = user_context[active_user].A & 0xFF;
        temp -= getreg(opcode & 0x07);
        setarith(temp);
        user_context[active_user].PC += 1;
        return 0;
    }
    
    // JMP <condition>
    if ((opcode & 0xC7) == 0xC2) {                  
            if (cond((opcode >> 3) & 0x07) == 1) {
                lo = user_ram[active_user][user_context[active_user].PC+1];
                hi = user_ram[active_user][user_context[active_user].PC+2]; 
                user_context[active_user].PC = (hi << 8) + lo;
            } else {
                user_context[active_user].PC += 3;	// condition NOT met - do not jump
            }
            return 0;
    }
    
    // CALL <condition>
    if ((opcode & 0xC7) == 0xC4) {                  
    	if (cond((opcode >> 3) & 0x07) == 1) {
    		// get the CALL address HI:LO
            lo = user_ram[active_user][user_context[active_user].PC+1];
            hi = user_ram[active_user][user_context[active_user].PC+2];
            user_context[active_user].PC += 3;
            // save the address after this instruction
            user_ram[active_user][--user_context[active_user].SP] = (user_context[active_user].PC >> 8) & 0xff;
            user_ram[active_user][--user_context[active_user].SP] = (user_context[active_user].PC & 0xff); 
            user_context[active_user].PC = (hi << 8) + lo;
        } else {
            user_context[active_user].PC += 3;	// condition NOT met - do not call
        }
        return 0;
    }
        
    // RET <condition> 
    if ((opcode & 0xC7) == 0xC0) {                  
        if (cond((opcode >> 3) & 0x07) == 1) {
            user_context[active_user].PC = user_ram[active_user][user_context[active_user].SP];  
            user_context[active_user].SP++;
            user_context[active_user].PC |= ((user_ram[active_user][user_context[active_user].SP] << 8) & 0xff00);
            user_context[active_user].SP++;
        } else {
            user_context[active_user].PC += 1;	// condition NOT met - do not return
        }
        return 0;
    }    
    
    // RST n
    if ((opcode & 0xC7) == 0xC7) {                  
        user_context[active_user].SP--;
        user_ram[active_user][user_context[active_user].SP] = ((user_context[active_user].PC >> 8) & 0xff);
        user_context[active_user].SP--;
        user_ram[active_user][user_context[active_user].SP] = (user_context[active_user].PC & 0xff);
        user_context[active_user].PC = opcode & 0x38;		// jump to 1 of 8 addresses
        return 0;
    }  
    
    // PUSH
    if ((opcode & 0xCF) == 0xC5) {                  
        temp = getpush((opcode >> 4) & 0x03);
        user_context[active_user].SP--;
        user_ram[active_user][user_context[active_user].SP] = (temp >> 8) & 0xff;
        user_context[active_user].SP--;
        user_ram[active_user][user_context[active_user].SP] = temp & 0xff;
        user_context[active_user].PC += 1;
        return 0;
    }
    
    // POP
    if ((opcode & 0xCF) == 0xC1) {                   
        temp = user_ram[active_user][user_context[active_user].SP];
        user_context[active_user].SP++;
        temp |= user_ram[active_user][user_context[active_user].SP] << 8;
        user_context[active_user].SP++;
        putpush((opcode >> 4) & 0x03, temp);
        user_context[active_user].PC += 1;
        return 0;
    }
        
    // ADD    
    if ((opcode & 0xF8) == 0x80) {                  
        user_context[active_user].A += getreg(opcode & 0x07);
        setarith(user_context[active_user].A);
        user_context[active_user].A = user_context[active_user].A & 0xFF;
        user_context[active_user].PC += 1;
        return 0;
    }    
    
    // ADC
    if ((opcode & 0xF8) == 0x88) {                  
        carry = 0;
        if (user_context[active_user].C) carry = 1;
        user_context[active_user].A += getreg(opcode & 0x07);
        user_context[active_user].A += carry;
        setarith(user_context[active_user].A);
        user_context[active_user].A = user_context[active_user].A & 0xFF;
        user_context[active_user].PC += 1;
        return 0;
    }
        
    // SUB    
    if ((opcode & 0xF8) == 0x90) {                  
        user_context[active_user].A -= getreg(opcode & 0x07);
        setarith(user_context[active_user].A);
        user_context[active_user].A = user_context[active_user].A & 0xFF;
        user_context[active_user].PC += 1;
        return 0;
    }
        
    // SBB
    if ((opcode & 0xF8) == 0x98) {                  
        carry = 0;
        if (user_context[active_user].C) carry = 1;
        user_context[active_user].A -= getreg(opcode & 0x07) + carry;
        setarith(user_context[active_user].A);
        user_context[active_user].A = user_context[active_user].A & 0xFF;
        user_context[active_user].PC += 1;
        return 0;
    }    
    
    // INR
    if ((opcode & 0xC7) == 0x04) {
        temp = getreg((opcode >> 3) & 0x07);
        temp++;
        setinc(temp);
        temp = temp & 0xFF;
        putreg((opcode >> 3) & 0x07, temp);
        user_context[active_user].PC += 1;
        return 0;
    }
    
    // DCR    
    if ((opcode & 0xC7) == 0x05) {                  
        temp = getreg((opcode >> 3) & 0x07);
        temp--;
        setinc(temp);
        temp = temp & 0xFF;
        putreg((opcode >> 3) & 0x07, temp);
        user_context[active_user].PC += 1;
        return 0;
    }
    
    // INX
    if ((opcode & 0xCF) == 0x03) {                  
        temp = getpair((opcode >> 4) & 0x03);
        temp++;
        temp = temp & 0xFFFF;
        putpair((opcode >> 4) & 0x03, temp);
        user_context[active_user].PC += 1;
        return 0;
    }
        
    // DCX    
    if ((opcode & 0xCF) == 0x0B) {                  
        temp = getpair((opcode >> 4) & 0x03);
        temp--;
        temp = temp & 0xFFFF;
        putpair((opcode >> 4) & 0x03, temp);
        user_context[active_user].PC += 1;
        return 0;
    }    
    
    // DAD
    if ((opcode & 0xCF) == 0x09) {
        uint32_t PAIR1=0, PAIR2=0;
        PAIR1 = user_context[active_user].HL & 0xffff;
        PAIR2 = (getpair((opcode >> 4) & 0x03) & 0xffff);
        PAIR1 += PAIR2;
        if (PAIR1 & 0x010000) 
            user_context[active_user].C = 1;
        else
            user_context[active_user].C = 0;
        user_context[active_user].HL = PAIR1 & 0xffff;    
        user_context[active_user].PC += 1;
        return 0;
    }
        
    
    // ANA
    if ((opcode & 0xF8) == 0xA0) {                  
        user_context[active_user].A &= getreg(opcode & 0x07);
        user_context[active_user].C = 0;
        setlogical(user_context[active_user].A);
        user_context[active_user].A &= 0xFF;
        user_context[active_user].PC += 1;
        return 0;
    }
        
    // ORA
    if ((opcode & 0xF8) == 0xB0) {                  
        user_context[active_user].A |= getreg(opcode & 0x07);
        user_context[active_user].C = 0;
        setlogical(user_context[active_user].A);
        user_context[active_user].A &= 0xFF;
        user_context[active_user].PC += 1;
        return 0;
    } 
    
    // XRA
    if ((opcode & 0xF8) == 0xA8) {                  
        user_context[active_user].A ^= getreg(opcode & 0x07);
        user_context[active_user].C = 0;
        setlogical(user_context[active_user].A);
        user_context[active_user].A &= 0xFF;
        user_context[active_user].PC += 1;
        return 0;
    }
        
        
    /* now do the rest of the instructions */

    switch(opcode) {

		// CPI
        case 0xfe: {                        
            temp = user_context[active_user].A & 0xFF;
            temp -= user_ram[active_user][user_context[active_user].PC+1]; 
            user_context[active_user].PC += 2;
            setarith(temp);
            break;
        }
            
        // ANI
        case 0xe6: {                        
            user_context[active_user].A &= user_ram[active_user][user_context[active_user].PC+1]; 
            user_context[active_user].PC += 2;
            user_context[active_user].C = user_context[active_user].AC = 0;
            setlogical(user_context[active_user].A);
            user_context[active_user].A &= 0xFF;
            break;
        }
        
        // ORI
        case 0xf6: {                        
            user_context[active_user].A |= user_ram[active_user][user_context[active_user].PC+1]; 
            user_context[active_user].PC += 2;
            user_context[active_user].C = user_context[active_user].AC = 0;
            setlogical(user_context[active_user].A);
            user_context[active_user].A &= 0xFF;
            break;
        }
        
        // XRI
        case 0xee: {                        
            user_context[active_user].A ^= user_ram[active_user][user_context[active_user].PC+1];
            user_context[active_user].PC += 2;
            user_context[active_user].C = user_context[active_user].AC = 0;
            setlogical(user_context[active_user].A);
            user_context[active_user].A &= 0xFF;
            break;
        }
        
        
         /* Jump/Call instructions */
         
        // JMP (unconditional) 
        case 0xc3: {                        
            lo = user_ram[active_user][user_context[active_user].PC+1];
            hi = user_ram[active_user][user_context[active_user].PC+2];
            user_context[active_user].PC = (hi << 8) + lo;
            break;
        }
            
        
        // PCHL
        case 0xe9: {                        
            user_context[active_user].PC = user_context[active_user].HL;
            break;
        }    
        
        // CALL (unconditional)
        case 0xcd: {
            lo = user_ram[active_user][user_context[active_user].PC+1];
            hi = user_ram[active_user][user_context[active_user].PC+2]; 
            user_context[active_user].PC += 3;       // point to address after call
            user_context[active_user].SP--;
            user_ram[active_user][user_context[active_user].SP] = (user_context[active_user].PC >> 8) & 0xff;
            user_context[active_user].SP--;
            user_ram[active_user][user_context[active_user].SP] = (user_context[active_user].PC & 0xff);
            user_context[active_user].PC = (hi << 8) + lo;
            break;
        }
        
        
        // RET (unconditional)
        case 0xc9: {                        
            user_context[active_user].PC = user_ram[active_user][user_context[active_user].SP];
            user_context[active_user].SP++;
            user_context[active_user].PC |= ((user_ram[active_user][user_context[active_user].SP] << 8) & 0xff00);
            user_context[active_user].SP++;
            break;
        }
            
            
        /* Data Transfer instructions */
        
        // STA    
        case 0x32: {                        
            lo = user_ram[active_user][user_context[active_user].PC+1]; 
            hi = user_ram[active_user][user_context[active_user].PC+2]; 
            user_context[active_user].PC += 3;
            temp = (hi << 8) + lo;
            user_ram[active_user][temp] = user_context[active_user].A; 
            break;
        }
        

        // LDA
        case 0x3a: {                        
            lo = user_ram[active_user][user_context[active_user].PC+1];  
            hi = user_ram[active_user][user_context[active_user].PC+2]; 
            temp = (hi << 8) + lo;
            user_context[active_user].A = user_ram[active_user][temp];
            user_context[active_user].PC += 3;
            break;
        }
        
        
        // SHLD
        case 0x22: {                        
            lo = user_ram[active_user][user_context[active_user].PC+1]; 
            hi = user_ram[active_user][user_context[active_user].PC+2];
            user_context[active_user].PC += 3;
            temp = (hi << 8) + lo;
  
            user_ram[active_user][temp] = user_context[active_user].HL;
            temp++;
   
            user_ram[active_user][temp] = ((user_context[active_user].HL >> 8) & 0x00ff);
            break;
        }
            
        
        // LHLD
        case 0x2a: {                        
            lo = user_ram[active_user][user_context[active_user].PC+1];
            hi = user_ram[active_user][user_context[active_user].PC+2]; 
            user_context[active_user].PC += 3;
            temp = (hi << 8) + lo;
            user_context[active_user].HL = user_ram[active_user][temp];  
            temp++;
            user_context[active_user].HL = user_context[active_user].HL | user_ram[active_user][temp] << 8; 
            break;
       }
       
       
       // XCHG
       case 0xeb: {                        
            temp = user_context[active_user].HL;
            user_context[active_user].HL = user_context[active_user].DE;
            user_context[active_user].DE = temp;
            user_context[active_user].PC += 1;
            break;
        }
            
        
        /* Arithmetic Instructions */
        
        // ADI    
        case 0xc6: {                        
            user_context[active_user].A += user_ram[active_user][user_context[active_user].PC+1];  
            user_context[active_user].PC += 2;
            setarith(user_context[active_user].A);
            user_context[active_user].A = user_context[active_user].A & 0xFF;
            break;
        }  
        
        // ACI
        case 0xce: {                        
            carry = 0;
            if (user_context[active_user].C) carry = 1;
            user_context[active_user].A += user_ram[active_user][user_context[active_user].PC+1]; 
            user_context[active_user].A += carry;
            setarith(user_context[active_user].A);
            user_context[active_user].A = user_context[active_user].A & 0xFF;
            user_context[active_user].PC += 2;
            break;
        }
            
        
        // SUI           
        case 0xd6: {                        
            user_context[active_user].A -= user_ram[active_user][user_context[active_user].PC+1]; 
            user_context[active_user].PC += 2;
            setarith(user_context[active_user].A);
            user_context[active_user].A = user_context[active_user].A & 0xFF;
            break;
        }
        
        
        // SBI
        case 0xde: {                        
            carry = 0;
            if (user_context[active_user].C) carry = 1;
            user_context[active_user].A -= (user_ram[active_user][user_context[active_user].PC+1] + carry);  
            user_context[active_user].PC += 2;
            setarith(user_context[active_user].A);
            user_context[active_user].A = user_context[active_user].A & 0xFF;
            break;
        }
        
        // DAA
        case 0x27: {                        
            uint8_t a_hi, a_lo;
            a_lo = user_context[active_user].A & 0x0F; 
            a_hi = (user_context[active_user].A >> 4) & 0x0F;
                
            if (a_lo > 9 || user_context[active_user].AC == 1) {
                a_lo += 6;
                if (a_lo > 0xf) {
                    user_context[active_user].C = 1;
                    a_hi += 1;                  
                }
            }
            a_lo &= 0x0f; 

            if (a_hi > 9 ) a_hi += 6;
            if (a_hi > 15) 
                user_context[active_user].C = 1;
            else
                user_context[active_user].C = 0;
                
            a_hi &= 0xf;
                    
            user_context[active_user].A = (a_hi << 4);
            user_context[active_user].A |= a_lo;
            user_context[active_user].A &= 0xFF;

            user_context[active_user].Z = 0;
            if (user_context[active_user].A == 0) user_context[active_user].Z = 1;
            user_context[active_user].P = parity(user_context[active_user].A);
            user_context[active_user].S = 0;
            if (user_context[active_user].A > 0x80) user_context[active_user].S = 1;
            user_context[active_user].PC += 1;
            break;    
            
        }
        
        // RLC
        case 0x07: {
            user_context[active_user].C = 0;
            user_context[active_user].A = (user_context[active_user].A << 1);
            if (user_context[active_user].A & 0x100) user_context[active_user].C = 1;
            user_context[active_user].A &= 0xFF;
            if (user_context[active_user].C)
                user_context[active_user].A |= 0x01;
            user_context[active_user].PC += 1;
            break;
        }
        
        
        // RRC 
        case 0x0f: {
            user_context[active_user].C = 0;
            if ((user_context[active_user].A & 0x01) == 1)
                user_context[active_user].C = 1;
            user_context[active_user].A = (user_context[active_user].A >> 1) & 0xFF;
            if (user_context[active_user].C)
                user_context[active_user].A |= 0x80;
            user_context[active_user].PC += 1;
            break;       
        }    
            
        // RAL
        case 0x17: {
            temp = user_context[active_user].C;
            user_context[active_user].C = 0;
            user_context[active_user].A = (user_context[active_user].A << 1);
            if (user_context[active_user].A & 0x100) user_context[active_user].C = 1;
            user_context[active_user].A &= 0xFF;
            if (temp)
                user_context[active_user].A |= 1;
            else
                user_context[active_user].A &= 0xFE;
            user_context[active_user].PC += 1;
            break;
        }
        
        // RAR
        case 0x1f: {                        
            temp = user_context[active_user].C;
            user_context[active_user].C = 0;
            if ((user_context[active_user].A & 0x01) == 1)
                user_context[active_user].C = 1;
            user_context[active_user].A = (user_context[active_user].A >> 1) & 0xFF;
            if (temp)
                user_context[active_user].A |= 0x80;
            else
                user_context[active_user].A &= 0x7F;
            user_context[active_user].PC += 1;
            break;
        }
            
        
        // CMA
        case 0x2f: {                        
            user_context[active_user].A = ~ user_context[active_user].A;
            user_context[active_user].A &= 0xFF;
            user_context[active_user].PC += 1;
            break;
        }

		// CMC
        case 0x3f: {                        
            user_context[active_user].C = !user_context[active_user].C;
            user_context[active_user].PC += 1;
            break;
        }

        // STC
        case 0x37: {                        
            user_context[active_user].C = 1;
            user_context[active_user].PC += 1;
            break;
        }   
        
        
        /* Stack and Control Group */
         
        // XTHL    
        case 0xe3: {                        
            lo = user_ram[active_user][user_context[active_user].SP]; 
            hi = user_ram[active_user][user_context[active_user].SP + 1];  
            user_ram[active_user][user_context[active_user].SP] = user_context[active_user].HL & 0xff;  
            user_ram[active_user][user_context[active_user].SP + 1] = (user_context[active_user].HL >> 8) & 0xff;  
            user_context[active_user].HL = (hi << 8) + lo;
            user_context[active_user].PC += 1;
            break;
        }
        
        
        // SPHL
        case 0xf9: {                        
            user_context[active_user].SP = user_context[active_user].HL;
            user_context[active_user].PC += 1;
            break;
        }
            
        
        // EI (enable external interrupts)
        case 0xfb: {                        
            user_context[active_user].INTE = 1;
            // give full time to current user
            software_timer = time_us_64(); 
            user_context[active_user].PC += 1;
            break;
        }

        
        // DI  (disable external interrupts)  
        case 0xf3: {                        
            user_context[active_user].INTE = 0;
            user_context[active_user].PC += 1;
            break;
        }     
            
        
        // OUT
        case 0xd3:  {   
        	// call a seperate routine for outputs                   
            output(user_context[active_user].A, user_ram[active_user][user_context[active_user].PC+1]);
            user_context[active_user].PC += 2;
            //software_timer = time_us_64();		// restart context timer 
            break;
        }
        
        // IN    
        case 0xdb:  {  
        	// call a seperate routine for inputs                     
            user_context[active_user].A = input(user_ram[active_user][user_context[active_user].PC+1]);
            user_context[active_user].A &= 0xff;
            user_context[active_user].PC += 2;
            //software_timer = time_us_64();		// restart context timer
            break;
        }
            
        default:
        	// trap exceptions
        	gpio_put(FAULTLED,1);
        	return 1;
        	
    }
        
	// break jumps here
	return 0;
	
}	// end of 8080 decode





/* Get an 8080 register and return the value */
uint16_t getreg(uint16_t reg) {
    switch (reg) {
        case 0:     // B
            return ((user_context[active_user].BC >> 8) & 0x00ff);	//  ((BC >>8) & 0x00ff);
        case 1:     // C
            return (user_context[active_user].BC & 0x00ff); // (BC & 0x00ff);
        case 2:     // D
            return ((user_context[active_user].DE >> 8) & 0x00ff);  // ((DE >>8) & 0x00ff);
        case 3:     // E
            return (user_context[active_user].DE & 0x00ff); // (DE & 0x00ff);
        case 4:     // H
            return ((user_context[active_user].HL >> 8) & 0x00ff);  // ((HL >>8) & 0x00ff);
        case 5:     // L
            return (user_context[active_user].HL & 0x00ff);  // (HL & 0x00ff);
        case 6:     // (HL)
            return user_ram[active_user][user_context[active_user].HL] & 0x00ff;//  RAM[HL];     // fram.read8(HL);
        case 7:
            return user_context[active_user].A;  // (A);
        default:
            break;
    }
    return 0;		// unlikely to reach this point
}


/* Put a value into an 8080 register from memory */
void putreg(uint16_t reg, uint16_t val) {

	switch (reg) {
        case 0:
            user_context[active_user].BC = user_context[active_user].BC & 0x00FF;
            user_context[active_user].BC = user_context[active_user].BC | (val <<8);
            break;
        case 1:
            user_context[active_user].BC = user_context[active_user].BC & 0xFF00;
            user_context[active_user].BC = user_context[active_user].BC | val;
            break;
        case 2:
            user_context[active_user].DE = user_context[active_user].DE & 0x00FF;
            user_context[active_user].DE = user_context[active_user].DE | (val <<8);
            break;
        case 3:
            user_context[active_user].DE = user_context[active_user].DE & 0xFF00;
            user_context[active_user].DE = user_context[active_user].DE | val;
            break;
        case 4:
            user_context[active_user].HL = user_context[active_user].HL & 0x00FF;
            user_context[active_user].HL = user_context[active_user].HL | (val <<8);
            break;
        case 5:
            user_context[active_user].HL = user_context[active_user].HL & 0xFF00;
            user_context[active_user].HL = user_context[active_user].HL | val;
            break;
        case 6:
            // fram.write8(HL,val & 0xff);
            user_ram[active_user][user_context[active_user].HL] = val & 0xff;
            break;
        case 7:
            user_context[active_user].A = val & 0xff;
        default:
            break;
    }

}

/* Put a value into an 8080 register pair */
void putpair(int16_t reg, uint16_t val) {

		switch (reg) {
        case 0:
            user_context[active_user].BC = val;
            break;
        case 1:
            user_context[active_user].DE = val;
            break;
        case 2:
            user_context[active_user].HL = val;
            break;
        case 3:
            user_context[active_user].SP = val;
            break;
        default:
            break;
    }
}

/* Return the value of a selected register pair */
int16_t getpair(int16_t reg)
{
    switch (reg) {
        case 0:
            return (user_context[active_user].BC);
        case 1:
            return (user_context[active_user].DE);
        case 2:
            return (user_context[active_user].HL);
        case 3:
            return (user_context[active_user].SP);
        default:
            break;
    }
    return 0;
}


/* Set flags based on val */
void setarith(int val) {            // *** May need work
    if (val & 0x100)    // >= 256
        user_context[active_user].C = 1;
    else
        user_context[active_user].C = 0;
    if (val & 0x80) {   // negative
        user_context[active_user].S = 1;
    } else {
        user_context[active_user].S = 0;          // positive
    }
    if ((val & 0xff) == 0)
        user_context[active_user].Z = 1;
    else
        user_context[active_user].Z = 0;
    user_context[active_user].AC = 0;             // only true w/8080, not for Z80
    user_context[active_user].P = parity(val);

}


/* set flags after logical op */
void setlogical(int32_t reg)
{
    user_context[active_user].C = 0;      // always  (AND A opcode will clear CY)
    if (reg & 0x80) {
        user_context[active_user].S = 1;
    } else {
        user_context[active_user].S = 0;
    }
    if ((reg & 0xff) == 0)
        user_context[active_user].Z = 1;
      else
        user_context[active_user].Z = 0;
    user_context[active_user].AC = 0;
    user_context[active_user].P = parity(reg);
}


/* set flags after INR/DCR operation (8 bit only)*/
void setinc(int reg) {
    if (reg & 0x80) {
        user_context[active_user].S = 1;
    } else {
        user_context[active_user].S = 0;
    }
    if ((reg & 0xff) == 0)
        user_context[active_user].Z = 1;
      else
        user_context[active_user].Z = 0;
        
    user_context[active_user].P = parity(reg);

}


/* Test an 8080 flag condition and return 1 if true, 0 if false */
int cond(int con)
{
    switch (con) {
        case 0:
            if (user_context[active_user].Z == 0) return (1);
            break;
        case 1:
            if (user_context[active_user].Z != 0) return (1);
            break;
        case 2:
            if (user_context[active_user].C == 0) return (1);
            break;
        case 3:
            if (user_context[active_user].C != 0) return (1);
            break;
        case 4:
            if (user_context[active_user].P == 0) return (1);
            break;
        case 5:
            if (user_context[active_user].P != 0) return (1);
            break;
        case 6:
            if (user_context[active_user].S == 0) return (1);
            break;
        case 7:
            if (user_context[active_user].S != 0) return (1);
            break;
        default:
            break;
    }
    return (0);
}


/* get value of register pair */
int getpush(int reg) {

    int stat;

    switch (reg) {
        case 0:
            return (user_context[active_user].BC);
        case 1:
            return (user_context[active_user].DE);
        case 2:
            return (user_context[active_user].HL);
        case 3:		// AF
            stat = user_context[active_user].A << 8;
            if (user_context[active_user].S) stat |= 0x80;
            if (user_context[active_user].Z) stat |= 0x40;
            if (user_context[active_user].AC) stat |= 0x10;
            if (user_context[active_user].P) stat |= 0x04;
            stat |= 0x02;
            if (user_context[active_user].C) stat |= 0x01;
            return (stat);
        default:
            break;
    }
    return 0;
}


/* put value of register pair */
void putpush(int reg, int data) {

    switch (reg) {
        case 0:
            user_context[active_user].BC = data;
            break;
        case 1:
            user_context[active_user].DE = data;
            break;
        case 2:
            user_context[active_user].HL = data;
            break;
        case 3:		// AF
            user_context[active_user].A = (data >> 8) & 0xff;
            user_context[active_user].S = 0;
            user_context[active_user].Z = 0;
            user_context[active_user].AC = 0;
            user_context[active_user].P = 0;
            user_context[active_user].C = 0;
            if (data & 0x80) user_context[active_user].S  = 1;
            if (data & 0x40) user_context[active_user].Z  = 1;
            if (data & 0x10) user_context[active_user].AC = 1;
            if (data & 0x04) user_context[active_user].P  = 1;
            if (data & 0x01) user_context[active_user].C  = 1;
            break;
        default:
            break;
    }
}


/* test for parity */
int parity(unsigned char ptest) {    
    int p=0;

    if (ptest==0)   /* odd parity/no parity */
        return(0);
    
    while (ptest != 0) {
        p ^= ptest;
        ptest >>= 1;
    }
    if ((p & 0x1)==0)   /* 0=even parity */
        return(1);  /* parity set */
    else
        return(0);  /* parity not set */
}



/*                     */
/*** output routines ***/
/*                     */
void output(uint8_t val, uint8_t address) {
	
	
	/* ********* OUT port 0 - console output ************** */
	if (address == 0) {
	
		if (active_user == 0) {
			printf("%c",val); stdio_flush();
			return;
		}

		
		if (active_user == 1) {
			uart_putc(uart0, val); 
			return;
		}
		
		if (active_user == 2) {
			uart_putc(uart1, val);
			return;
		}
		
		if (active_user == 3) {
			write_byte_to_bus(3, val);	// port 3 is on-board processor uart
			return;
		}
		
		if (active_user == 4) {
			write_byte_to_bus(4, val);	// port 4 is on-board processor uart
			return;
		}
		
		// no other users
		return;
	}
	
	
	/*  OUT port 24, 25, 26, 27 - write to bit 0 on co processor*/
	if (address > 23 && address < 28) {
		val = val & 0x01;		// val will always be 0 or 1
		write_byte_to_bus(address, val);	
		return;
	}
	
	
	/* OUT port 32 - write (and read for input) to a single byte of psram */
	if (address == 32) {
		uint8_t readval;
		uint32_t psram_addr;		// address for the psram = HL + (active_user * 65536)
									// value is byte to save
		psram_addr = user_context[active_user].HL + (active_user * 65536);
		//printf("OUT 32: psram address = %d  val = %02X\n",psram_addr,val);		// DEBUG
		psram_select_bus();
    	psram_write(psram_addr, &val, 1);
    	//printf("wrote %02X\n",val);
    	// verify read
    	psram_read(psram_addr, &readval, 1);
    	if (readval != val)
    		printf("Wrote %02X, read back %02X\n",val, readval);	// DEBUG
		return;
	}
	
	
	/* OUT port 33 - write (and read for input) to a single byte of global (shared) psram */
	if (address == 33) {
		uint8_t readval;
		uint32_t psram_addr;		// address for the psram = HL + (active_user * 65536)
									// value is byte to save
		psram_addr = user_context[active_user].HL + (8 * 65536);
		psram_select_bus();
    	psram_write(psram_addr, &val, 1);
    	// verify read
    	psram_read(psram_addr, &readval, 1);
    	if (readval != val)
    		printf("Wrote %02X, read back %02X\n",val, readval);	// DEBUG
		return;
	}
	
	
	/* ******* OUT port 248 - save RAM to a named file ****** */
	// filename in FCB, Start of memory to save in DE, last memory location in HL
	if (address == 248) {
		// get addresses
		uint16_t file_start = user_context[active_user].DE;
		uint16_t file_end = user_context[active_user].HL;
		
		printf("Start: %04X   End: %04X \n",file_start, file_end);
		
		if (file_end - file_start <= 0) {		// empty block
			user_ram[active_user][FCBSTATUS] = 1;
			printf("Error - block to save is 0 bytes\n");
			return;
		}
		
		// get filename
		char filename[13] = {'\0'};
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			if (user_ram[active_user][FILENAME + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILENAME + c];
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (user_ram[active_user][FILETYPE + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILETYPE + c];
		}
		
		printf("Filename: %s\n",filename);
		
		// mount SD card, open file for write, save block
		FATFS fs;
		FIL sdcard_file;
		FRESULT fr;
		UINT addr, bw;		// read count, write count
		uint8_t buffer[512];
    
    	/* Mount Card */
		fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    printf("Error mounting card - return code %d\n",fr);
    	    user_ram[active_user][FCBSTATUS] = 1;
    	    return ;	
		}
		
		// create file
		fr = f_open(&sdcard_file, filename, FA_CREATE_NEW | FA_WRITE);
		
		if (fr != 0) {
			printf("File Create Failed - return code %d\n",fr);
			user_ram[active_user][FCBSTATUS] = fr;
			f_unmount("0:");
			return;
		}
	
		while (1) {		// write loop
			memset(buffer,0,512);
			if (file_start >= file_end) break;
			if ((file_end - file_start) < 512) break;
			for (addr = 0; addr < 512; addr++) 
				buffer[addr] = user_ram[active_user][file_start + addr];
			fr = f_write(&sdcard_file, buffer, addr, &bw);
			file_start += 512;
		}
		
		// more then 0, but less than 512 bytes to save
		if (file_end - file_start > 0) {
			// write byte by byte
			for (int addr = file_start; addr < file_end; addr++)
				f_putc(user_ram[active_user][addr], &sdcard_file);
		}
		
		// memory block saved - close file
		f_close(&sdcard_file);
		
		// unmount the card
    	f_unmount("0:");
    	user_ram[active_user][FCBSTATUS] = 0;
		return ;
}
			
		

	/* ********* OUT port 253 - delete a file ******** */
	// file name in FCB
	if (address == 253) {
		
		char filename[13] = {'\0'};
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			if (user_ram[active_user][FILENAME + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILENAME + c];
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (user_ram[active_user][FILETYPE + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILETYPE + c];
		}
		

		FATFS fs;
    
    	/* Mount Card */
		FRESULT fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    printf("Error mounting card\n");
    	    user_ram[active_user][FCBSTATUS] = 1;
    	    return ;	
		}
		
		// remove file
		fr = f_unlink(filename);
		if (fr != 0) {
			printf("Delete Failed: fr=%d\n",fr);
			user_ram[active_user][FCBSTATUS] = fr;
			return ;
		};

		// unmount the card
    	f_unmount("0:");
    	user_ram[active_user][FCBSTATUS] = 0;
		return ;
	}
	
	
	/* ***** OUT port 252 - create an empty file ********* */
	if (address == 252) {
	
		char filename[13] = {'\0'};
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			if (user_ram[active_user][FILENAME + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILENAME + c];
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (user_ram[active_user][FILETYPE + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILETYPE + c];
		}
		
		FATFS fs;
		FIL sdcard_file;
    
    	/* Mount Card */

		FRESULT fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    printf("Error mounting card\n");
    	    user_ram[active_user][FCBSTATUS] = 1;
    	    return ;	
		}
		
		// create file
		fr = f_open(&sdcard_file, filename, FA_CREATE_NEW | FA_WRITE);
		
		if (fr != 0) {
			printf("create failed - return code %d\n",fr);
			user_ram[active_user][FCBSTATUS] = fr;
			return;
		}
		
		// close the file
		f_close(&sdcard_file);
		
		// unmount the card
    	f_unmount("0:");
    	user_ram[active_user][FCBSTATUS] = 0;
		return ;
	}
	
	/* ******** OUT port 251 Return Date/Time *********** */
	if (address == 251) {
		absolute_time_t now = get_absolute_time();
    	int64_t elapsed_secs = (to_us_since_boot(now) - start_us) / 1000000;
	    time_t current_epoch = start_epoch + elapsed_secs;
	    struct tm *current_time = localtime(&current_epoch);

		char datebuf[80] = {'\0'};
    	sprintf(datebuf,"Current Date/Time: \n%04d-%02d-%02d %02d:%02d:%02d\n", 
        current_time->tm_year + 1900, 
        current_time->tm_mon + 1, 
        current_time->tm_mday, 
        current_time->tm_hour, 
        current_time->tm_min, 
        current_time->tm_sec);
        // copy datebuf to ram[] 
        for (int n=0; n<80; n++)
        	user_ram[active_user][TIMEBUF + n] = datebuf[n];
        return;
    }
        
     
    /* ********** OUT port 250 copy file1 file2 ********* */  
    if (address == 250) {
    	char filefrom[13] = {'\0'};
    	char fileto[13] = {'\0'};
    	
    	int c=0, i=0, br;
    	 
        // get from filename from FCB
		for (c=0; c<8; c++) {
			if (user_ram[active_user][FILENAME + c] == '\0') continue;
			filefrom[i++] = user_ram[active_user][FILENAME + c];
		}
		filefrom[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (user_ram[active_user][FILETYPE + c] == '\0') continue;
			filefrom[i++] = user_ram[active_user][FILETYPE + c];
		}

		
		i=0;
		// get to filename from FCB
		for (c=0; c<8; c++) {
			if (user_ram[active_user][FILENAME2 + c] == '\0') continue;
			fileto[i++] = user_ram[active_user][FILENAME2 + c];
		}
		fileto[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (user_ram[active_user][FILETYPE2 + c] == '\0') continue;
			fileto[i++] = user_ram[active_user][FILETYPE2 + c];
		}
		
		// printf("copy: filefrom [%s]  fileto [%s]\n",filefrom,fileto);
		FATFS fs;
		FIL read_file, write_file;
    
    	/* Mount Card */
		FRESULT fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    printf("Error mounting card\n");
    	    user_ram[active_user][FCBSTATUS] = 1;
    	    return ;	
		}
		
		// open read file
		fr = f_open(&read_file, filefrom, FA_READ);
		if (fr != 0) {
			printf("Error opening read file %s\n",filefrom);
			// unmount the card
    		f_unmount("0:");
    		user_ram[active_user][FCBSTATUS] = fr;
    		return;
    	}

		// get file size
		FSIZE_t filesize;
		filesize = f_size(&read_file);
		// printf("filesize %04X\n",(uint16_t)filesize);
		 if (filesize >= (TPAEND - TPA)) {	// file too large *** HARDCODED VALUE F000
		 	printf("Input file too large\n");
		 	f_close(&read_file);
		 	f_unmount("0:");
    		user_ram[active_user][FCBSTATUS] = fr;
    		return;
    	}
		char buffer[filesize];
		
		// open write_file
		fr = f_open(&write_file, fileto, FA_CREATE_ALWAYS | FA_WRITE);
		if (fr != 0) {
			printf("Error opening write file %s\n",fileto);
			f_close(&read_file);
			// unmount the card
    		f_unmount("0:");
    		user_ram[active_user][FCBSTATUS] = fr;
    		return;
    	}
		
		// read from source
    	fr = f_read(&read_file, &buffer, filesize, &br);
    	if (fr != FR_OK) {
    		printf("Error reading input file: %d\n",fr);
    	}
    	
    	// write to copy
    	fr = f_write(&write_file, &buffer, filesize, &br);
    	if (fr != FR_OK) {
    		printf("Error writing to output file: %d\n",fr);
    	}
    	
    	// close files
    	fr = f_close(&read_file);
    	if (fr != FR_OK) printf("Error closing input file\n");
    	fr = f_sync(&write_file);
    	if (fr != FR_OK) printf("Error syncing output file\n");
    	fr = f_close(&write_file);
    	if (fr != FR_OK) printf("Error closing output file\n");
    	// unmount the card
    	f_unmount("0:");
    	
    	// update result code
    	user_ram[active_user][FCBSTATUS] = 0;
		return;
	}
		
		
	// -------------- OUT Port 249 File Stats --------------	
	if (address == 249) {
		char filename[13] = {'\0'};
		FRESULT fr;
		FILINFO fno;
		
		int c=0, i=0;
		FATFS fs;
		
		// get from filename from FCB
		for (c=0; c<8; c++) {
			if (user_ram[active_user][FILENAME + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILENAME + c];
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (user_ram[active_user][FILETYPE + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILETYPE + c];
		}
		
		/* Mount Card */
		fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    printf("Error mounting card\n");
    	    user_ram[active_user][FCBSTATUS] = 1;
    	    return ;	
		}
		
		printf("File: %s\n",filename);
		fr = f_stat(filename, &fno);
    	switch (fr) {

    	case FR_OK:
        	printf("Size: %lu\n", fno.fsize);
        	printf("Timestamp: %u-%02u-%02u, %02u:%02u\n",
        	       (fno.fdate >> 9) + 1980, fno.fdate >> 5 & 15, fno.fdate & 31,
        	       fno.ftime >> 11, fno.ftime >> 5 & 63);
        	printf("Attributes: %c%c%c%c%c\n",
        	       (fno.fattrib & AM_DIR) ? 'D' : '-',
        	       (fno.fattrib & AM_RDO) ? 'R' : '-',
        	       (fno.fattrib & AM_HID) ? 'H' : '-',
        	       (fno.fattrib & AM_SYS) ? 'S' : '-',
        	       (fno.fattrib & AM_ARC) ? 'A' : '-');
        	break;

    	case FR_NO_FILE:
    	case FR_NO_PATH:
    	    printf("\"%s\" is not exist.\n", filename);
    	    break;

    	default:
    	    printf("An error occured. (%d)\n", fr);
    	}
		
		// unmount the card
    	f_unmount("0:");
    	
		user_ram[active_user][FCBSTATUS] = 0;
		return;
	}
		
		
		
	// no other ports
	printf("Invalid Port %d\n",address);
	return;


}


/*** input routines ***/
uint8_t input(uint8_t address) {

    /* IN port 0: non-blocking keyboard input - returns 0xff if no data available */
    if (address == 0) { 
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
       	if (active_user == 3) {				// port 3 is uart0 on coprocessor
       		val = read_byte_from_bus(3);
       	}
       	if (active_user == 4) {				// port 4 is uart1 on coprocessor
       		val = read_byte_from_bus(4);
       	}
       	// now return the char just got
       	return val;
    }


	/* IN port 21, 22, 23 */
	/* ADC chan 0,1 or 2 from coprocessor */
	if (address == 21 || address == 22 || address == 23) {
		return read_byte_from_bus(address);
	}


	/* IN port 24, 25, 26, 27 - input bit 0 from cpu board copropcessor */
	if (address > 23 && address < 28) {
		uint8_t val = read_byte_from_bus(address);
		val &= 0x01;		// return value will always be bit 0
		return val;
	}


	/* IN port 32 - read input from a single byte of psram */
	if (address == 32) {
		uint32_t psram_addr;		// address for the psram = HL + (active_user * 65536)
									// value is byte to save
		uint8_t val; 
		psram_addr = user_context[active_user].HL + (active_user * 65536);
		//printf("IN 25: psram address = %d ",psram_addr);		// DEBUG
		psram_select_bus();
    	psram_read(psram_addr, &val, 1);

		return val;
	}


	/* IN port 33 - read input from a single byte of global (shared) psram */
	if (address == 33) {
		uint32_t psram_addr;		// address for the psram = HL + (active_user * 65536)
									// value is byte to save
		uint8_t val; 
		psram_addr = user_context[active_user].HL + (8 * 65536);
		psram_select_bus();
    	psram_read(psram_addr, &val, 1);

		return val;
	}


	/* IN port 248 - load file into user memory */
	if (address == 248) {
		
		// get filename
		char filename[13] = {'\0'};
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			if (user_ram[active_user][FILENAME + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILENAME + c];
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (user_ram[active_user][FILETYPE + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILETYPE + c];
		}
		
		// mount SD card, open file for write, save block
		FATFS fs;
		FIL sdcard_file;
		FRESULT fr;
		UINT addr, br;		// read count, write count
		char ch;
    
    	/* Mount Card */
		fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    printf("Error mounting card\n");
    	    user_ram[active_user][FCBSTATUS] = 1;
    	    f_unmount("0:");
    	    return fr;	
		}

		/* open file for read */
		fr = f_open(&sdcard_file, filename, FA_READ | FA_OPEN_EXISTING);
		if (fr != 0) {
			printf("open failed - return code %d\n",fr);
			user_ram[active_user][FCBSTATUS] = fr;
			f_unmount("0:");
			return fr;
		}

		addr = user_context[active_user].HL;	// set start address
		
		/* read in file, stopping on EOF. Save to user ram */
		while (1) {
			if (f_eof(&sdcard_file)) break;
			fr = f_read(&sdcard_file, &ch, 1, &br);		// read byte
			if (fr != FR_OK) break;
			// save byte
			user_ram[active_user][addr++] = ch;
		}
		
		user_context[active_user].HL = addr;	// save address of last char in HL
		user_ram[active_user][addr++] = 0;
		user_ram[active_user][addr++] = 0;		// and write 2 NULLS after last char
		
		/* close the file */
    	fr = f_close(&sdcard_file);
    	if (fr != FR_OK) {
    		printf("Error closing input file\n");
			user_ram[active_user][FCBSTATUS] = fr;	// status byte
			f_unmount("0:");
			return fr;
		}
		
		// unmount the card
    	f_unmount("0:");
    	user_ram[active_user][FCBSTATUS] = 0;
		return fr;
		
}		



	/* IN port 253 - password test  */
	if (address == 253) {
		// test password in LOGNAME fea3-FEC3 (32 bytes) store name:pass for login command
		int PASSCOUNT = 4;
		char PASSLIST[][32] = {"fredderf","derffred","primepr1me","kurtkurt"};
		int BASEADR = 0xFEA3;
		char pass_to_test[33] = {'\0'};
		
		for (int n=0; n<33; n++) 
			pass_to_test[n] = user_ram[active_user][BASEADR+n];

		// look for match
		for (int n=0; n<PASSCOUNT; n++) {
			if (strcmp(pass_to_test,PASSLIST[n])==0) 
			return 0xff;
		}
		return 0;
	}



	/* IN port 254 - load a disk file to RAM starting at TPA - return 0 on success, 1 on failure */
	if (address == 254) {
		#define TPA 0x100	// start memory address

		char filename[13] = {'\0'};
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			if (user_ram[active_user][FILENAME + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILENAME + c];
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (user_ram[active_user][FILETYPE + c] == '\0') continue;
			filename[i++] = user_ram[active_user][FILETYPE + c];
		}
		
		// see if file exists
		
		//int res = load_comfile_to_ram(active_user,filename, TPA);
		int res = load_file_to_ram(active_user, TPA, filename);
		user_ram[active_user][FCBSTATUS] = res;
		
		return res;
	}	
	


	
	/* IN port 255 - use the system routines to show the directory of the disk */
	if (address == 255) {
		if (active_user == 0) {
			show_dir();
			return 0;
		}
	}
	
	
	// no other ports defined
	return 0;
}


/*** ABORT ROUTINE ***/
void abort_routine(void) {

	/* While this routine is active all processor functions are paused
	   and all users programs are stalled. When exiting this routine all
	   users routines are started where they left off.
    */
	
	char buffer[80];
	char ch;
	int input;
	
	printf("\nType 'help' for a command summary\n");
	while (1) {
		
		
		printf("\nabort> ");
		memset(buffer,0,80);
		pico_fgets(buffer,80,stdin); 
		printf("\n");

		char *p = strchr(buffer,'\n');
		if (p) *p = '\0';
		if (buffer[0] == '\0') continue; 	// ignore empty lines

		
		if (buffer[0] == 'R') {
			// restart processor
			for (int count = 0; count < NUM_USERS; count++)		// reset all PC's to 0
    			user_context[count].PC = 0x0000;
			break;
		}
		
		if (buffer[0] == '0') {
			// return - no changes
			break;
		}
		
		// set rtc
		if (buffer[0] == '1') {
			init_current_time();
			printf("\n");
			continue;
		}
		
		// show rtc
		if (buffer[0] == '2') {
			get_current_time();
			printf("\n");
			continue;
		}
		
		// show registers for all users
		if (buffer[0] == '3') {
			int i=0;
			printf("\nREGISTERS [user]:hex value\n");
			printf("\n A ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%02X ",i,user_context[i].A);
			printf("\nBC ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%04X ",i,user_context[i].BC);
			printf("\nDE ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%04X ",i,user_context[i].DE);
			printf("\nHL ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%04X ",i,user_context[i].HL);
			printf("\nSP ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%04X ",i,user_context[i].SP);
			printf("\nPC ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%04X ",i,user_context[i].PC);	
				
			printf("\n\nFLAGS [user]:value\n");		//  C, Z, P, AC, S, INTE
			printf("\n C ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%d ",i,user_context[i].C);
			printf("\n Z ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%d ",i,user_context[i].Z);
			printf("\n P ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%d ",i,user_context[i].P);
			printf("\nAC ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%d ",i,user_context[i].AC);
			printf("\n S ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%d ",i,user_context[i].S);
			printf("\nINTE ");
			for (i=0; i<NUM_USERS; i++)
				printf("[%d]:%d ",i,user_context[i].INTE);
			
			//
			printf("\n\n");
			continue;
		}	
				
		if (!strncmp(buffer,"hexload",4)) {		// load a .hex disk file into user memory
			char cmd[6], user[4], filename[40];
			sscanf(buffer,"%s %s %s",cmd, user, filename);
			if (atoi(user) < 0 || atoi(user) > NUM_USERS) {
				printf("Bad User Number %s\n",user);
				continue;
			}
			printf("loading filename [%s]\n",filename);
			int res = load_hexfile_to_ram(atoi(user), filename);
			if (res == 0) {
				printf("File load SUCCESS\n");
				continue;
			}
			printf("File load FAILED\n");
			continue;

		}
		
		if (!strncmp(buffer,"type",4)) {		// display a file on the SD card
			char cmd[6], filename[40];
			sscanf(buffer,"%s %s",cmd, filename);
			int res = type_file(filename);
			if (res == 0) {
				continue;
			}
			printf("File Not Found\n");
			continue;

		}

		
		if (!strncmp(buffer,"dump",4)) {		// show a block of user_ram[user][start address]
			// parse the line
			char cmd[6], USER[3], ADDR[6];
			sscanf(buffer,"%s %s %s", cmd, USER, ADDR);
			int user = atoi(USER);
			if (user < 0 || user >= NUM_USERS) {
				printf("Bad User Number %d\n",user);
				continue;
			}
			long addr = strtol(ADDR,NULL,16);
			if (addr < 0 || addr > 65536) {
				printf("Bad Address %X\n",addr);
				continue;
			}
      		addr = addr & 0xff00;	// strip LSB, just use MSB
      		printf("User %d   Address %04Xh\n", user, addr);
      		// display 256 bytes from page using MSB of address
      		int row=0, col=0;
      		while (row < 16) {
      			printf("%04X ",addr + (16 * row));
      			for (col=0; col < 16; col++) 
      				printf("%02X ",user_ram[user][addr + (16 * row) + col]);
      			printf("\n");
      			row++;
      		}
      		printf("\n");
			continue;
		}
		
		if (!strncmp(buffer,"dir",3)) {
			show_dir();
			continue;
		}
		
		if (!strncmp(buffer,"ver",3)) {
			printf("Version %s\n",VERSION);
			continue;
		}
		
		if (!strncmp(buffer,"help",4)) {
			printf("\n*** Abort Menu ***\n");
			printf("1. Set Clock\n");
			printf("2. Show Time\n");
			printf("3. Show Processor Status\n");
			printf("hexload [user] [file]  Load Disk File into User[n] Memory\n");
			printf("type [file]  Display Disk File Contexts\n");
			printf("dump [user] [address] Dump a page of User[n] Memory\n");
			printf("dir    Show Directory on Disk\n");
			printf("ver    Show Current Version\n");
			printf("help   Show this message\n");
		
		
			printf("\nR. Reset Processor\n");
			printf("0. Return w/o Changes\n");
			continue;
		}
		
		printf("\nEH? [%s]\n",buffer);
		continue;
	}
	
	
	return;
}

