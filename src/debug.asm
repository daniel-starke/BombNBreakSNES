; @file debug.asm
; @author Daniel Starke
; @copyright Copyright 2023 Daniel Starke
; @date 2023-07-16
; @version 2023-07-16

.include "hdr.asm"
.accu 16
.index 16
.16bit


.section ".debugBreak_text" superfree
; void debugBreak(const char * msg);
debugBreak:
	php                ; push processor flags to stack (1 byte)
	                   ; stack:
	                   ; 5 | 4 byte message
	                   ; 1 | 4 byte return address
	                   ; 0 | 1 byte processor flags

	rep #$30           ; 16-bit accumulator and index registers
	lda 7,s            ; load source message high word
	tax                ; transfer accumulator to x register
	lda 5,s            ; load source message low word
	brk                ; software breakpoint (message in X:A)

	plp                ; pull processor flags from stack (1 byte)
	rtl                ; return from subroutine long

.ends
