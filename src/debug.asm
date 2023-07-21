; @file debug.asm
; @author Daniel Starke
; @copyright Copyright 2023 Daniel Starke
; @date 2023-07-16
; @version 2023-07-21

.RAMSECTION ".reg_debug7e" BANK $7E
; extern const char * debugMessage;
debugMessage:     DSW 2 ; debug message
.ends

.include "hdr.asm"
.accu 16
.index 16
.16bit


.section ".debugBreak_text" superfree
; void debugBreak();
debugBreak:
	php                ; push processor flags to stack (1 byte)
	brk                ; software breakpoint (message in [debugMessage] if called via ASSERT())
	plp                ; pull processor flags from stack (1 byte)
	rtl                ; return from subroutine long

.ends
