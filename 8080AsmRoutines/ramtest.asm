;; ramtest.asm
; write a block of characters to psram
; then read back and display
; k theis 9/28/2026

conout	equ	0
conin	equ	0
CR		equ	13
LF		equ	10
NULL	equ	0

	org	0100h
	
	lxi h	MSG1
	call	puts
	
	lxi	h	0000h		; psram address
	mvi a	'A'			; char to write
		
;; write a string A-Z	
loop1
	call	putc
	out	32				; write to psram
	inx	h				; next address
	inr	a				; next character
	cpi	5bh		; Z+1
	jnz	loop1
	
	call	crlf
	lxi h	MSG2
	call	puts
	
;; read a string
	lxi	h	0000h
	mvi c	26
loop2
	in	32				; read from psram
	call	putc		; print char
	inx	h				; next address
	dcr	c				; counter
	jnz	loop2

	

	; return to OS
	call	crlf
	lxi h	MSG3
	call	puts
	jmp	0
	
	
MSG1	DB "Writing a block to psram",CR,LF,NULL
MSG2	DB "Reading a block of psram",CR,LF,NULL
MSG3	DB "Finished.",CR,LF,NULL	
	
; --------------------
crlf
	mvi a	CR
	call	putc
	mvi a	LF
	call	putc
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
