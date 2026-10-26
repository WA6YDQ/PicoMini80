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

	
; <cr> - increment address, show values
test1
	cpi		CR
	jnz		test2		; increment address. show addr and byte
	lhld	WORKINGADR
	inx		h
	shld	WORKINGADR
	call	crlf
	call	show_current
	jmp		monloop
	
	
; 'b' - decrement address, show values
test2
	cpi		'b'			; decrement address, show addr and byte
	jnz		test3
	lhld	WORKINGADR
	dcx		h
	shld	WORKINGADR
	call	crlf
	call	show_current
	jmp		monloop
	
	
; 'q' - finished here, resturn to OS
test3
	cpi		'q'
	jnz		test4
	jmp		0		; done here


; '.' - get next 2 chars, convert to hex, save byte to current address, inc adr
test4
	cpi		'.'		
	jnz		test5
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
	


; 'm' - get next 4 chars, use as new working address
test5
	cpi	'm'
	jnz		test6

	call	gethexbyte		; get MSB
	mov h,a	
	call	gethexbyte		; get LSB
	mov l,a
	shld	WORKINGADR		; save new address
	call	crlf
	call	show_current
	jmp		monloop



; 'd' - get next 4 chars, use as starting address to display a 256 byte block of memory
test6
	cpi 'd'
	jnz		test7
	
	call	gethexbyte
	mov h,a
	call	gethexbyte
	mov l,a
	call	crlf
	call	showblock
	call	crlf
	call	show_current
	jmp		monloop
	
	
; 'g' - get next 4 chars, use as address to start execution
test7
	cpi	'g'
	jnz		test_end
	
	call	gethexbyte
	mov h,a
	call	gethexbyte
	mov l,a
	call	crlf
	pchl
	

; unknown char - do nothing
test_end
	jmp		monloop
	





MONMSG1		DB	"8080 ML Monitor",CR,LF,NULL

WORKINGADR DS 2

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
	
	
; ----------- show_current -------------	
show_current	; crlf, show address, show byte
	; show address as 4 byte hex value
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
	
	
