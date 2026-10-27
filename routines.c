/* routines.c - holds various utility routines */


#include "emulator.h"


/* for SD Card */
#define SPI_PORT 0	
#define PIN_MOSI 19		// gpio pin numbers
#define PIN_SCK 18
#define PIN_CS 17
#define PIN_MISO 16

#include "ff.h"             // FatFS
#include "hw_config.h"		// part of SD card init
#include "f_util.h"

FRESULT fr;
FATFS fs;
FIL fil;

/* global externals */
extern I80REGS regs[MAXUSER];
extern uint8_t read_memory(uint8_t, uint16_t);
extern uint8_t write_memory(uint8_t, uint16_t, uint8_t);
extern void output(uint8_t, uint8_t, uint8_t);

extern int user;
extern uint8_t ram;
extern uint8_t rom;

// for rtc functions
extern time_t start_epoch;
extern int64_t start_us;

// instruction timing
extern uint64_t avg_inst_time;

/* local routine definitions */
char* pico_fgets(char*, int, FILE*);
void abort_routine(void);
int load_hexfile_to_ram(int, char *);
int load_file_to_ramrom(int, uint16_t, char*);
int abort_cmdLine(void);
void write_byte_to_bus(uint8_t, uint8_t);
uint8_t read_byte_from_bus(int8_t);
void init_current_time(void);
int show_dir(int);
void lls(int, const char *);
int send_to_output(int , char *);


/* *********** abort_routine ************* */
// call this when ABORTSW pressed or hardware break (0x10) opcode called
#pragma GCC push_options
#pragma GCC optimize ("O0")
void abort_routine(void) {

	printf("\n\nABORT Routine Invoked\n");
	gpio_put(HALTLED,1);
	
	// show instruction timing
	printf("Average Opcode Instruction Time %lld nS\n\n",avg_inst_time);
	
	// show machine registers
	printf("User\tPC\tA \tBC\tDE\tHL\tSP\tZ\tCY\tS\tP\tAC\n");
	for (int n=0; n<MAXUSER; n++) {
		printf("%d \t%04X\t%02X \t%02X%02X\t%02X%02X\t%02X%02X\t%04X",n, regs[n].PC, \
		regs[n].A, regs[n].B, regs[n].C, regs[n].D, regs[n].E, regs[n].H, regs[n].L, \
		regs[n].SP);
		printf("\t%01X\t%01X\t%01X\t%01X\t%01X \n",regs[n].Z, regs[n].CY, regs[n].S, regs[n].P, regs[n].AC);
	}
	
	// wait for ABORTSW release (if still pressed)
	if (gpio_get(ABORTSW)==0) 
		while (gpio_get(ABORTSW)==0) sleep_ms(40);
	sleep_ms(40);
	
	
	printf("Press RESET to continue, ABORT for command interface\n");
	while (1) {
		if (gpio_get(RESETSW)==0) break;
		if (gpio_get(ABORTSW)==0) {
			printf("Abort Switch Pressed\n");
			while (gpio_get(ABORTSW)==0) sleep_ms(40);
			sleep_ms(40);
			if (abort_cmdLine() == 1) break;;
			printf("\nPress RESET to continue, ABORT for command interface\n");
			continue;
		}
		sleep_ms(20);		// keep cpu cycles down
	}
	printf("Returning to Running State\n");
	
	if (gpio_get(RESETSW)==0) {	
		while (gpio_get(RESETSW)==0) 
			sleep_ms(40);
	}
	sleep_ms(40);
	gpio_put(HALTLED,0);
	return;
} /* --------------------------------------------------------------- */
#pragma GCC pop_options



#pragma GCC push_options
#pragma GCC optimize ("O0")
int abort_cmdLine(void) {
	char buffer[80];
	char ch;
	int input;
	
	
	printf("\n\nAbort Commands\nType 'help' for command list\n");
	while (1) {
	
		printf("\nabort> ");
		memset(buffer,0,80);
		pico_fgets(buffer,80,stdin); 
		printf("\n");

		char *p = strchr(buffer,'\n');
		if (p) *p = '\0';
		if (buffer[0] == '\0') continue; 	// ignore empty lines
		
		if (!strncmp(buffer,"reboot",6)) {
			printf("\nRebooting!\n");
			sleep_ms(750);
			watchdog_reboot(0, 0, 0);
			// Arguments: (pc, sp, delay_ms)
    		// Setting pc and sp to 0 tells the chip to perform a clean hardware reboot
		}
		
		if (!strncmp(buffer,"dir",3)) {
			//this replaces show_dir();
			// show files on SD card
			
		    FRESULT res;
		    FATFS *fs;
		    fs = malloc(sizeof (FATFS));
		    
		    char cmd[5];
		    const char path[20]={'\0'};
		    sscanf(buffer,"%s %s",cmd, path);

			res = f_mount(fs, "", 0);
			if (FR_OK != res) {
				printf("Error Mounting Card\n");
				f_unmount("0:");
				free(fs);
				continue;
			}
      		printf("Card Mounted OK\n");
			lls(0, path);		/* use ls routine instead of FatFS readdir() which is broken */
			f_unmount("0:");
			free(fs);
			printf("\n");
			continue;
		}
		
		if (!strncmp(buffer,"dump",4)) {		// show a block of user_ram[user][start address]
			// parse the line
			char cmd[6], USER[3], ADDR[6];
			sscanf(buffer,"%s %s %s", cmd, USER, ADDR);
			int user = atoi(USER);
			if (user < 0 || user >= MAXUSER) {
				printf("\nBad User Number %d\n",user);
				continue;
			}
			uint16_t addr = (uint16_t)strtol(ADDR,NULL,16);
			if (addr < 0 || addr > 65536) {
				printf("Bad Address %X\n",addr);
				continue;
			}
      		addr = addr & 0xff00;	// strip LSB, just use MSB
      		printf("User %d   Start Address %04Xh\n", user, addr);
      		// display 256 bytes from page using MSB of address
      		int row=0, col=0, locadr=0;
      		while (row < 16) {
      			printf("%04X ",addr + (16 * row));
      			for (col=0; col < 16; col++) {
      				locadr = addr + (row*16) + col;
      				if (locadr > ROMEND) {
      					printf("\nviewing address (%04X) exceeds bounds\n",locadr);
      					break;
      				}
      				//printf("[DEBUG] End of loop. Current row: %d, Next row will be: %d\n", row, row + 1);
      				uint8_t byte_val = read_memory(user, locadr);	//memory[user][locadr];
      				printf("%02X ",byte_val);
      			}
      			printf("\n");
      			row++;
      		}
      		printf("\n");
			continue;
		}
		
		if (!strncmp(buffer,"exit",4)) {	// exit abort routine
			return 1;
		}
		
		if (!strncmp(buffer, "help", 4)) {
			printf("exit        exit the abort routine\n");
			printf("dir         display directory of files on SD card\n");
			printf("dump [user] [address]\n");
			printf("help        show this message\n");
			printf("reboot      reboot the machine and internal hardware\n");
			printf("\n");
			continue;
		}
		
		printf("Type 'help' for a command summary\n");
		
	}
} // ----------------------------------------------------------
#pragma GCC pop_options





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
} /* ------------------------------------------------------- */




#pragma GCC push_options
#pragma GCC optimize ("O0")
// load a given .hex file into ram for specified user
int load_hexfile_to_ramrom(int usernumber, char *filename) {

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
      		write_memory(usernumber, addr++, (16 * tdat[0] + tdat[1]));
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

} /* ------------------------------------------------ */
#pragma GCC pop_options



/* load a binary file (typically .com) to defined ram memory */
int load_file_to_ram(int usernumber, uint16_t start_address, char *filename) {

	static FIL sdcard_file;
	uint br;
	FATFS fs;
	
	char locbuf[80];
	
	/* Mount Card */
	FRESULT fr = f_mount(&fs, "", 1);
	if (FR_OK != fr) {
        sprintf(locbuf, "lf2r(): Error mounting card\n");
        send_to_output(usernumber, locbuf);
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
    	sprintf(locbuf, "lf2r(): Error - File Too Large\n");
    	send_to_output(usernumber, locbuf);
    	return 1;
    }
    	
    //printf("load_file_to_ram(): TPA %04X  filesize %04X\n",TPA, filesize);
    
    // save end address to FBC
    // FCB+38 = last used address high, FCB+39 = last used address low
	write_memory(usernumber, FCB_LAST_HI, (((filesize + TPA) & 0xff00) >> 8));
	write_memory(usernumber, FCB_LAST_LO, ((filesize + TPA) & 0x00ff));

    uint8_t file_buffer[filesize];
    memset(file_buffer,0,filesize);
    
    // read the file
    fr = f_read(&sdcard_file, &file_buffer, filesize, &br);
    
    int addr, count;
    // save buffer to RAM
    count = 0;
    for (addr = TPA; addr <= TPA + filesize; addr++) 
    	write_memory(usernumber, addr, file_buffer[count++]);
    	
    // close the file
    fr = f_close(&sdcard_file);
    // unmount the card
    f_unmount("0:");
	
	// done
	return 0;	
}/* ---------------------------------------------------------- */






/* write an address to the bus, then write a byte to the bus (GP8 thru GP15) */
void write_byte_to_bus(uint8_t address, uint8_t data_byte) {
	/* delays of 10 uS are unnecessaraly long - 1uS should work) */
	
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
    /* delays of 10 uS are unnecessaraly long - 1uS should work) */

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




/* initialize and set time using pi pico 2 xtal clock reference */
void init_current_time(void) {
    struct tm user_time = {0};
    int input;
	char buffer[80];

    printf("\n--- Enter Current Local Date/Time ---");
    printf("\nEnter Year (e.g. 2026): ");   
    memset(buffer,0,80);
	pico_fgets(buffer,80,stdin); 
    sscanf(buffer, "%d", &input); user_time.tm_year = input - 1900;
    
    printf("\nEnter Month (1-12): ");  
    memset(buffer,0,80);
	pico_fgets(buffer,80,stdin);     
    sscanf(buffer, "%d", &input); user_time.tm_mon = input - 1;
    
    printf("\nEnter Day (1-31): ");        
    memset(buffer,0,80);
	pico_fgets(buffer,80,stdin); 
    sscanf(buffer, "%d", &user_time.tm_mday);
    
    printf("\nEnter Hour (0-23): ");       
    memset(buffer,0,80);
	pico_fgets(buffer,80,stdin); 
    sscanf(buffer, "%d", &user_time.tm_hour);
    
    printf("\nEnter Minute (0-59): ");     
    memset(buffer,0,80);
	pico_fgets(buffer,80,stdin); 
    sscanf(buffer, "%d", &user_time.tm_min);

    
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




/* display the directory of the sd card drive */
/* NOTE: There are much better directory listing programs for FatFs formats */
int show_dir(int usernumber) {

	char locbuf[80];
	
	/* Mount Card */
	FRESULT fr = f_mount(&fs, "", 1);
	if (FR_OK != fr) {
        sprintf(locbuf, "show_dir(): Error mounting card\n");
        send_to_output(usernumber, locbuf);
        return 1;
	}
	
	ls("/");	// the root level files
	
	// unmount the card
    f_unmount("0:");
    
    return 0;
}



/*** This is a better readdir then from FatFS ***/
/* this is pulled from ~/Pico/pico-tool/no-OS-FatFS-SD-SPI-RPi-Pico/example/FatFS_SPI_example.cpp */
void lls(int usernumber, const char *dir) {
const char *FRESULT_str(FRESULT);
	char locbuf[80];		// create string for output instruction
    char cwdbuf[FF_LFN_BUF] = {0};
    FRESULT fr; /* Return value */
    char const *p_dir;
    if (dir[0]) {
        p_dir = dir;
    } else {
        fr = f_getcwd(cwdbuf, sizeof cwdbuf);
        if (FR_OK != fr) {
            sprintf(locbuf, "f_getcwd error: %s (%d)\n", FRESULT_str(fr), fr);
            send_to_output(usernumber, locbuf);
            return;
        }
        p_dir = cwdbuf;
    }
    //printf("Directory Listing: %s\n", p_dir);
    DIR dj;      /* Directory object */
    FILINFO fno; /* File information */
    memset(&dj, 0, sizeof dj);
    memset(&fno, 0, sizeof fno);
    fr = f_findfirst(&dj, &fno, p_dir, "*");
    if (FR_OK != fr) {
        sprintf(locbuf, "f_findfirst error: %s (%d)\n", FRESULT_str(fr), fr);
        send_to_output(usernumber, locbuf);
        return;
    }
    int col = 1;
    while (fr == FR_OK && fno.fname[0]) { /* Repeat while an item is found */
        
        /* Create a string that includes the file name, the file size and the
         attributes string. */
        const char *pcWritableFile = "rw",	// "writable file",
                   *pcReadOnlyFile = "ro",	// "read only file",
                   *pcDirectory = "<DIR>";
        const char *pcAttrib;
        /* Point pcAttrib to a string that describes the file. */
        if (fno.fattrib & AM_DIR) {
            pcAttrib = pcDirectory;
        } else if (fno.fattrib & AM_RDO) {
            pcAttrib = pcReadOnlyFile;
        } else {
            pcAttrib = pcWritableFile;
        }
        
        /* Create a string that includes the file name */
        if (read_memory(usernumber, DIRMODE)==0) {		// wide mode
        	sprintf(locbuf,"%-12s    ",fno.fname);
        	send_to_output(usernumber,locbuf);
        	col++;
        	if (col == 6) {
        		col = 1;
        		sprintf(locbuf,"\n");
        		send_to_output(usernumber, locbuf);
        	}
		}
        
        /* Create a string with filename, , the file size and the attributes string. */
        if (read_memory(usernumber, DIRMODE)==1) {		// long mode
        	sprintf(locbuf,"%06llu %3s %-12s\n", fno.fsize, pcAttrib, fno.fname);
        	send_to_output(usernumber,locbuf);
		}
		
        fr = f_findnext(&dj, &fno); /* Search for next item */
    }
    f_closedir(&dj);
}


/* common output via output port 0 */
int send_to_output(int user, char *outstring) {

	volatile int n;
	for (n = 0; n < strlen(outstring); n++)
		output(user, outstring[n], 0);		// user, single char in A, port# (always 0)
	return 0;
}
