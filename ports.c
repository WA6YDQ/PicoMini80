/* ports.c - define I/O hardware and OS hooks */

/*
	
	in    0		read from serial input device (USB console or uart)
	in   10		read a byte from common ram
	in  253		password test for login routines
	in  254		load a named file from SD card into RAM
	in  255		show SD card directory listing
	
	out   0		write to serial output device (USB console or uart)
	out  10		write a byte to common ram
	out 250		copy source_file, dest_file on SD card
	out	251		retreive date/time from RTC
	out 252 	create an empty file on the SD card
	out 253		delete a named file on sd card
	out 254		save a named file from RAM to an SD card file
	
*/


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "emulator.h"

/* SD Card */
#include "ff.h"             // FatFS
#include "hw_config.h"		// part of SD card init
#include "f_util.h"

extern void write_byte_to_bus(uint8_t, uint8_t);	// in routines.c
extern uint8_t read_byte_from_bus(int8_t);			// in routines.c
extern void write_byte_to_bus(uint8_t, uint8_t);	// in routines.c
extern uint8_t read_byte_from_bus(int8_t);			// in routines.c
extern int load_file_to_ram(int, uint16_t, char *);	// in routines.c
extern int show_dir(void);							// in routines.c
extern int send_to_output(int , char *);			// in routines.c
extern void lls(int, const char *);					// in routines.c

extern I80REGS regs[MAXUSER];
extern uint8_t read_memory(uint8_t, uint16_t);
extern uint8_t write_memory(uint8_t, uint16_t, uint8_t);

// for rtc functions
extern  time_t start_epoch;
extern  int64_t start_us;

// common ram block
extern uint8_t comram[];

/* ********************************************* */
/* ************* INPUT ROUTINES **************** */
/* ********************************************* */

uint8_t input(uint8_t active_user, uint8_t port) {

	char locbuf[80];
	

	/* ************************************************************************** */
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
    
    
    /* ************************************************ */
    /* IN port 10 - read a single byte from comram[256] */
    if (port == 10) {
    	uint16_t address = ((regs[active_user].H << 8) | regs[active_user].L);
    	return comram[(uint8_t)(address & 0x00ff)];
    }
    
    
    
    
    /* ***************************** */
    /* IN port 253 - password test   */
	if (port == 253) {
		// test password in LOGNAME (32 bytes) store name:pass for login command

		extern char PASSLIST[PASSCOUNT][32];	// defines in emulator_3.c, PASSCOUNT in emulator.h
		int BASEADR = LOGNAME;
		char pass_to_test[33] = {'\0'};
		
		for (int n=0; n<33; n++) 
			pass_to_test[n] = read_memory(active_user, BASEADR+n);

		// look for match
		for (int n=0; n<PASSCOUNT; n++) {
			if (strcmp(pass_to_test,PASSLIST[n])==0) 
			return 0xff;
		}
		return 0;
	}
	
	
	/* ***************************************************************************************** */
	/* IN port 254 - load a disk file to RAM starting at TPA - return 0 on success, 1 on failure */
	if (port == 254) {

		char filename[13] = {'\0'};
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			if (read_memory(active_user, FILENAME + c) == '\0') continue;
			filename[i++] = read_memory(active_user, FILENAME + c);
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (read_memory(active_user, FILETYPE + c) == '\0') continue;
			filename[i++] = read_memory(active_user, FILETYPE + c);
		}
		
		// load if file exists
		int res = load_file_to_ram(active_user, TPA, filename);
		write_memory(active_user, FCBSTATUS, res);
		
		return res;
	}	
	
	
	/* *********************************************************************** */
	/* IN port 255 - use the system routines to show the directory of the disk */
	if (port == 255) {
		// show files on SD card
			
		FRESULT res;
		FATFS *fs;
		fs = malloc(sizeof (FATFS));
		char locbuf[80];
		    
		char cmd[5];
		const char path[20]={"/"};

		res = f_mount(fs, "", 0);
		if (FR_OK != res) {
			sprintf(locbuf, "Error Mounting Card\n");
			send_to_output(active_user, locbuf);
			f_unmount("0:");
			free(fs);
			return 1;
		}

		lls(active_user, path);		/* use ls routine instead of FatFS readdir() which is broken */
		f_unmount("0:");
		free(fs);
		printf("\n");
		return 0;
	}

	sprintf(locbuf,"Error - calling non-existant port %d\n",port);
	send_to_output(active_user, locbuf);
	
	
}






/* ******************************************************** */
/* ********************** OUTPUT ROUTINES ***************** */
/* ******************************************************** */

void output(uint8_t active_user, uint8_t acc, uint8_t port) {

	char locbuf[80];
	
	
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
		// You will need to write your own drivers for any UARTS you add
		if (active_user > 2 && active_user < 8) {
			write_byte_to_bus(active_user, acc);	// user => address
			return;
		}
			
		// no other users
		return;
	}
	
	
	
	/* ************************************************************ */
	/* ************* OUT port 10 - common ram block access ******** */
	if (port == 10) {
    	uint16_t address = ((regs[active_user].H << 8) | regs[active_user].L);
    	comram[(uint8_t)(address & 0x00ff)] = acc;
    	return;
    }
	
	
	
	
	
	
	/* ********** OUT port 250 copy file1 file2 ********* */  
    if (port == 250) {
    	char filefrom[13] = {'\0'};
    	char fileto[13] = {'\0'};
    	char locbuf[80];
    	
    	int c=0, i=0, br;
    	 
        // get from filename from FCB
		for (c=0; c<8; c++) {
			// skip leading nulls
			if (read_memory(active_user, FILENAME + c) == '\0') continue;
			filefrom[i++] = read_memory(active_user, FILENAME + c);
		}
		filefrom[i++] = '.';		// save separator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			// skip leading nulls
			if (read_memory(active_user, FILETYPE + c) == '\0') continue;
			filefrom[i++] = read_memory(active_user, FILETYPE + c);
		}

		
		i=0;
		// get to filename from FCB
		for (c=0; c<8; c++) {
			// skip leading nulls
			if (read_memory(active_user, FILENAME2 + c) == '\0') continue;
			fileto[i++] = read_memory(active_user, FILENAME2 + c);
		}
		fileto[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			// skip leading nulls
			if (read_memory(active_user, FILETYPE2 + c) == '\0') continue;
			fileto[i++] = read_memory(active_user, FILETYPE2 + c);
		}
		
		// printf("copy: filefrom [%s]  fileto [%s]\n",filefrom,fileto);
		FATFS fs;
		FIL read_file, write_file;
    
    	/* Mount Card */
		FRESULT fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    sprintf(locbuf,"Error mounting card\n");
    	    send_to_output(active_user, locbuf);
    	    write_memory(active_user, FCBSTATUS, 1);
    	    return ;	
		}
		
		// open read file
		fr = f_open(&read_file, filefrom, FA_READ);
		if (fr != 0) {
			sprintf(locbuf,"Error opening read file %s\n",filefrom);
			send_to_output(active_user,locbuf);
			// unmount the card
    		f_unmount("0:");
    		write_memory(active_user, FCBSTATUS, fr);
    		return;
    	}

		// get file size
		FSIZE_t filesize;
		filesize = f_size(&read_file);
		// printf("filesize %04X\n",(uint16_t)filesize);
		 if (filesize >= (TPAEND - TPA)) {	// file too large *** HARDCODED VALUE F000
		 	sprintf(locbuf,"Input file too large\n");
		 	send_to_output(active_user, locbuf);
		 	f_close(&read_file);
		 	f_unmount("0:");
    		write_memory(active_user, FCBSTATUS, fr);
    		return;
    	}
		char buffer[filesize];
		
		// open write_file
		fr = f_open(&write_file, fileto, FA_CREATE_ALWAYS | FA_WRITE);
		if (fr != 0) {
			sprintf(locbuf,"Error opening write file %s\n",fileto);
			send_to_output(active_user, locbuf);
			f_close(&read_file);
			// unmount the card
    		f_unmount("0:");
    		write_memory(active_user, FCBSTATUS, fr);
    		return;
    	}
		
		// read from source
    	fr = f_read(&read_file, &buffer, filesize, &br);
    	if (fr != FR_OK) {
    		sprintf(locbuf,"Error reading input file: %d\n",fr);
    		send_to_output(active_user, locbuf);
    	}
    	
    	// write to copy
    	fr = f_write(&write_file, &buffer, filesize, &br);
    	if (fr != FR_OK) {
    		sprintf(locbuf, "Error writing to output file: %d\n",fr);
    		send_to_output(active_user, locbuf);
    	}
    	
    	// close files
    	fr = f_close(&read_file);
    	if (fr != FR_OK) {
    		sprintf(locbuf, "Error closing input file\n");
    		send_to_output(active_user, locbuf);
    	}
    	fr = f_sync(&write_file);
    	if (fr != FR_OK) {
    		sprintf(locbuf, "Error syncing output file\n");
    		send_to_output(active_user, locbuf);
    	}
    	fr = f_close(&write_file);
    	if (fr != FR_OK) {
    		sprintf(locbuf, "Error closing output file\n");
    		send_to_output(active_user, locbuf);
    	}
    	// unmount the card
    	f_unmount("0:");
    	
    	// update result code
    	write_memory(active_user, FCBSTATUS, 0);
		return;
	}
	
	
	/* ******** OUT port 251 Return Date/Time *********** */
	if (port == 251) {
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
        // display datebuf to calling user
        send_to_output(active_user, datebuf);
        // copy datebuf to ram[] 
        for (int n=0; n<80; n++)
        	write_memory(active_user, TIMEBUF + n, datebuf[n]);
        return;
    }
    
    
    
    /* ***** OUT port 252 - create an empty file ********* */
	if (port == 252) {
	
		char filename[13] = {'\0'};
		char locbuf[80];
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			// ignore leading nulls
			if (read_memory(active_user, FILENAME + c) == '\0') continue;
			filename[i++] = read_memory(active_user, FILENAME + c);
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			// ignore leading nulls
			if (read_memory(active_user, FILETYPE + c) == '\0') continue;
			filename[i++] = read_memory(active_user, FILETYPE + c);
		}
		
		FATFS fs;
		FIL sdcard_file;
    
    	/* Mount Card */

		FRESULT fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    sprintf(locbuf, "Error mounting card\n");
    	    send_to_output(active_user, locbuf);
    	    write_memory(active_user, FCBSTATUS, 1);
    	    return ;	
		}
		
		// create file
		fr = f_open(&sdcard_file, filename, FA_CREATE_NEW | FA_WRITE);
		
		if (fr != 0) {
			sprintf(locbuf, "create failed - return code %d\n",fr);
			send_to_output(active_user, locbuf);
			write_memory(active_user, FCBSTATUS, fr);
			return;
		}
		
		// close the file
		f_close(&sdcard_file);
		
		// unmount the card
    	f_unmount("0:");
    	write_memory(active_user, FCBSTATUS, 0);
		return ;
	}
	
	
    
    /* ********* OUT port 253 - delete a file ******** */
	// file name in FCB
	if (port == 253) {
		
		char filename[13] = {'\0'};
		char locbuf[80];
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			// skip initial nulls 
			if (read_memory(active_user, FILENAME + c) == '\0') continue;
			filename[i++] = read_memory(active_user, FILENAME + c);
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			// skip initial nulls
			if (read_memory(active_user, FILETYPE + c) == '\0') continue;
			filename[i++] = read_memory(active_user, FILETYPE + c);
		}
		

		FATFS fs;
    
    	/* Mount Card */
		FRESULT fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    sprintf(locbuf, "Error mounting card\n");
    	    send_to_output(active_user, locbuf);
    	    write_memory(active_user, FCBSTATUS, 1);
    	    return ;	
		}
		
		// remove file
		fr = f_unlink(filename);
		if (fr != 0) {
			sprintf(locbuf, "Delete Failed: fr=%d\n",fr);
			send_to_output(active_user, locbuf);
			write_memory(active_user, FCBSTATUS, fr);
			return ;
		};

		// unmount the card
    	f_unmount("0:");
    	write_memory(active_user, FCBSTATUS, 0);
		return ;
	}
	
	
	
	/* ******* OUT port 254 - save RAM to a named file ****** */
	// filename in FCB, Start of memory to save in DE, last memory location in HL
	if (port == 254) {
		// get addresses
		uint16_t file_start = (regs[active_user].D << 8) | (regs[active_user].E);
		uint16_t file_end = (regs[active_user].H << 8) | (regs[active_user].L);
		char locbuf[80];
		
		// printf("Start: %04X   End: %04X \n",file_start, file_end);
		
		if (file_end - file_start <= 0) {		// empty block
			write_memory(active_user, FCBSTATUS, 1);
			sprintf(locbuf, "Error - block to save is 0 bytes\n");
			send_to_output(active_user, locbuf);
			return;
		}
		
		// get filename
		char filename[13] = {'\0'};
		int c=0, i=0;

		// get filename from FCB
		for (c=0; c<8; c++) {
			if (read_memory(active_user, FILENAME + c) == '\0') continue;
			filename[i++] = read_memory(active_user, FILENAME + c);
		}
		filename[i++] = '.';		// save seperator
		
		// get filetype from FCB
		for (c=0; c<3; c++) {
			if (read_memory(active_user, FILETYPE + c) == '\0') continue;
			filename[i++] = read_memory(active_user, FILETYPE + c);
		}
		
		// printf("Filename: %s\n",filename);
		
		// mount SD card, open file for write, save block
		FATFS fs;
		FIL sdcard_file;
		FRESULT fr;
		UINT addr, bw;		// read count, write count
		uint8_t buffer[512];
    
    	/* Mount Card */
		fr = f_mount(&fs, "", 1);
		if (FR_OK != fr) {
    	    sprintf(locbuf, "Error mounting card - return code %d\n",fr);
    	    send_to_output(active_user, locbuf);
    	    write_memory(active_user, FCBSTATUS, 1);
    	    return ;	
		}
		
		// create file
		fr = f_open(&sdcard_file, filename, FA_CREATE_NEW | FA_WRITE);
		
		if (fr != 0) {
			sprintf(locbuf, "File Create Failed - return code %d\n",fr);
			send_to_output(active_user, locbuf);
			write_memory(active_user, FCBSTATUS, fr);
			f_unmount("0:");
			return;
		}
	
		while (1) {		// write loop
			memset(buffer,0,512);
			if (file_start >= file_end) break;
			if ((file_end - file_start) < 512) break;
			for (addr = 0; addr < 512; addr++) 
				buffer[addr] = read_memory(active_user, file_start + addr);
			fr = f_write(&sdcard_file, buffer, addr, &bw);
			file_start += 512;
		}
		
		// more then 0, but less than 512 bytes to save
		if (file_end - file_start > 0) {
			// write byte by byte
			for (int addr = file_start; addr < file_end; addr++)
				f_putc(read_memory(active_user, addr), &sdcard_file);
		}
		
		// memory block saved - close file
		f_close(&sdcard_file);
		
		// unmount the card
    	f_unmount("0:");
    	write_memory(active_user, FCBSTATUS, 0);
		return ;
	}
	
	sprintf(locbuf,"Error - calling non-existant port %d\n",port);
	send_to_output(active_user, locbuf);
	
	
}
