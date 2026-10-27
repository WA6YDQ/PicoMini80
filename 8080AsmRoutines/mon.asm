;; mon.asm - simple 8080 monitor program
;; commands:
;; <enter>     show next address:byte
;; b           show previous address:byte
;; dnnnn       display a page of memory (256 bytes) starting at address nnnn (in hex)
;; mnnnn       change working address to nnnn (in hex)
;; gnnnn       start execution at address nnnn (in hex)
;; q           exit back to OS
;; .bb         save bb (in hex) to current address, increment address and show


CR 		EQU		13
LF		EQU		10
NULL	EQU		0
CONSOLE EQU		0

MON		EQU		0100h
STACK 	EQU		efffh



	org	MON
start
	lxi	sp	STACK
	lxi h	1000h		;; starting address
	shld	WORKINGADR	; save
	
	lxi h	MONMSG1
	call	puts
	
	call	crlf
	call	show_current	;; show initial address and byte
	
	
; ------------------------------------------------
; this is the main working loop                  ;
; ------------------------------------------------
monloop
	call	getc
	; command in A - save for later
	sta		COMMAND

	
; <cr> - increment address, show values
test0
	lda		COMMAND
	cpi		CR
	jnz		test1
	lhld	WORKINGADR
	inx		h
	shld	WORKINGADR
	call	crlf
	call	show_current
	jmp		monloop
	
test1  ; 'n' Same as CR. Increment address, show next
	lda		COMMAND
	cpi		'n'
	jnz		test2
	lhld	WORKINGADR
	inx		h
	shld	WORKINGADR
	call	crlf
	call	show_current
	jmp		monloop
	

test2	; 'b' - decrement address, show values
	lda		COMMAND
	cpi		'b'	
	jnz		test3
	lhld	WORKINGADR
	dcx		h
	shld	WORKINGADR
	call	crlf
	call	show_current
	jmp		monloop
	
	

test3	; 'q' - finished here, return to OS
	lda		COMMAND
	cpi		'q'
	jnz		test4
	jmp		0		; done here
	
	
	
test4	; show command summary
	lda		COMMAND
	cpi		'h'
	jnz	test5
	lxi h	HELPMSG
	call	puts
	lxi h	WORKINGADR
	call	crlf
	call	show_current
	jmp		monloop
	
HELPMSG		DB	CR,LF,"Command Summary",CR,LF
			DB	"q             Exit to OS",CR,LF
			DB	"<enter> or n  Increment Address",CR,LF
			DB	"b             Decrement Address",CR,LF
			DB	"c [address]   Clear a Block of Memory",CR,LF
			DB	"d [address]   Display a Block of Memory",CR,LF
			DB	"g [address]   Goto and Start running at address",CR,LF
			DB	"h             Show this Summary",CR,LF
			DB  "m [address]   Move address pointer to new address",CR,LF
			DB  ". [byte]      Enter new byte at current address, advance by one",CR,LF
			DB  "All entered values are in hex",CR,LF
			DB  NULL
			

; '.' - get next 2 chars, convert to hex, save byte to current address, inc adr
test5
	lda		COMMAND
	cpi		'.'		
	jnz		test6
	call	gethexbyte		; get 2 ascii, convert to hex in A
	lhld	WORKINGADR
	mov		m,a				; save to current addr
	mvi a	' '
	call	putc
	call	show_current	; show update
	call	crlf
	lhld	WORKINGADR
	inx h
	shld	WORKINGADR
	call	show_current
	jmp		monloop
	
	
test6	; test for 'm' - change working memory address
	lda		COMMAND
	cpi		'm'
	jnz		test7
	call	get_address
	lhld	TEMPADDR
	shld	WORKINGADR
	call	crlf
	call	show_current
	jmp		monloop
	
test7	; 'g' (goto) - set PC to address and execute from there
	lda		COMMAND
	cpi		'g'
	jnz		test8	
	call	get_address
	call	crlf
	lhld	TEMPADDR
	pchl
	
test8	; 'd' (display) - display a 256 byte block of memory starting at HL
	lda		COMMAND
	cpi		'd'
	jnz		test9
	call	get_address
	lhld	TEMPADDR
	call	crlf
	call	showblock
	call	crlf
	call	show_current
	jmp		monloop
	
test9	; 'c' (clear) - clear a 256 byte block of memory starting at HL
	lda		COMMAND
	cpi		'c'
	jnz		test10
	call	get_address
	call	crlf
	lhld	TEMPADDR
	call	clearblock
	call	crlf
	call	show_current
	jmp		monloop
	
test10	; future use
	

test11	; future use

test12	; future use

test_end	; unknown char - do nothing
	mvi a	'?'
	call	putc
	call	crlf
	call	show_current
	jmp		monloop
	


	
	






MONMSG1		DB	"--- 8080 ML Monitor ---",CR,LF
			DB  "Hit 'h' for command summary",CR,LF,NULL

WORKINGADR  DS  2
TEMPADDR	DS	2
COMMAND		DS 	1

;; ------ Subroutines -------



; -------- putc -------------
putc		; output a character in A
	out	CONSOLE
	ret
	


; -------- puts ------------	
puts		; print a line of text
	mov a,m
	cpi	NULL
	rz
	call	putc
	inx h
	jmp	puts
	
	
	
; ----- getc ----------	
getc		; get a char into A
	in	CONSOLE
	cpi	feh
	jz	getc
	;; echo it
	call	putc
	ret
	
	
; ------ crlf ----------
crlf
	mvi a	CR
	call	putc
	mvi a	LF
	call	putc
	ret	
	
	
; ----- get_address ------
; read next 4 bytest from the keyboard, convert to 16 bit, save for later
get_address
	mvi a	':'
	call	putc
	call	gethexbyte
	mov h,a
	call	gethexbyte
	mov l,a
	shld	TEMPADDR	; save for later
	ret	
	
	
; ----------- show_current -------------	
show_current	; crlf, show address, show byte
	; show address as 4 digit hex value and byte
	; as 2 digit hex value. Do not change address
	lhld	WORKINGADR
	mov a,h
	call	showhex
	mov a,l
	call	showhex
	mvi a	' '
	call	putc
	mov a,m		; get byte at address
	call	showhex
	mvi a	' '
	call	putc
	ret
	
	
; ---------- showhex --------------------	
showhex		; display value in A as 2 byte hex values
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
	call	putc	; show it
	mov 	a,b		; restore A
	ani		0fh		; mask off MSB
	adi 	30h		; convert to ascii
	cpi 	3ah		; test for > 39h
	jc		sb2
	adi 	07h
sb2
	call	putc
	pop 	b		; restore BC
	ret

; ----------------- hex2b ----------------
hex2b		; take ascii value in a, convert to absolute, return in A
	; convert to UPPER CASE
	cpi		5Ah
	jc		hb0
	sui		20h
hb0
	sui		30h		; convert from ascii
	cpi		0ah		; overflow?
	jc		hb1		; no
	sui 	07h		; convert a-f
hb1
	ret	
	
; ---------- gethexbyte - get 2 chars in A, convert to absolute, return in A --------
gethexbyte
	call	getc
	; got a char in a
	call	hex2b
	rlc
	rlc
	rlc
	rlc		; shift to MSB
	mov b,a	
ghb1
	call	getc
	; got a 2nd char
	call	hex2b
	ora b
	ret
	

; -------- showblock - display a 256 byte block of memory. Start addr in HL --------
showblock
	mvi b	16		; rows
	mvi c	16		; cols
	; starting address in HL
	mov a,h		; display address high
	call	showhex
	mov a,l
	call	showhex
	mvi a	' '
	call	putc
	; show 16 chars
	mvi c 	16		; columns
showblock1		; show 16 bytes
	mov a,m
	call	showhex
	mvi a	' '
	call	putc
	inx h
	dcr c
	jnz		showblock1
	; we showed 16 bytes - next block
	call	crlf
	dcr b
	jnz		showblock+2
	call	crlf
	ret		; done
	
	
; ------- clearblock - set a 256 byte block to NULL ----------
clearblock
	; starting address in HL
	mvi c	ffh
	xra a
clearblock1
	mov m,a
	inx h
	dcr c
	jnz	clearblock1
	ret
	
	
