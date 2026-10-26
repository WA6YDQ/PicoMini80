;; OS1 for Kurts multi-user computer
;; (C) 2026 Kurt Theis
;; uses a superset of 8080 and PDP11 opcodes
;; runs on a pi-pico2 but should work on any system 
;; with the same hardware configuration.
;; Compiled with ASM80 assembler (Kurts assembler)
;;
;; version 0.5  Modified for emulator_3 memory changes
;;				removed stat command (did nothing useful)
;;				changed date command to not show buffer
;; version 0.4	Added access controls
;; version 0.3	Added all basic file and disk commands
;; version 0.2  basic functionality


conout	equ	0			;; console output port#
conin	equ	0			;; console input port#
DIRPORT	EQU		255		;; input port# to show the directory on the storage device
LOADFILE EQU	254		;; input port# loads a named file from storage device 
CR		equ	13
LF		equ	10
NULL	equ	0

;; ROMSTART is 1st address used by OS, equals RAMEND+1	
RAMEND 		EQU 0xEFFF
ROMSTART	EQU	RAMEND+1
STACK	EQU	RAMEND		;; 80 bytes for stack (40 levels), grows down

OSRAM	EQU	EE00h		;; 512 bytes below RAMEND used for buffers, stack etc

DIRMODE EQU  OSRAM+196	;; 0: dir wide   1: dir long
WARMBOOT EQU OSRAM+195	;; address to hold boot status: 0=cold boot, 1=warm boot
LOGNAME EQU OSRAM+162	;; (32 bytes) store name:pass for login command
TIMEBUF EQU OSRAM+82	;; 80 bytes
buffer	EQU OSRAM+1		;; 80 bytes
LOGSTAT	EQU	OSRAM		;; 00: not logged in, FF: logged in

						;; OS resides just above TPAEND 
OSSTART	equ	ROMSTART	;; start of OS routines
TPA		equ	100h		;; transiant program area Start address
TPAEND	equ EDFFh		;; end of TPA - need to subtract TPA from TPAEND for max program size (TPASIZE)
TPASIZE EQU ECFFh		;; TPA size

BOOT	EQU	0x0000
IOBYTE	EQU	0x0003

STARTSEED	EQU	5ah		;; initial seed for LFSR routine


; File Control Block 1 (5Ch thru 7Ch) FCB1 5C-7C (92d - 124d)
FCB				EQU	BOOT+5Ch
FCBDRIVECODE	EQU	FCB			; 1 byte for drive code (0-16) (0=default, 1=A, 2=B, 16=P)
FILENAME		EQU	FCB+1		; 8 char filename high bit 0 (default upper case)
FILETYPE		EQU	FCB+9		; 3 char file type in ascii, upper case
FCBEXTENT		EQU	FCB+12		; default 00h extent (EX) ranges from 0 to 31
FCBS1			EQU	FCB+13		; reserved by cp/m for bdos
FCBS2			EQU	FCB+14		; reserved for cp/m for bdos, typically set to 0
FCBRC			EQU	FCB+15		; record count, 0-128

;; disk map - allocation block numbers assigned to file extent. Internal BDOS map
;; FCB+16 thru FCB+31 (16 bytes)
FCBDN			EQU	FCB+16		; reserved, 0-128 typical values

;; these below will be overwritten by the BDOS allocation map. Consider them temporary
FCBCR			EQU	FCB+32		; current record, user usually sets to 0
FCBR0			EQU	FCB+33		; random record number, typ 0
FCBR1			EQU	FCB+34		; random record number, typ 0
FCBR2			EQU	FCB+35		; random record number, typ 0
FCBSTARTHI		EQU FCB+36		; 2 byte start address
FCBSTARTLO		EQU	FCB+37
FCBENDHI		EQU	FCB+38		; 2 byte end address
FCBENDLO		EQU	FCB+39
FCBSTATUS		EQU	FCB+40		; returned status byte from disk routines (0=OK, >0=failure)

;; FCB2 for routines needing a second filename (rename, copy, etc)
;; these will be overwritten by the BDOS
FILENAME2		EQU	FCB+41		; 8 char filename. high bit 0 (default upper case)
FILETYPE2		EQU	FCB+49		; 3 char file type in ascii, upper case

	; ---------------------------------
	; Start of System Memory at 0x0000
	; ---------------------------------
	org BOOT
	jmp OSSTART		;; warm boot (and cold boot on power up) jump point
	
	org	IOBYTE		;; i/o assignment byte, typically 0 (future use for CP/M)
	db 0
	
	org	0x0004		;; current drive, usually 0 (future use for CP/M)
	db 0
	
	org 0x0005		;; BDOS system call entry (future use for CP/M)
	jmp	BDOS
	
	;; CP/M Memory Structure (future use)
	; Top of Memory FFFF
	; BIOS	FA00
	; BDOS  EC00
	; CCP	DC00
	; TPA   0100
	; ZERO PAGE 0000
	
	; 0005 JMP instruction
	; 0006 2 byte address of BDOS (also 1 more then end of TPA)

;; ---------------------------------
;;         Main Code
;; ---------------------------------


	org OSSTART
	
	lda		WARMBOOT
	cpi		1	; if we already booted up once, don't go thru boot process
	jz		WBOOT		; jump to warm boot
	jmp		COLDBOOT	; else jump to cold boot to set things up

bootmessage	db	CR,LF,"OS1 version 0.5",CR,LF,NULL	
helpmessage db  "Type 'help' if needed.",CR,LF,NULL
PROMPT      DB  "OS1> ",NULL

COLDBOOT
	;; Start of COLD BOOT sequence
	mvi	a 0
	sta		LOGSTAT		; set login status to off

	
	; start running user interface
	lxi h	bootmessage
	call	puts
	lxi h	helpmessage
	call	puts
	jmp		WBOOT
	
;; ------------------	
;; Main Command Loop
;; ------------------
	
; ---- WARM boot
WBOOT
loop0
	mvi		a,1
	sta		WARMBOOT	
	
	lxi sp  STACK		;; set up stack
	mvi	a	CR
	call	putc
	mvi a	LF
	call	putc
	
	
CCP		; Console Command Processor (a la CP/M)

	; show prompt
	lxi h	PROMPT
	call	puts
	;;mvi a	'>'
	;;call putc
	
	; clear the buffer
	mvi c	80
	lxi h	buffer
	call	memset
	
	; read a line
	mvi c	80
	lxi h	buffer
	call	gets
	
	;; keep typed line from being overwritten
	mvi		a CR
	call	putc
	mvi		a LF
	call	putc
	
	
	
	; ------------------ OS1 Command Interpreter ----------------------------------------
	; Test user commands (built-in: ver, dir, new, clear, help, del, type, era, etc)
	; Any other command is assumed a disk program. An attempt will be made to load
	; and execute it. In CP/M terms this is the CCP (Console Command Processor)
	
	; OS1 has a very basic access control system. Nothing can be done until logging
	; in. After your work is done, you log out of the system by typing 'logout'.
	; To login in type 'login'. You will them be prompted for a user name.
	; After typing your user name and hitting <enter> you will be prompted for a
	; password. Type in your password and press <enter>. If your credentidals match
	; you will be granted access to the system. 
	; -----------------------------------------------------------------------------------
	
	; login  - test for the 'login' command
	lxi	d	login_cmd
	lxi h	buffer
	call	strcmp
	cpi		0
	jz		login_user	
	
	lda		LOGSTAT		; test for 00 (not logged in) or FF (logged in)
	cpi	ffh
	jz		CCP1
		
	;; not logged in yet
	lxi	h	LOGMSG0
	call	puts
	jmp		CCP
	
	
CCP1		;; logged in entry loop

	; test for <cr> only
	lxi h	buffer
	mov a,m
	cpi 	NULL
	jz		CCP	
	cpi		CR
	jz		CCP


	; 'logout' - log off the system
	lxi	d	logout_cmd
	lxi	h	buffer
	call strcmp
	cpi		0
	jz		logout_user


	; 'ver' - show version
	lxi d	ver_cmd
	lxi h	buffer
	call	strcmp
	cpi		0
	jz		show_version
	
	
	; run - start execution at TPA start
	lxi	d	run_cmd
	lxi	h	buffer
	call	strcmp
	cpi		0
	jz		100h
	
	
	; 'type' - load a disk file to ram and display it
	lxi	d	type_cmd
	lxi	h	buffer
	call	strcmp
	cpi		0
	jz		type_file
	
	
	; 'dir' - show disk directory (wide mode, 5 cols)
	lxi d	dirw_cmd
	lxi h	buffer
	call	strcmp
	cpi 	0
	jz		show_dirw
	
	; 'dirl' - shopw long version of dir listing
	lxi d	dirl_cmd
	lxi h	buffer
	call	strcmp
	cpi 	0
	jz		show_dirl
	
	
	; 'new' - clear ram from TPA thru TPAEND
	lxi d	new_cmd
	lxi h	buffer
	call	strcmp
	cpi		0
	jz		new
	
	
	; 'clear' the screen
	lxi d	cls_cmd
	lxi h	buffer
	call	strcmp
	cpi		0
	jz		clrscr
	
	
	; 'help' - show help
	lxi d	help_cmd
	lxi h	buffer
	call	strcmp
	cpi		0
	jz		show_help
	
	; 'del' - delete a disk file
	lxi	d	del_cmd
	lxi h	buffer
	call	strcmp
	cpi		0
	jz		delete_file
	
	; 'era' - delete a disk file (same as del)
	lxi	d	era_cmd
	lxi h	buffer
	call	strcmp
	cpi		0
	jz		delete_file
	
	; 'create' - create an empty file
	lxi	d	create_cmd
	lxi h	buffer
	call	strcmp
	cpi		0
	jz		create_file
	
	; 'date' - display time/date on users console
	lxi d	date_cmd
	lxi h	buffer
	call	strcmp
	cpi 	0
	jz		show_daytime
	
	; 'copy' - copy file1 to file2
	lxi d	copy_cmd
	lxi	h	buffer
	call	strcmp
	cpi		0
	jz		copy_file
	

	
	
	;; assume the command is a program name
	;; attempt to load and execute it
	
	
	
	
	; clear existing filename
	call	clear_fcb
	
	; clear ram space	
	call	clear_tpa
	
	; take filename and fileext from buffer, store in fcb
	call	get_filename
	
	; test the return value. 0=OK, 1=error
	cpi		1			; filename not on disk
	jz		err1		; stop here on error
	
	; file on disk -  load into RAM from disk
	call	load_diskfile	
	lda		FCBSTATUS	; get result of load operation
	cpi		0			; A = 0: operation successful
	jnz		err1
	
	; now run code starting at TPA
	jmp 	100h
	

	
;; --------------------------------	
err1	
	;; bad command name or bad file name - show error
	lxi h	error_msg
	call	puts
	; show bad file
	mvi a	'"'
	call	putc
	lxi	h	buffer
	call	puts
	mvi a	'"'
	call	putc
	call	crlf
	jmp		CCP	
error_msg	DB "Bad Command or File Name: ",NULL	

; set up seed for lfsr routine
SEED	DB	STARTSEED		


; ----------------------------------------------------
; ---------- This is the OS command List -------------
; ----------------------------------------------------
login_cmd	DB "login",NULL
; log the user in

logout_cmd	DB "logout",NULL
; log the user out

ver_cmd		DB "ver",NULL		
; runs show_version

run_cmd		DB "run",NULL
; jumps to TPA start 

dirw_cmd	DB "dir",NULL
; runs show_dir

dirl_cmd	DB "dirl",NULL
; runs show_dir

new_cmd		DB "new",NULL
; runs 'new' routine Clear TPA

cls_cmd		DB "clear",NULL
; runs clr_scr Clear the Screen

help_cmd	DB "help",NULL
; runs show_help

type_cmd	DB "type",NULL
; runs type_file Display disk text file

del_cmd		DB "del",NULL
; runs delete_file Delete Disk File

era_cmd		DB "era",NULL
; runs delete_file Delete Disk File

ren_cmd		DB "ren",NULL
; runs rename_file Rename Disk File

create_cmd	DB "create",NULL
; runs create_file Create an empty Disk File

date_cmd	DB "date",NULL
; display date/time

copy_cmd	DB	"copy",NULL
; copy file1.ext file2.ext on disk



;; --------------------------------------------------
;; OS Commands and Routines
;; --------------------------------------------------

; ---------------------------------
logout_user	; log user off the system
	lxi	h	LOGMSG4
	call	puts
	mvi a	0
	sta		LOGSTAT
	jmp		CCP

; --------------------------
login_user		; user typed 'login<cr>'   now log the user in
	; clear LOGNAME
	mvi c	32		; counter
	mvi a	NULL
	lxi	h	LOGNAME
login0
	mov	m,a
	inx h
	dcr c
	jnz		login0
	
	; ask for user name
	; saved user name/password in LOGNAME
	lxi	h		LOGMSG1
	call		puts
	
	; clear the buffer
	mvi c	80
	lxi h	buffer
	call	memset
	
	; get a user name
	
	; read a line
	mvi c	16			; 16 chars max
	lxi h	buffer
	call	gets
	
	;; store user name in LOGNAME
	lxi h	buffer
	lxi d	LOGNAME
login1		; save the user name
	mov	a,m
	cpi		NULL
	jz		login2
	stax	d
	inx	h
	inx	d
	jmp		login1
	
login2
	; get a password
	
	; ask for passowrd
	lxi	h	LOGMSG2
	call	puts
	
	; clear the buffer
	mvi c	80
	lxi h	buffer
	call	memset
	
	; read a line
	mvi c	16			; 16 chars max
	lxi h	buffer
	call	gets
	
	;; combine user name and password
	lxi	h	buffer
login3		; save the password
	mov	a,m
	cpi		NULL
	jz		login4
	stax d
	inx h
	inx	d
	jmp		login3

login4
	;; test user:password
	in 253		;; password test against list
	sta		LOGSTAT		; save respomse
	cpi	0
	jnz		login5
	
	;; password failed
	lxi h	LOGFAIL
	call	puts
	jmp		CCP

login5
	; welcome the user
	lxi	h	LOGMSG3
	call	puts
	
	; run any login scripts
	;;
	
	jmp		CCP
	
LOGMSG0		DB "Login Please",CR,LF,NULL
LOGMSG1		DB "User: ",NULL
LOGMSG2		DB CR,LF,"Password: ",NULL
LOGMSG3		DB CR,LF,"Welcome!",CR,LF,NULL
LOGMSG4		DB CR,LF,"Goodbye!",CR,LF,NULL
LOGFAIL		DB CR,LF,"Bad Login/Password",CR,LF,NULL
	
; -----------------------------
show_daytime	; read rtc via out port, display date/time

	mvi a	0
	out		251		;; buffer filled via DMA and time displayed
	jmp 	CCP
	
	; below works, but is ignored
	; now show memory location
	lxi		h TIMEBUF
s_day0
	mov		a,m
	cpi		NULL
	jz		s_day1
	call	putc		; print the buffer until NULL
	inx		h
	jmp		s_day0
	
s_day1	; finished
	mvi		a CR
	call	putc
	mvi		a LF
	call	putc
	jmp		CCP





; ----------------------------------
copy_file	; copy a disk file: copy file1.ext file2.ext
	
	call	get_filename2
	out		250			; run copy routine
	
	lda		FCBSTATUS
	cpi		0
	jz		copy_file_OK
	
	; failed
	lxi		h	COPYFAIL
	call	puts
	jmp		CCP
	
copy_file_OK
	lxi		h	COPYOK
	call	puts
	jmp		CCP
	
COPYFAIL	DB "File Copy Failed",CR,LF,NULL
COPYOK		DB "File Copied",CR,LF,NULL




; ----------------------------
create_file ; create an empty disk file

	; clear any old filename data
	call	clear_fcb
	
	; get the file name to create
	; point to buffer, skip past 'type'
	lxi h	buffer
crf1	inx		h
	mov		a,m
	cpi		' '
	jnz		crf1
	;; at space after 'type'
crf2	inx		h		; skip 1st space
	mov		a,m
	cpi		' '
	jz		crf2		; look for real character

	; take filename.ext from buffer, store in fcb
	call	get_fname	;; fname - special case where HL is already set
	
	; test the return value. 0=OK, 1=error
	cpi		1
	jz		err1		; stop here on error
	
	; TPA is set, call routine via out 252 to create the file
	; whose name is in FCB
	
	out		252
	lda		FCBSTATUS		; test status
	cpi		0
	jnz		crf3
	; operation OK
	lxi		h	create_msg1
	call	puts
	jmp		CCP
	
crf3	; operation failed
	lxi		h	create_msg2
	call	puts
	jmp		CCP

create_msg1		DB "File Created",CR,LF,NULL
create_msg2		DB "File creation Failed",CR,LF,NULL


; -----------------------------
delete_file ; delete a file on disk

	lxi		h	delete_msg3
	call	puts
	
	; clear any old filename data
	call	clear_fcb	
	
	; get the file name to delete
	; point to buffer, skip past 'type'
	lxi h	buffer
df1	inx		h
	mov		a,m
	cpi		' '
	jnz		df1
	;; at space after 'type'
df2	inx		h		; skip 1st space
	mov		a,m
	cpi		' '
	jz		df2		; look for real character

	; take filename.ext from buffer, store in fcb
	call	get_fname	;; fname - special case where HL is already set
	
	; test the return value. 0=OK, 1=error
	cpi		1
	jz		err1		; stop here on error
	
	; TPA is set, call routine via out 253 to delete the file
	; whose name is in FCB
	
	out		253
	lda		FCBSTATUS		; test status
	cpi		0
	jnz		df3
	; operation OK
	lxi		h	delete_msg1
	call	puts
	jmp		CCP
	
df3	; operation failed
	lxi		h	delete_msg2
	call	puts
	jmp		CCP
	
delete_msg1		DB "File deleted",CR,LF,NULL
delete_msg2		DB "Delete operation Failed",CR,LF,NULL
delete_msg3		DB "deleting file",CR,LF,NULL	


; --------------------------------------------
show_version
	lxi h	bootmessage
	call	puts
	jmp		CCP	



; ---------------------------------------------
show_dirw		;; wide mode
	mvi a	0
	jmp		show_dir1

show_dirl		;; long mode
	mvi a	1
	jmp		show_dir1

show_dir1
	sta		DIRMODE
	in 		0xff
	mvi a	CR
	call	putc
	mvi a	LF
	call	putc
	jmp		CCP	
SHOWDIR	DB	CR,LF,"Directory Listing",CR,LF,NULL




; ------------------------------------
new	; Clear the TPA memory (also called by load etc)
	call	clear_tpa
	jmp		CCP	
	
clear_tpa	; clear user ram space
	lxi h	TPA
	lxi d	TPASIZE	;TPAEND-TPA
	mvi a	0
c_tpa1
	mov	m,a
	inx h
	dcr e
	jnz		c_tpa1
	dcr	d
	jnz		c_tpa1
	ret


; --------------------------------------
clrscr	; clear the console display
	call	clear_screen
	jmp		CCP	



; ---------------------------------
show_help
	; display help information
	lxi	h	HELPLIST
	call	puts
	jmp		CCP	

HELPLIST	DB CR,LF
			DB "** Built In Commands **",CR,LF
			DB "----------------------------------------------------",CR,LF
			DB "login   - log into the system",CR,LF
			DB "logout  - log out of the system",CR,LF
			DB "new     - clear user memory",CR,LF
			DB "ver     - show version",CR,LF
			DB "run     - restart program loaded in memory",CR,LF
			DB "clear   - clear the screen",CR,LF
			DB "dir     - show the disk directory (wide display)",CR,LF
			DB "dirl    - show the disk directory (detailed)",CR,LF
			DB "type    - display contents of a text file",CR,LF
			DB "del     - delete a disk file (del file)",CR,LF
			DB "era     - delete a disk file (era file) (CP/M compatibility)",CR,LF
			DB "create  - create an empty disk file",CR,LF
			DB "ren     - rename a disk file (ren old new)",CR,LF
			DB "copy    - copy a file (copy old new)",CR,LF
			DB "date    - display time and date",CR,LF
			DB NULL
			

; ------------------------------------------------------		
type_file
	; load a text file to TPA then display a page at a time
	; (akin to unix more command or ms-dos type command)
	
	; clear any old filename data
	call	clear_fcb	
	
	; get the file name to display
	; point to buffer, skip past 'type'
	lxi h	buffer
tf1	inx		h
	mov		a,m
	cpi		' '
	jnz		tf1
	;; at space after 'type'
tf2	inx		h		; skip 1st space
	mov		a,m
	cpi		' '
	jz		tf2		; look for real character

	; take filename.ext from buffer, store in fcb
	call	get_fname	;; fname - special case where HL is already set
	
	; test the return value. 0=OK, 1=error
	cpi		1
	jz		err1		; stop here on error
	
	; routine to load into RAM from disk
	call	load_diskfile	
	lda		FCBSTATUS	; get result of load operation
	cpi		0			; A = 0: operation successful
	jnz		err1
	
	; FCBENDHI and FCBENDLO hold last read address after load_diskfile is run
	
	; HL holds location of text to display
	lxi		h	TPA		; location of file in memory
	dcx		h			; point to 1 less (end of file check is easier)
	
tf30
	; set or reset row/col counters
	mvi		b	ROWDEF		; number of rows to show
	mvi		c	COLDEF		; number of columns to show, resets at CR
	
tf3
	inx		h		; next address
	lda		FCBENDHI
	cmp		h
	jnz		tf3b
	lda		FCBENDLO
	cmp		l
	jnz		tf3b
	; and end of the file
	call	crlf
	jmp		CCP		; end of file reached - we're done
	
tf3b	
	mov		a,m		; get a char from (HL) and show it.
	call 	putc
	cpi		CR		; set col ctr to 0
	jz		tf3a	; reset col to 0, but decrement row counter
	cpi		LF		; same as CR
	jz		tf3a
	;
	dcr		c
	jnz		tf3

	
tf3a	; col at 0 
	mvi		c,COLDEF	; reset col counter
	dcr		b		; row counter
	jnz		tf3
	
tf4	; printed n rows - ask user for next page or quit
	push	h
	lxi		h next_page_msg
	call	puts
	pop		h
tf4a ; get user response
	in		conin
	cpi		feh
	jz		tf4a
	cpi		'q'
	jz		tf5		; quit
	cpi		CR
	jnz		tf4a
	

	; got CR - clear the prompt, print another page
	call	clear_line	; clear the screen
	jmp		tf30			; reset row/col to default, then resume showing page 

tf5	; user wants to quit - clear the message and return to loop
	call	clear_line
	call	crlf
	jmp		CCP	
 
	 
ROWDEF	EQU	20
COLDEF	EQU	79
next_page_msg	DB CR,LF,"Press <cr> for More, Q to Quit",NULL	
	
	
;; -------------------------	
;; ---- OS1 OS Subroutines ----
;; -------------------------
	
	
; -------------------------------
crlf	; send a CR LF to console
		push	psw
		mvi		a	CR
		call	putc
		mvi		a	LF
		call	putc
		pop		psw
		ret
	

; ------------------------------------
clear_screen
	; clear the screen
	mvi	a	0x1b	; esc
	call	putc
	mvi a	'['
	call	putc
	mvi a	'2'
	call	putc
	mvi a	'J'
	call	putc
	; home cursor
	mvi a	0x1b
	call	putc
	mvi a	'['
	call	putc
	mvi a	'H'
	call	putc
	ret
	

; ----------------------------------
clear_line	; clear the current screen line	
	push	psw
	push	b
	mvi 	c 79
	mvi		a CR
	call	putc
cl1	
	mvi		a ' '
	call	putc
	dcr		c
	jnz		cl1
	mvi		a CR
	call	putc
	pop		b
	pop		psw
	ret
	
	
; -------------------------------------------
putc
	; output a character in A to console
	out		conout
	ret
	
	
; ------------------------------------------	
puts
	; output a null terminated string
	; addressed by HL
	mov a,m
	cpi		NULL
	rz
	cpi		'$'		; for cp/m compatability
	rz
	call putc
	inx h
	jmp	puts
	
	
	
; -----------------------------------------
memset
	; set a block of memory to NULL
	; HL points to block, c is the counter
	mvi a,NULL
	mov m,a
	dcr c
	cmp c
	rz
	inx h
	jmp memset
	

; ------------------------------------
getc	; blocking input: get/return a single char

	in	conin
	cpi	feh
	rnz
	jmp	getc	
	
; ------------------------------------------------------------	
gets
	; blocking character input - get a string terminated by \r into the buffer
	; read max (c) chars from console (conin)
	; chars are stored on (HL) (HL preset by calling routine)
	; c is a countdown counter (preset by calling routine to 80)
	; termination is \r
	; last char in buffer is NULL (\r is NOT saved)

	
	call	getc	; get a char
	
	; test backspace
	cpi		08h		; backspace
	jnz		gets1
	
	; got BS
	mvi a	80		;; test if we are at buffer start
	cmp	c
	jz		gets	; we are - do nothing
	
	; delete last char
	dcx h			; point back 1 in buffer
	mvi	a,NULL		; save NULL in buffer
	mov	m,a
	inr	c			; increment counter
	; clear char on display
	mvi a	08h		;; show backspace
	call	putc
	mvi a	' '		;; blank the char under cursor
	call	putc
	mvi a	08h
	call	putc
	jmp		gets

gets1
	; test ctrl-U (delete line)
	cpi 15h		; ^U
	jnz	gets2
	
	lxi h	buffer		; restore pointers
	mvi c	80
	call	memset
	lxi h	buffer
	mvi c	80
	mvi a	'^'
	call	putc
	mvi a	'U'
	call	putc
	call	crlf
	mvi a	'>'
	call	putc
	jmp		gets

gets2
	; test ctrl-C (warm boot)
	cpi	03h
	jnz	gets_save
	; clear buffer 
	lxi h	buffer
	mvi c	80
	call 	memset
	mvi a	'^'
	call	putc
	mvi a	'C'
	call	putc
	jmp	WARMBOOT

gets_save	
	call	putc	; echo char
	cpi	CR
	rz
	
	; save to buffer
	mov m,a
	inx h
	dcr c
	mov a,c
	cpi 0
	jnz	gets
	;; when counter = 0, ignore last char, wait for \r
	inr c
	dcx h
	jmp		gets
	
	
	
; ----------------------------------------------------------------
; strcmp - compare strings from DE and HL. Return 0 on success
strcmp		; DE - predefined string,  HL - string under test
	ldax 	d
	cpi		0
	jz		strcmp2		; NULL in predefined string - end of string
	cmp		m
	jnz		strcmp1		; no match
	inx		d
	inx		h
	jmp		strcmp		; loop until mis-match
	;

strcmp2		; got NULL in pre-defined string. Test HL for NULL or space
	mov 	a,m
	cpi 	0			;; NULL
	jz		strcmpend
	cpi		' '			;; space
	jz		strcmpend

strcmp1		; no match
	mvi a	1
	ret

strcmpend
	mvi a	0		; match - return 0
	ret
	
; -----------------------------------
; 8 bit LFSR subroutine 	
; Input:  None (reads from memory variable 'SEED')
; Output: A = Next pseudo-random byte
; Destroys: A, B, F (flags)
; use: set SEED (if starting sequence), push AF,BC, call lfsr, 
; random number returned in A, pop BC, AF
lfsr	lda	SEED
		mov	b,a		;; save for later
		
		; shift the register right by 1 bit
		; rotate right (RRC) and mask out the MSB
		rrc
		ani	7fh		; clear the MSB
		sta	SEED
		
		; test the bit just shifted out
		mov	a,b		; restore original state to test bit 0
		ani	01h
		rz			; if LSB is 0,  no feedback needed
		
		; apply feedback if shifted out bit was 1
		lda	SEED
		xri	b0h		; gaslois feedback
		sta	SEED
		ret
	
	
	
; --------------------------------------------------
; subroutine showbytehex: display value in A as 2 digit hex
showbytehex
	push 	b		; save BC
	mov 	b,a		; save A
	ani 	f0h		; mask off lsb
	rrc
	rrc
	rrc
	rrc			; shift A right 4 bits
	adi 	30h		; convert to ascii
	cpi		3ah		; test for > 39h
	jc		sb1
	adi 	07h		; convert to A-f	
sb1
	call	putc	;;out		CON		; show it
	mov 	a,b		; restore A
	ani		0fh		; mask off MSB
	adi 	30h		; convert to ascii
	cpi 	3ah		; test for > 39h
	jc		sb2
	adi 	07h
sb2
	call	putc	;;out 	CON
	pop 	b		; restore BC
	ret
	
; ---------------------------------------------
; subroutine to clear the FCB for later use
clear_fcb
	mvi 	c 52		; size of FCB1 thru FILETYP2
	mvi 	a 0
	push h
	lxi 	h FCB
cf1	;
	mov		m,a
	inx		h
	dcr		c
	jnz		cf1
	; done
	pop h
	ret	
	
; -----------------------------------------------------------------
; subroutine get_filename: extract filename/filetype from buffer, place in FCB
get_filename
	lxi h	buffer		; filename is here
get_fname				; call here if hl is preloaded
	lxi	d	FILENAME	; filename storage in FCB
	mvi c	9
	; scan buffer until '.'
gf1	mov		a,m
	cpi		'.'			; end of filename denoted by '.' or \r
	jz		gf2
	cpi		NULL		;; end of word in buffer is NULL
	jz		gf6
	stax	d			; save char in fcb
	inx		h
	inx		d
	dcr		c			; error if filename exceeds 8 chars
	jz		gf_error
	jmp		gf1
gf2			; now save extension
	inx 	h			; skip '.' in buffer
	lxi		d	FILETYPE
	mvi 	c 4
	; first, test for NULL. If true, set extension to com and return
	mov 	a,m
	cpi		NULL
	jz		gf6
gf4	mov		a,m		; get extension from buffer
	cpi		NULL
	jz		gf5		; end of extension
	cpi		' '
	jz		gf5		; end of extension		
	stax	d		; store in FCB
	inx		h
	inx		d
	dcr		c
	jz		gf_error
	jmp		gf4
gf5	; done extracting filename
	mvi 	a	0	;; return OK
	ret
gf6 	; no extension given, so use .com as extension
	lxi		h FILETYPE
	mvi 	a	'c'
	mov		m,a
	inx 	h
	mvi 	a	'o'
	mov		m,a
	inx		h
	mvi		a	'm'
	mov		m,a
	ret		; done
	
gf_error	; filename or extension bad
	lxi		h	GF_MSG
	call	puts
	mvi 	a 1		;; return error		
	ret
GF_MSG	DB	CR,LF,"Bad Filename",CR,LF,NULL


; ----------------
get_filename2		; get 2 filenames
	lxi		h	buffer
		
get_filename1

	call	clear_fcb	; first clear the FCB
		
gfname1		; skip past command name in buffer
	mov		a,m
	cpi		NULL		; error - NULL after command
	jz		gfname_error
	cpi		' '
	jz		gfname2		; pointing to space
	inx		h
	jmp		gfname1
	
gfname2		; HL points to space - jump to 1st non-space
	mov		a,m
	cpi		NULL
	jz		gfname_error
	cpi		' '
	jnz		gfname3
	inx		h
	jmp	gfname2
	
gfname3		; pointing to ascii char
	lxi		d	FILENAME	; get 1st filename

gfname4
	mov		a,m		; get char from filename
	cpi		'.'
	jz		gfname5	; name.ext seperator
	cpi		NULL	; user didn't type .com - add it
	jz		gfname11
	stax	d		; save to FCB
	inx		h
	inx		d
	jmp		gfname4
	
gfname5	
	inx		h	; skip seperator
	lxi		d	FILETYPE
	
gfname6		; get extension
	mov		a,m
	cpi		NULL
	rz		; filename and extension saved
	cpi		' '		; another filename - get it
	jz		gfname7
	stax	d		; save char to FILETYPE
	inx		h
	inx		d
	jmp		gfname6
	
gfname7		; another filename - save in FILENAME2 and FILETYPE2
	lxi		d 	FILENAME2
	inx		h	; skip space
	mov		a,m		; get char
	cpi		' '
	jz	gfname7		; skip spaces 
	
gfname8		; same as gname4
	mov		a,m		; get char from filename
	cpi		'.'
	jz		gfname9	; name.ext seperator
	stax	d		; save to FCB
	inx		h
	inx		d
	jmp		gfname8
	
gfname9		; get 2nd extension
	inx		h		; skip '.'
	lxi		d	FILETYPE2
	
gfname10	; same as gfname6
	mov		a,m
	cpi		NULL
	rz		; filename and extension saved
	stax	d		; save char to FILETYPE
	inx		h
	inx		d
	jmp		gfname10
	
gfname11 ; user didn't use an extension - add com to FILETYPE1
	lxi		d	FILETYPE
	mvi		a	'c'
	stax	d
	inx		d
	mvi		a	'o'
	stax	d	
	inx		d
	mvi		a	'm'
	stax	d
	ret		; routine finished
	
gfname_error	; bad formatting 
	lxi		h	gfname_msg
	call	puts
	ret
	
gfname_msg	DB "Bad Filename Format",CR,LF



; ----------------------------------------------
; subroutine to load a file named in TPA from disk
load_diskfile	; filename in FCB	
	mvi 	a 0
	sta		FCBSTATUS	; initalize status to OK
	in		254			; operation to load in a file
	; status placed in FCBSTATUS and A
	ret
	
;-----------------------------------------------------------------
;#################### End Of OS1 Subroutines #####################
;-----------------------------------------------------------------


; ---------- CP/M BIOS Jump Table -----------------------

	;; CP/M BIOS Jump Table (for future CP/M compatability)
	;; This is machine specific to this particular implimentation
	;; This table is called by the BDOS routines
	;; The typical address start is FC00
;JUMPTABLE               ; offset	description
;	jmp		BOOT		; 0  		coldboot	0
;	jmp		WBOOT		; 3  		warmboot	1
;	jmp		CONST		; 6			console status (0xff is char ready, 0x00 if not)  2
;	jmp		CONIN		; 9			blocking, read a char from the console  3
;	jmp		CONOUT		; 12		write a char to the console  4
;	jmp		LIST		; 15		write a char to list device (printer)  5
;	jmp		PUNCH		; 18		write a char to paper tape punch  6
;	jmp		READER		; 21		read a char from paper tape reader  7
;	jmp		HOME		; 24		move disk head to track 0  8
;	jmp		SELDISK		; 27		select active disk drive  9
;	jmp		SETTRK		; 30		set track number for next disk operation  10
;	jmp		SETSEC		; 33		set sector number for next disk operation  11
;	jmp		SETDMA		; 36		set disk memory buffer transfer address  12
;	jmp		READ		; 39		read a single 128 byte sector from the disk
;	jmp		WRITE		; 42		write a single 128 byte sector to the disk
;	jmp		LISTST		; 45		check printer status
;	jmp		SECTRAN		; 48		translate logical sector to physical sector





; - --------------- CP/M BDOS Routines ------------------
BDOS	;; this is the routine that comes from a jump a 0005h with the function in C
	mov		a,c
	;; Console and System Functions
	cpi		0
	jz		WBOOT	; SYSTEM_RESET
	cpi		1
	jz		CONIN	; CONSOLE_INPUT
	cpi		2
	jz		CONOUT	; CONSOLE_OUTPUT
	cpi		3	
	jz		READER	; READER_INPUT
	cpi		4
	jz		PUNCH	; PUNCH_OUTPUT
	cpi		5
	jz		LIST	; LIST_OUTPUT
	cpi		6
	jz		DIRECT_CON_IO
	cpi		7
	jz		GET_IO_BYTE
	cpi		8
	jz		SET_IO_BYTE
	cpi		9
	jz		PRINT_STRING
	cpi		10
	jz		READ_CON_BUFFER
	cpi		11
	jz		GET_CON_STATUS
	cpi		12
	jz		RETURN_VERSION
	
	;; File System and Drive Functions
	;; DE must point to FCB (preset with values)
	
	cpi		13
	jz		RESET_DISK
	cpi		14
	jz		SELECT_DISK
	cpi		15
	jz		OPEN_FILE
	cpi		16
	jz		CLOSE_FILE
	cpi		17
	jz		SEARCH_FIRST
	cpi		18
	jz		SEARCH_NEXT
	cpi		19
	jz		DELETE_FILE
	cpi		20
	jz		READ_SEQUENTIAL
	cpi		21
	jz		WRITE_SEQUENTIAL
	cpi		22
	jz		MAKE_FILE
	cpi		23
	jz		RENAME_FILE
	cpi		24
	jz		GET_LOGIN_VECTOR
	cpi		25
	jz		GET_CURR_DISK
	cpi		26
	jz		SET_DMA_ADDRESS
	cpi		27
	jz		GET_ALLOC_ADDR
	cpi		28
	jz		WRITE_PROTECT
	cpi		29
	jz		GET_RO_VECTOR
	cpi		30
	jz		SET_FILE_ATTR
	cpi		31
	jz		GET_DPB_ADDR
	cpi		32
	jz		GET_SET_USER
	cpi		33
	jz		READ_RANDOM
	cpi		34
	jz		WRITE_RANDOM
	cpi		35
	jz		COMPUTE_FILE_SZ
	cpi		36
	jz		SET_RAND_RECORD
	
	;; for file operations returning an index in A:
	;; 0xff Operation Failed
	;; 0x00, 0x01, 0x02, 0x03 Operation successed
	
	
;; CP/M 80 routines live here

CONIN	ret

CONOUT	ret

READER	ret

PUNCH	ret

LIST	ret

DIRECT_CON_IO ret

GET_IO_BYTE ret

SET_IO_BYTE ret

PRINT_STRING ret

READ_CON_BUFFER ret

GET_CON_STATUS ret

RETURN_VERSION ret

RESET_DISK ret

SELECT_DISK ret

OPEN_FILE	ret

CLOSE_FILE ret

SEARCH_FIRST ret

SEARCH_NEXT ret

DELETE_FILE ret

READ_SEQUENTIAL ret

WRITE_SEQUENTIAL ret

MAKE_FILE ret

RENAME_FILE ret

GET_LOGIN_VECTOR ret

GET_CURR_DISK ret

SET_DMA_ADDRESS ret

GET_ALLOC_ADDR ret

WRITE_PROTECT ret

GET_RO_VECTOR ret

SET_FILE_ATTR ret

GET_DPB_ADDR ret

GET_SET_USER ret

READ_RANDOM ret

WRITE_RANDOM ret

COMPUTE_FILE_SZ ret

SET_RAND_RECORD ret




	

