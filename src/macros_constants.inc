; ----------------------------------------------------------------------------
; COMMON CONSTANTS

    VDPIO		.equ 0x98				; VRAM Data (Read/Write)
    VDPPORT1	.equ 0x99
    VDPSTREAM   .equ 0x9B

;-------------------------
; IN:  			A - the value
; 				reg - reg-number
; Modifies: A
; Set a value in reg number n
; Cost: 32 cycles (6 bytes)
;-------------------------
.macro vdpWriteReg reg
	out 	(VDPPORT1),a
	ld  	a,#reg | 0x80   ; sets write bit
	out 	(VDPPORT1),a
.endm

