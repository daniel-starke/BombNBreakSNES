; @file utility.asm
; @author Daniel Starke
; @copyright Copyright 2023 Daniel Starke
; @date 2023-07-07
; @version 2023-07-15

.equ REG_VMAIN    $2115
.equ REG_VMADDL   $2116
.equ REG_VMDATAL  $2118
.equ REG_VMDATAH  $2119
.equ REG_MDMAEN   $420B
.equ REG_DMAP0    $4300
.equ REG_BBAD0    $4301
.equ REG_A1T0L    $4302
.equ REG_A1B0     $4304
.equ REG_DAS0L    $4305

.RAMSECTION ".reg_utility7e" BANK $7E
; extern uint32_t lrngSeed;
lrngSeed:         DSW 2 ; random seed
.ends

.include "hdr.asm"
.accu 16
.index 16
.16bit


.section ".dmaFillVramWord_text" superfree
; void dmaFillVramWord(const uint16_t value, const uint16_t address, const uint16_t size);
dmaFillVramWord:
	php                ; push processor flags to stack (1 byte)
	                   ; stack:
	                   ; 9 | 2 byte size
	                   ; 7 | 2 byte address
	                   ; 5 | 2 byte value
	                   ; 1 | 4 byte return address
	                   ; 0 | 1 byte processor flags

	rep #$21           ; 16-bit accumulator, clear carry flag
	tsc                ; copy stack register to accumulator
	adc #5             ; address of the value on stack
	tax                ; copy accumulator to x register
	sta.l REG_A1T0L    ; source address
	lda 7,s
	sta.l REG_VMADDL   ; VRAM destination address (word addressed)
	lda 9,s
	sta.l REG_DAS0L    ; number of bytes to be written
	lda #$1808
	sta.l REG_DMAP0    ; fixed source address, byte increment source, operate on REG_VMDATAL

	sep #$20           ; 8-bit accumulator
	lda #$00
	sta.l REG_A1B0     ; source address in memory bank 0
    sta.l REG_VMAIN    ; increment VRAM address every low byte
	lda #1             ; turn on bit 1 (channel 0) of DMA
	sta.l REG_MDMAEN

	rep #$20           ; 16-bit accumulator
	txa                ; copy x register to accumulator
	inc a              ; next source byte
	sta.l REG_A1T0L    ; source address
	lda 7,s
	sta.l REG_VMADDL   ; VRAM destination address (word addressed)
	lda 9,s
	sta.l REG_DAS0L    ; number of bytes to be written

	sep #$20           ; 8-bit accumulator
	lda #$19
	sta.l REG_BBAD0    ; operate on REG_VMDATAH
	lda #$80
    sta.l REG_VMAIN    ; increment VRAM address every high byte
	lda #1             ; turn on bit 1 (channel 0) of DMA
	sta.l REG_MDMAEN

	plp                ; pull processor flags from stack (1 byte)
	rtl                ; return from subroutine long

.ends


.section ".dmaCopyVramLowBytes_text" superfree
; void dmaCopyVramLowBytes(const uint8_t * source, const uint16_t address, const uint16_t size);
dmaCopyVramLowBytes:
	php                ; push processor flags to stack (1 byte)
	                   ; stack:
	                   ; 11 | 2 byte size
	                   ;  9 | 2 byte address
	                   ;  5 | 4 byte source
	                   ;  1 | 4 byte return address
	                   ;  0 | 1 byte processor flags

	rep #$20           ; 16-bit accumulator
	lda 5,s
	sta.l REG_A1T0L    ; source address
	lda 9,s
	sta.l REG_VMADDL   ; VRAM destination address (word addressed)
	lda 11,s
	sta.l REG_DAS0L    ; number of bytes to be written
	lda #$1800
	sta.l REG_DMAP0    ; byte increment source, operate on REG_VMDATAL

	sep #$20           ; 8-bit accumulator
    lda 7,s
    sta.l REG_A1B0     ; bank address of the source
	lda #$00
    sta.l REG_VMAIN    ; increment VRAM address every high byte
	lda #1             ; turn on bit 1 (channel 0) of DMA
	sta.l REG_MDMAEN

	plp                ; pull processor flags from stack (1 byte)
	rtl                ; return from subroutine long

.ends


.section ".dmaCopyVramHighBytes_text" superfree
; void dmaCopyVramHighBytes(const uint8_t * source, const uint16_t address, const uint16_t size);
dmaCopyVramHighBytes:
	php                ; push processor flags to stack (1 byte)
	                   ; stack:
	                   ; 11 | 2 byte size
	                   ;  9 | 2 byte address
	                   ;  5 | 4 byte source
	                   ;  1 | 4 byte return address
	                   ;  0 | 1 byte processor flags

	rep #$20           ; 16-bit accumulator
	lda 5,s
	sta.l REG_A1T0L    ; source address
	lda 9,s
	sta.l REG_VMADDL   ; VRAM destination address (word addressed)
	lda 11,s
	sta.l REG_DAS0L    ; number of bytes to be written
	lda #$1900
	sta.l REG_DMAP0    ; byte increment source, operate on REG_VMDATAL

	sep #$20           ; 8-bit accumulator
    lda 7,s
    sta.l REG_A1B0     ; bank address of the source
	lda #$80
    sta.l REG_VMAIN    ; increment VRAM address every high byte
	lda #1             ; turn on bit 1 (channel 0) of DMA
	sta.l REG_MDMAEN

	plp                ; pull processor flags from stack (1 byte)
	rtl                ; return from subroutine long

.ends


.section ".lrng_text" superfree
; uint16_t lrng(void) {
; 	lrngSeed ^= lrngSeed >> 17;
; 	lrngSeed ^= lrngSeed << 15;
; 	lrngSeed ^= lrngSeed >> 23;
; 	return (uint16_t)lrngSeed;
; }
lrng:
	php                ; push processor flags to stack (1 byte)
	rep #$20           ; 16-bit accumulator
	; lrngSeed ^= lrngSeed >> 17;
	lda.w lrngSeed + 2 ; high word to accumulator
	lsr A              ; shift accumulator right; set carry flag with shifted bit
	eor.w lrngSeed     ; xor accumulator with low word
	sta.w lrngSeed     ; store accumulator in low word
	; lrngSeed ^= lrngSeed << 15;
	lda.w lrngSeed     ; low word to accumulator
	ror A              ; rotate accumulator right; use previously set carry flag for the MSB; set carry flag with removed bit
	eor.w lrngSeed + 2 ; xor accumulator with high word
	sta.w lrngSeed + 2 ; store accumulator in high word
	lda.w #0           ; 0 to accumulator
	ror A              ; set MSB from carry flag
	eor.w lrngSeed     ; xor accumulator with low word
	sta.w lrngSeed     ; store accumulator in low word
	; lrngSeed ^= lrngSeed >> 23;
	sep #$20           ; 8-bit accumulator
	lda.w lrngSeed + 2 ; load low byte in high word to accumulator
	asl A              ; shift accumulator left; set carry flag with shifted bit
	lda.w lrngSeed + 3 ; load high byte in high word to accumulator
	rol A              ; rotate accumulator left; use previously set carry flag for the LSB; set carry flag with removed bit
	eor.w lrngSeed     ; xor accumulator with low byte in low word
	sta.w lrngSeed     ; store accumulator in low byte of low word
	lda #0             ; 0 to accumulator
	rol A              ; set LSB from carry flag
	eor.w lrngSeed + 1 ; xor accumulator with high byte in low word
	sta.w lrngSeed + 1 ; store accumulator in high byte of low word
	; return (uint16_t)lrngSeed;
	rep #$20           ; 16-bit accumulator
	lda.w lrngSeed     ; load low word to accumulator
	sta.b tcc__r0      ; store accumulator in return register
	plp                ; pull processor flags from stack (1 byte)
	rtl                ; return from subroutine long

.ends
