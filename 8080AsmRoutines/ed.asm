; ed.asm - simple text editor
; (C) 2026 Kurt Theis
; written for the 8080

; LINE - 80 char line buffer

; commands:
; a - append text to buffer
; p - list buffer
; q - quit ed
; s - save buffer to disk
; l - load a disk file to buffer
; c - clear buffer
; h - display command list
; d - delete a line
; r - replace a line
; i - insert a line

conout		EQU	0
conin		EQU	0
CR			EQU	13
LF			EQU	10
ESC			EQU	1bh
NULL		EQU	0
WARMBOOT 	EQU 0
BOOT		EQU	0

; File Control Block 1 (5Ch thru 7Ch) FCB1 5C-7C (92d - 124d)
FCB				EQU	BOOT+5Ch
FCBDRIVECODE	EQU	FCB			; 1 byte for drive code (0-16) (0=default, 1=A, 2=B, 16=P)
FILENAME		EQU	FCB+1		; 8 char filename high bit 0 (default upper case)
FILETYPE		EQU	FCB+9		; 3 char file type in ascii, upper case
FCBEXTENT		EQU	FCB+12		; default 00h extent (EX) ranges from 0 to 31
FCBS1			EQU	FCB+13		; reserved by cp/m for bdos
FCBS2			EQU	FCB+14		; reserved for cp/m for bdos, typically set to 0
FCBRC			EQU	FCB+15		; record count, 0-128
FCBSTATUS		EQU	FCB+40		; returned status byte

BUFFERSIZE EQU C000h	; 48K bytes

	org	0100h
start
	mvi a	0			; mode 0=command, 1=append, 2=insert
	sta		MODE
	lxi	h	STARTMSG
	call	puts		; start up message
	
	lxi h	BUFFERSTART	; start by clearing the buffer
	lxi d	BUFFERSIZE
	call	clear_block
	lxi h	CLEARMSG
	call	puts
	
	lxi h	BUFFERSTART	; begin of text entry buffer
	shld	CURBUFFERPOS	; current position of the buffer
	
	
	
loop
	mvi a	0		; ed> prompt
	sta		MODE
	call	showprompt
	
	lxi h	LINE
	mvi c	80
	call	memset	; clear the line buffer
	lxi h	LINE
	mvi c	80
	call	gets	; read a line
	
	; point to LINE start, test commands
	lxi h	LINE
	mov a,m
	
	cpi	NULL
	jz	cronly			; user hit <cr> 
	
	cpi	'q'				; exit program
	jz	WARMBOOT		
	
	cpi	'a'				; append to buffer
	jz	append
	
	cpi 'b'				; show buffer status
	jz	buffer_stats	
	
	cpi	'p'				; print (list) buffer
	jz	list
	
	cpi	's'				; save buffer to disk
	jz	save
	
	cpi	'l'				; load file to buffer
	jz	load
	
	cpi	'h'				; show help
	jz	help
	
	cpi	'c'				; clear buffer
	jz	clear_buffer
	
	cpi	'd'				; delete a line
	jz	delete
	
	cpi	'i'				; insert a line
	jz	insert
	
	cpi 't'
	jz 	test
	
	; bad command?
	lxi h	ERRORMSG
	call	puts
	call	crlf
	jmp		loop


cronly		; user hit <cr>
	call	crlf
	jmp		loop





STARTMSG DB "Editor V1",CR,LF
		 DB	"Press 'h' for command list",CR,LF,NULL
		 
ERRORMSG DB CR,LF,"EH?",NULL
CLEARMSG DB "Buffer Cleared",CR,LF,NULL

; ----------- COMMANDS --------------



; ----------------------------------------------------
help
	call	crlf
	lxi h	HELPMSG
	call	puts
	jmp		loop
	
HELPMSG	DB "commands:",CR,LF
		DB " a - append text to buffer",CR,LF
		DB " b - show buffer status",CR,LF
		DB " c - clear buffer",CR,LF
		DB " d [n] - delete a line",CR,LF
		DB " *f - append file to buffer end",CR,LF
		DB " h - display this list",CR,LF
		DB " i [n] - insert a line",CR,LF
		DB " l - load a disk file to buffer (clears existing)",CR,LF
		DB " p - print buffer",CR,LF
		DB " q - quit ed",CR,LF
		DB " *r [n] - replace a line",CR,LF
		DB " s - save buffer to disk",CR,LF
		DB NULL
		


; --------------------------------------------
test
	call	crlf
	jmp		loop


; ---------- buffer stats -----------
buffer_stats	; show buffer stats (size, position, etc)
	
	; show buffer start address
	lxi h	BUFSTAT1
	call	puts
	lxi h	BUFFERSTART
	mov a,h
	call	printhex
	mov a,l
	call	printhex
	
	call	crlf
	
	; show buffer position
	lxi h	BUFSTAT2
	call	puts
	lhld	CURBUFFERPOS
	mov a,h
	call	printhex
	mov a,l
	call	printhex
	
	call	crlf
	jmp		loop




BUFSTAT1 DB "Buffer Start Address ",NULL
BUFSTAT2 DB "Current End of Buffer Position ",NULL


; ----------------------------------------------------
clear_buffer
	; fill the used buffer with NULL and resetting pointers
	call	crlf
	
	lxi h	BUFFERSTART
	lxi d	BUFFERSIZE
	call	clear_block
	; reset pointers
	lxi h	BUFFERSTART
	shld	CURBUFFERPOS
	
	lxi h	CLEARMSG
	call	puts
	
	jmp		loop
	
	

	; lxi h	BUFFERSTART
	; lxi d	BUFFERSIZE
	
	; DE - HL, result in DE (after xchg)
	;mov a,e		; low byte of BUFFERSIZE to A
	;sub l		; subtract low byte of BUFFERSTART
	;mov l,a		; save
	
	;mov a,d		high byte of BUFFERSIZE to A
	;sbb h		; subtract high byte w/borrow
	;mov h,a		; save in A
	;xchg		; difference (counter) in DE
	


	

; -----------------------
append		; append text to buffer. Type ESC to exit


	
	call	crlf
	mvi a	1
	sta		MODE
	call	showprompt		; show append prompt
	
	lxi h	LINE
	mvi c	80
	call	memset				; clear line
	lxi h	LINE
	mvi c	80
	call	gets				; get a line of text
	
	; test for ESC to end
	cpi	ESC
	jz	append_end
	
	; test for command
	lxi h	LINE
	mov a,m
	cpi '.'
	jnz		append_saveline
	
	; got command marker '.'
	inx h		; point to actual command
	mov a,m
	cpi	'x'		; exit append routine?
	jz		append_end
	
	; not a command - 
	dcx	h		; point back to '.'	
	
append_saveline		; save LINE to buffer
	lhld	CURBUFFERPOS		; get current buffer position
	lxi d	LINE
	
append_save0
	ldax d						; get char from LINE
	cpi		NULL
	jnz		append_save1
	
	; end of line - save CR/LF at EOL then get another line
	mvi a	CR
	mov	m,a						; end of line marker
	inx h
	mvi a	LF
	mov m,a
	inx h
	shld	CURBUFFERPOS
	jmp		append				; get another line
	
append_save1	; save LINE to text buffer
	mov	m,a		; save char
	inx h
	inx d
	jmp		append_save0
	
		
append_end
	; return to main
	mvi a	0		; set mode to command
	sta		MODE
	call	crlf
	jmp		loop
	
APPENDMSG	DB	"Hit ESC to exit",CR,LF,NULL
	
	
	
; ---------- insert -------------
insert		; insert a single line into the buffer

; This is overly complicated. format is i # where # is the line number
; displayed by a print 'p' command.
; Insert continues until the user press ESC at line start.

; HL points to 'i'. skip ahead and get the line number to insert
; UD2B converts string pointed to by DE to number in HL

	inx h		; skip past 'i'
	cpi ' '
	jz	insert	; skip spaces
	cpi	09h
	jz	insert	; skip tabs
	cpi	NULL
	jz	insert_error
	
	; test for decimal number 0-9 as 1st char
	cpi	10
	cmc			; if A < 10, set CY
	jnc		insert_error
	
	; HL points to start of number string
	xchg				; DE needs to point to start of decimal number
	call	UD2B		; HL holds line number (in binary) to delete
	shld	ACTIVE_NUM
	
	; search for matching line number in buffer
	lxi h	1		; Set starting line number
	shld	LINENUMBER
	
	lxi h	BUFFERSTART	; point to 1st line of text
	shld	START_ADDRESS
	
insert1
	mov a,m
	cpi	LF
	jz	insert2
	cpi	NULL
	jz	insert_error
	inx h
	jmp	insert1
	
insert2		; HL holds the end of the current line
	shld	END_ADDRESS
	; line #, start of line and end of line set
	

	; test if LINE_NUMBER = ACTIVE_NUM
	lhld	ACTIVE_NUM
	xchg	; save in DE
	lhld	LINENUMBER
	mov	a,h
	cmp d
	jnz		insert_no_match
	mov a,l
	cmp	e
	jnz		insert_no_match
	jmp		insert_match
	
insert_no_match		; line numbers don't match
	lhld	LINENUMBER
	inx h				; increment linenumber
	shld 	LINENUMBER	
	
	lhld	END_ADDRESS
	inx h		; point to start of next line
	shld	START_ADDRESS
	
	jmp		insert1		; test next line	
	
insert_match	
	; line numbers match - we will insert before this line
	; blocked by START_ADDRESS and END_ADDRESS
	; get a line of text from the user
	lxi h	INSERTMSG2
	call	puts
	mvi a	2		; show insert prompt
	sta	MODE
	
insert_match_again
	; clear, then fill LINE with new text to insert
	call	crlf
	call	showprompt
	
	lxi h	LINE
	mvi c	80
	call	memset		; clear buffer
	lxi h	LINE
	mvi c	80
	call	gets	
	cpi		ESC			; if user hits ESC we're done inserting
	jz		ins3
		
	; gets returns on seeing <CR> so there is no CRLF at the end
	; it needs to be added
	mvi a	CR
	mov m,a
	inx h
	mvi a	LF
	mov m,a
	
	; get length of LINE
	mvi c	0
	lxi h	LINE
i_count0
	mov	a,m
	cpi	NULL
	jz	i_count1
	inr c
	inx h
	jmp	i_count0
	
i_count1
	; c now holds num of chars in LINE (will always be less than 80)
	mov	l,c
	mvi h	0
	shld	LINESIZE	; LINESIZE is length of the line to insert
	
	; now shift up the entire buffer starting at CURBUFFERPOS down to STARTPOS
	; HL points to CURBUFFERPOS
	; DE points to CURBUFFERPOS + LINESIZE
	; BC holds LINESIZE
	; then call move_up
	
	; load DE with CURBUFFERPOS + LINESIZE
	lhld	CURBUFFERPOS
	dcx h			; subtract 1 since CURBUFFERPOS points to the NULL after buffer
	mov a,c			; add c (linesize) to HL for DE result
	add l
	mov l,a			; and save value
	mvi a 0
	adc h
	mov h,a			; and save value
	; HL holds new address - save into DE
	mov d,h
	mov e,l
	; DE now holds the move-to address
	
	; now load HL-STARTPOS into BC as the counter
	; the BC counter (HL - STARTPOS) is the counter
	lhld 	START_ADDRESS
	mov b,h
	mov c,l		; BC holds STARTPOS
	
	lhld 	CURBUFFERPOS
	dcx	h		; subtract BC (START_ADDRESS) from HL (CURBUFFERPOS) -1 (before the NULL)
	
	mov a,l
	sub c
	mov l,a
	
	mov a,h
	sbb b
	mov h,a		; HL now holds the counter
	
	mov b,h
	mov c,l		; BC now holds the counter
	inx b		; account for move_up counting 1 less than the counter
	
	lhld CURBUFFERPOS
	dcx h
	
	; Now shift from STARTPOS to CURBUFFERPOS up to DE
	; DB cbh
	call	move_up
	
	; set new value of CURBUFFERPOS (CURBUFFERPOS + LINESIZE)
	lhld	CURBUFFERPOS
	mov	d,h
	mov e,l		; save in DE
	lhld 	LINESIZE
	dad d		; add them
	shld	CURBUFFERPOS
	
	
	; now insert LINE at STARTPOS
	lhld 	START_ADDRESS		; get the address
	mov d,h
	mov e,l				; save into DE
	lxi h	LINE
ins1
	mov a,m
	cpi	NULL
	jz	ins2		; stop at EOL
	stax d
	inx h
	inx d
	jmp	ins1
	
ins2	; insert done
	; add LINESIZE to STARTADDRESS
	lhld START_ADDRESS
	mov d,h
	mov e,l
	lhld LINESIZE
	dad d
	shld START_ADDRESS
	jmp	insert_match_again
	; continues on during insert until user hits ESC
	
ins3		
	; done
	mvi a	0		; restore prompt
	call	showprompt
	call	crlf
	jmp		loop
		
	
	
insert_error	; badly formatted line number
	lxi	h	INSERTMSG1
	call	puts
	jmp		loop
	
INSERTMSG1 DB "Bad Line or Line Number",CR,LF,NULL	
INSERTMSG2	DB "Type line to insert then press <enter>",CR,LF,NULL
	
	
	
; ----------- delete --------------------
delete		; delete a single line

; HL points to 'd'. skip ahead and get the line number to delete
; UD2B converts string pointed to by DE to number in HL

	inx h		; skip past 'd'
	cpi	' '
	jz	delete	; skip spaces
	cpi	09h
	jz	delete	; skip TABS
	cpi	NULL
	jz	delete_error	; badly formatted
	
	; test for decimal number 0-9
	cpi	10
	cmc			; if A < 10 CY is set
	jnc	delete_error
	
	;; HL points to start of a number string
	xchg		; swap HL w/DE
	call	UD2B	; HL now holds binary line number to delete
	shld	ACTIVE_NUM	
	
	; search for matching line number in buffer
	lxi h	1		; starting line number
	shld	LINENUMBER
	
	lxi h	BUFFERSTART	; point to 1st line of text
	shld	START_ADDRESS
	
delete1
	mov a,m
	cpi	LF
	jz	delete2
	cpi	NULL
	jz	delete_error
	inx h
	jmp	delete1
	
delete2		; end of current line
	shld	END_ADDRESS
	; line #, start of line and end of line set
	; get the size (length) of the line
	lhld	END_ADDRESS
	xchg
	lhld	START_ADDRESS
	;
	mov a,e
	sub l
	mov l,a
	
	mov a,d
	sbb h		; subtract high byte w/borrow
	mov h,a
	; buffer size in HL
	shld	LINESIZE

	; test if LINE_NUMBER = ACTIVE_NUM
	lhld	ACTIVE_NUM
	xchg	; save in DE
	lhld	LINENUMBER
	mov	a,h
	cmp d
	jnz		delete_no_match
	mov a,l
	cmp	e
	jnz		delete_no_match
	jmp		delete_match


delete_no_match		; line numbers don't match
	lhld	LINENUMBER
	inx h				; increment linenumber
	shld 	LINENUMBER	
	lhld	END_ADDRESS
	inx h		; point to start of next line
	shld	START_ADDRESS
	jmp		delete1		; test next line
	
delete_match
	; line numbers match - we have line to delete
	; get size of move (CURBUFFERPOS - START_ADDRESS)
	lhld	CURBUFFERPOS
	xchg	; subtract START_ADDRESS from CURBUFFERPOS to get move size  
	lhld	START_ADDRESS
	; subtract
	mov a,e
	sub l
	mov l,a
	mov a,d
	sbb h		; subtract high byte w/borrow
	mov h,a
	
	mov	b,h
	mov c,l	; BC holds line size
	
	; get move to position in DE
	lhld	START_ADDRESS
	xchg
	
	; get position to move from
	lhld	END_ADDRESS
	inx h	; HL holds start of next line
	mvi a 0	; move downward
	call	move_down
	; line deleted
	
	; re-adjust CURBUFFERPOS downward by LINESIZE
	lhld	CURBUFFERPOS
	xchg		; put in DE
	lhld	LINESIZE
	; subtract
	mov a,e
	sub l
	mov l,a
	mov a,d
	sbb h		; subtract high byte w/borrow
	mov h,a
	; new address in HL
	dcx h		; -1 need to point before NULL
	shld CURBUFFERPOS

	; delete done	
	call	crlf
	jmp		loop
	

delete_error	; badly formatted line number
	lxi	h	DELETEMSG1
	call	puts
	jmp		loop
	
DELETEMSG1 DB "Bad Line or Line Number",CR,LF,NULL
	
	
	
; -------------- list ----------------------
list		; display buffer

	call	crlf
	
	;; show starting line number
	lxi h	0001h		; line number
	shld	LINENUMBER

	mov a,h			; (MSB)
	call	printdecimal
	mov a,l			; (LSB)
	call	printdecimal
	mvi a	':'
	call	putc
	call	putc		; two :: between line number and text
	
	lxi	h	BUFFERSTART
	
list1
	mov	a,m			; get char from buffer
	cpi	NULL
	jz	list_end
	
	cpi	CR
	jnz	list2
	
	; got CRLF - show, then print a new line number
	call	crlf
	inx h	; skip CR
	inx h	; skip LF
	mov a,m			; test char after CRLF - end of file
	cpi	NULL
	jz	list_end
	
	; not null - print line#
	push h		;; save address
	lhld	LINENUMBER
	inx h				; increment line number
	shld	LINENUMBER

	mov a,h
	call	printdecimal
	mov a,l
	call	printdecimal
	
	mvi a	':'			;; set between linenumber and text
	call	putc
	call	putc
	pop	h

list2
	; now show char
	mov	a,m
	call	putc
	inx	h
	jmp	list1
	
list_end
	; return to main
	call	crlf
	jmp		loop



; ----------- clear fcb --------------
clear_fcb
	mvi c	12
	lxi h	FILENAME
	mvi a	NULL
clear_fcb1
	mov m,a
	inx h
	dcr c
	jnz		clear_fcb1
	ret



; ---------------- save -----------------------
save		; save buffer to disk

	call	crlf
	lxi h	SAVEMSG1
	call	puts
	
	; clear the FCB
	call	clear_fcb
	mvi a	0
	sta		FCBSTATUS
	
	lxi	h	LINE
	mvi c	80
	call	memset
	lxi	h	LINE
	mvi c	80
	call	gets		; get a filename
	
	; save the filename to FCB
	lxi d	FILENAME
	lxi h	LINE
	mvi c	9		; counter
	
save1	; save the filename up to '.'
	mov a,m		; get char from LINE
	cpi	'.'
	jz	save2
	cpi	NULL
	jz	save_fnfail
	stax d		; store char in FILENAME
	inx h
	inx d
	dcr c
	jz	save_fnfail
	
	jmp	save1
	
save2	; skip '.'
	inx h		; skip '.'
	lxi d		FILETYPE
	mvi c	4
	
save3	; save the filetype
	mov a,m		; get char from LINE
	cpi	NULL
	jz		save4
	stax d		; save in FILETYPE
	inx h
	inx d
	dcr c
	jz	save_fnfail
	jmp		save3
	
save4	; point DE to start, HL to end and save buffer
	lxi d	BUFFERSTART
	lhld	CURBUFFERPOS
	out 248
	lxi d  0	; clear for later routines
	
	; get status byte
	lda		FCBSTATUS
	cpi		0
	jnz		save_fail
	call	crlf
	lxi h	SAVEMSGOK
	call	puts
	
	; restore pointers (ni)
	jmp		loop
	
save_fail
	call	crlf
	lxi h	SAVEMSGFAIL
	call	puts
	jmp		loop
	
save_fnfail		; bad filename
	call	crlf
	lxi h	SAVEFNFAIL
	call	puts
	jmp		loop
	
	
SAVEMSG1	DB	"Filename: ",NULL	
SAVEMSGOK	DB	"File Saved",CR,LF,NULL
SAVEMSGFAIL DB  "Save Error",CR,LF,NULL	
SAVEFNFAIL	DB	"Bad Filename",CR,LF,NULL



; -------------- load ---------------------
load		; load file into buffer

	call	crlf
	lxi h	LOADMSG1
	call	puts
	
	mvi a	0
	sta		FCBSTATUS
	
	lxi	h	LINE
	mvi c	80
	call	memset
	lxi	h	LINE
	mvi c	80
	call	gets		; get a filename
	
	; save the filename to FCB
	lxi d	FILENAME
	lxi h	LINE
	
load1	; save the filename up to '.'
	mov a,m		; get char from LINE
	cpi	'.'
	jz	load2
	stax d
	inx h
	inx d
	jmp	load1
	
load2	; skip '.'
	inx h		; skip '.'
	lxi d		FILETYPE
	
load3	; save the filetype
	mov a,m
	cpi	NULL
	jz		load4
	stax d	
	inx h
	inx d
	jmp		load3
	
load4	; point HL to memory start for loading
	lxi h	BUFFERSTART
	in	248		; call load routine - if return code is 0
				; then HL points to last char loaded
	
	; get status byte
	lda		FCBSTATUS
	cpi		0
	jnz		load_error
	
	shld	CURBUFFERPOS		; save HL address of last byte loaded
	
	; show load success
	call	crlf
	lxi h	LOADMSGOK
	call	puts
	jmp		loop			; return to command prompt
	
load_error		; show load error
	call	crlf
	lxi h	LOADMSGFAIL
	call	puts
	jmp		loop
	
LOADMSGFAIL	DB	"File Load Failed",CR,LF,NULL
LOADMSGOK	DB	"File Loaded",CR,LF,NULL
LOADMSG1	DB	"Filename: ",NULL




; --------- SUBROUTINES -----------
; ------------------------------------------
showprompt	; display prompt depending on mode in B
	push	psw
	push	h
	lda		MODE	; get current mode
	cpi	0
	jnz sp1
	
	; show cmd prompt
	lxi h	CMDPROMPT
	call	puts
	pop h
	pop psw
	ret
sp1	
	cpi 1
	jnz	sp2
	; show append prompt
	lxi h	APPENDPROMPT
	call	puts
	pop h
	pop psw
	ret
sp2
	cpi	2
	jnz	sp3
	; show insert prompt
	lxi h	INSERTPROMPT
	call	puts
	pop h
	pop psw
	ret
sp3
	pop h
	pop psw
	ret
	
CMDPROMPT DB "ed>",NULL
APPENDPROMPT DB "a>",NULL
INSERTPROMPT DB "i>",NULL


; ---------- move block --------------
; move a block of memory. 
; HL points to start of FROM address
; DE points to start of TO address
; BC is the count (size) 
; A = 0, move DOWN. A = 1, move UP
move_block

	cpi	0
	jz		move_down
	
move_up		; a=1
	mov a,m
	stax d
	dcx d
	dcx h
	dcx b	; dec counter
	mov	a,b
	ora c
	jnz		move_up		; NOTE: stops at BC=00 account for it by adding 1 to BC beforehand
	ret
	
	; counter = last_memory_used - start_of_TO_address
move_down		; a=0
	mov a,m
	stax d
	inx	d
	inx h
	dcx b	; dec counter
	mov a,b
	ora c
	jnz	move_down
	ret
	
	

	
	
	
	

; ---------- clear block -----------
				; clear a block of memory
clear_block		; HL holds start address, DE holds size of block

	
	mvi a	NULL
	
clear_block1
	mov m,a
	inx h
	dcr e
	jnz	clear_block1
	
	mvi	a	0	; if D is already 0, we're done
	cmp	d
	jz	clear_block2
	
	mvi a	NULL
	dcr	d
	jnz		clear_block1
	
clear_block2
	ret
	
	

; -------------------------------
crlf	; send a CR LF to console
		push	psw
		mvi		a	CR
		call	putc
		mvi		a	LF
		call	putc
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
getc	- blocking input: get/return a single char

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
	
	lxi h	LINE		; restore pointers
	mvi c	80
	call	memset
	lxi h	LINE
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
	; clear LINE 
	lxi h	LINE
	mvi c	80
	call 	memset
	mvi a	'^'
	call	putc
	mvi a	'C'
	call	putc
	jmp	WARMBOOT

gets_save	
	cpi	ESC			; don't echo, test for ESC
	rz				; return with ESC in A
	
	call	putc	; echo char
	cpi	CR
	rz
	
	; save to LINE
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
	
	
	
	
; ---------------------------------------------------
	; subroutine printhex - convert binary value in A, print as 2 hex bytes
printhex
		push b
		mov b,a		; save A
		ani	f0h	; MSB
		rar
		rar
		rar
		rar
		adi	30h
		cpi	3Ah
		jc	printhex1
		adi	7
printhex1
		call	putc
		mov a,b		; LSB
		ani	0fh
		adi	30h
		cpi	3Ah
		jc	printhex2
		adi	7
printhex2
		call	putc
		pop b
		ret			



; ---------------------------------------------------------------------------------
; subroutine divide - divide number in HL by B. Dividend in HL, divisor in B
divide
		mvi 	c 0x08		; counter
up		
		dad 	h			; calling routine supplies HL and B
		mov 	a,h
		sub 	b
		jc 		down
		mov 	h,a
		inr 	l
down		
		dcr 	c
		jnz 	up			; ie: 59D / 10D L=5, H=9
		ret					; result in HL (remainder/quotient)



; subroutine printdecimal - convert value in A to decimal, print it to CON
; NOTE: range in A is 0-99 only. 
printdecimal
		push psw
		push b
		push d
		push h
		
		mov 	l,a			; put value in L
		mvi 	h	0x00	; MSB is 0
		mvi 	b	0x0a	; dividing by 10D
		call	divide
		mov 	a,l			; get quotient
		adi 	0x30		; to ascii
		call	putc
		mov 	a,h			; get remainder
		adi 	0x30		; to ascii
		call	putc
		
		pop h
		pop d
		pop b
		pop psw
		ret
; -------------------------------------------------------------------------------		
		
ERROF	; overflow error
		lxi h	OVFLOW
		call	puts
		ret
		
OVFLOW	DB	"Overflow Error in math routine",CR,LF,0

;----------------------------------------------------------------------------;		
; These next routines come from                                              ;
; https://github.com/lvisser-code/8080-8085_Math_Library/blob/main/UIML.asm  ;
;----------------------------------------------------------------------------;


;------------------------------- UD2B ---------------------------------
; Convert decimal number to binary
; On call: DE = ptr to decimal number in ASCII
; On retn: HL = binary result, DE is advanced past decimal number.
;----------------------------------------------------------------------
UD2B:  
		LDAX D          ;Skip any leading blanks
        INX D
        CPI ' '
        JZ UD2B
        DCX D
        LXI H,0         ;Initialize result = 0
UD2B1:  LDAX D          ;Fetch next ascii character
        SUI 48          ;Convert char to BCD digit if possible
        MOV C,A         ;Save (character-48) in C
        RM              ;Is it a digit 0 thru 9...
        CPI 10
        RP              ;...If not, then exit
        PUSH D          ;...If so, save buffer pointer
        LXI D,10        ;...Multiply partial result by 10...
        CALL UMUL       ;...(also) checking for overflow...
        MVI D,0         ;...and add in value of digit...
        MOV E,C
        CALL UADD       ;HL = HL*10 + digit
        POP D           ;Recall buffer pointer
        INX D           ;Bump buffer pointer
        JMP UD2B1       ;...and we're ready for next character
        
        
        
;------------------------------- UB2D ---------------------------------
; Convert binary number to ASCII decimal
; On call: HL = binary number, DE = ptr to string location
; On retn: string 0 terminated, A = number of characters.
;----------------------------------------------------------------------
UB2D:   
		PUSH B
        PUSH D
        PUSH H
        MVI C,0         ;C=digit counter
        PUSH D          ;Save a copy of ptr
UB2D1:  PUSH D          ;Save DE ptr prior to divide
        LXI D,10
        CALL UDIV       ;HL = HL / 10
        MOV A,E         ;E = remainder
        ADI 48          ;Convert to ASCII char
        POP D           ;Recall DE ptr
        STAX D          ;Move char to string
        INX D           ;Inc ptr
        INR C           ;Inc counter
        MOV A,H         ;If HL > 0...
        ORA L
        JNZ UB2D1       ;...loop
        SUB A
        STAX D          ;Move 0 to end of string
        INR C           ;Inc digit count
;Reverse string
        DCX D           ;Ptr to end of string
        POP H           ;Ptr to beginning of string
        PUSH B          ;Save a copy of digit count
        DCR C
UB2D2:  MOV B,M         ;Leading char to temp
        LDAX D          ;Get next trailing char
        MOV M,A         ;...move it
        MOV A,B         ;Get leading char
        STAX D          ;...move it
        INX H           ;Update pointers
        DCX D
        MOV A,C         ;Decrement count by 2
        SUI 2
        JZ UB2D3        ;Check whether done
        JC UB2D3
        MOV C,A
        JMP UB2D2       ;Loop
UB2D3:  POP B           ;Recall digit count
        MOV A,C         ;A = count
        POP H
        POP D
        POP B
        RET
        
        
        
;------------------------------- UMUL ---------------------------------
; Integer multiplication.
; On call: Registers HL and DE contain the multiplicands.
; On retn: HL contains the result.
;----------------------------------------------------------------------
UMUL:   PUSH D
        XRA A           ;Test for HL less than 256
        ADD H
        JZ UMUL1        ;Branch if HL less than
        XRA A
        ADD D           ;Else, DE must be < 256...
        CNZ ERROF       ;...or overflow would result
        XCHG            ;HL now has an op < 256
UMUL1:  MOV A,L         ;Move 255 or less multiplier to A
        LXI H,0         ;Initialize partial product
UMUL2:  STC
        CMC
        RAR             ;Rotate multiplier right off end
        JNC UMUL3       ;If bit shifted out was 0, skip
        DAD D           ;Else, add multiplicand to partial product
        CC  ERROF       ;...while checking for overflow
UMUL3:  XCHG
        DAD H           ;Shift multiplicand left 1 bit...
        XCHG
        ORA A
        JNZ UMUL2       ;Branch to top of loop if mult is non-0
        POP D
        RET


;------------------------------- UDIV ---------------------------------
; Integer division.
; On call: HL = dividend, DE = divisor.
; On retn: HL = quotient, DE = remainder.
;----------------------------------------------------------------------
UDIV:   PUSH B
        MOV A,D         ;If divisor MSB = 0...
        ORA A
        JNZ UDIV0       ;...then skip
;Check for special cases
        MOV A,E         ;If divisor LSB = 0...
        ORA A
        CZ  ERROF       ;...then call error handler (divide by 0)
        CPI 1           ;If divisor = 1...
        JZ UDIVF1       ;...then branch to fast divide by 1
        CPI 2           ;If divisor = 2...
        JZ UDIVF2       ;...then branch to fast divide by 2

UDIV0:  MOV C,L         ;Move dividend (=rem) to BC
        MOV B,H
        LXI H,0         ;initialize quotient = 0...
        PUSH H          ;...on top of stack (TOS)
        INR L           ;Initialize HL (pos) = 1
;Now BC = rem, DE=div, HL = pos, TOS=quo
;Shift pos & div left until rem >= div
UDIV1:  MOV A,D         ;If msb of div = 1...
        RAL
        JC UDIV3        ;...jump
        DAD H           ;pos = pos*2 (shift left)
        XCHG 
        DAD H           ;div = div*2 (shift left)
        XCHG
        MOV A,C         ;If div < rem...
        SUB L
        MOV A,B
        SBB H
        JNC UDIV1       ;...loop

UDIV2:  CALL UDIVR      ;pos = pos/2 (shift right)
        JZ UDIV4        ;If pos = 0, we're done
        XCHG            ;Now HL=div
        CALL UDIVR      ;div = div/2 (shift right)
        XCHG            ;Now HL=pos
UDIV3:  MOV A,C         ;If div > rem...
        SUB E
        MOV A,B
        SBB D
        JC UDIV2        ;...loop
        MOV A,C         ;rem = rem - div...
        SUB E
        MOV C,A
        MOV A,B
        SBB D
        MOV B,A
        XCHG            ;Now HL=div
        XTHL            ;Now BC=rem, DE=pos, HL=quo, TOS=div
        DAD D           ;quo = quo + pos
        XTHL            ;Now BC=rem, DE=pos, HL=div, TOS=quo
        XCHG            ;Now HL=pos
        JMP UDIV2       ;Loop

UDIV4:  POP H           ;Get quotient to HL
        MOV E,C         ;Move final rem to DE
        MOV D,B
        POP B
        RET

;Fast divide by 1
UDIVF1: LXI D,0         ;Remainder = 0
        POP B
        RET

;Fast divide by 2
UDIVF2: XRA A           ;Clear CY
        MOV A,H         ;Shift H
        RAR
        MOV H,A
        MOV A,L         ;Shift L
        RAR
        MOV L,A
        LXI D,0         ;Set DE remainder = CY
        MOV A,E
        RAL
        MOV E,A
        POP B
        RET


;Right shift HL, remainder returned in CY, Z set if zero
UDIVR:  XRA A           ;Clear CY
        MOV A,H         ;Shift H
        RAR
        MOV H,A
        MOV A,L         ;Shift L
        RAR
        MOV L,A
        ORA H           ;Set zero flag if both H and L = 0
        RET







;------------------------------- UADD ---------------------------------
; Integer addition.
; On call: Registers DE and HL contain the addends.
; On retn: HL contains the result.
;----------------------------------------------------------------------
UADD:   DAD D           ;HL=HL+DE: Only CY flag affected
        CC  ERROF       ;If overflow, then call error handler
        RET


;------------------------------- USUB ---------------------------------
; Integer subtraction.
; On call: Register HL contains minuend and DE contains subtrahend.
; On retn: HL contains the result.
;----------------------------------------------------------------------
USUB:   MOV A,L         ;Get low byte of minuend
        SUB E           ;Subtract low byte of subtrahend
        MOV L,A         ;L = result
        MOV A,H         ;Get high byte of minuend
        SBB D           ;Subtract high byte of subtrahend
        MOV H,A         ;H = result
        CC ERROF        ;If underflow, then call error handler
        RET





; ---------------------------------------------
; ---------------------------------------------

; holds current mode for prompt
MODE DS 1

; holds current buffer position
CURBUFFERPOS DS 2	

; holds 4 digit hex line number (print buffer)
LINENUMBER DS 2

; holds line number for testing (delete, insert, replace)
ACTIVE_NUM DS 2

; holds starting address in delete, insert, replace, print
START_ADDRESS DS 2

; holds ending address in delete, insert, replace, print
END_ADDRESS DS 2

; holds the length (size) of a line
LINESIZE DS 2

; temp storage
TEMPADDR DS 2

; 80 char line buffer
LINE DS 80

; seperator between working area and text buffer
SPBUF DS 100

; text buffer (size actually set by BUFFERSIZE)
BUFFERSTART DS 1
	

	
