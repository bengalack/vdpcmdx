; CRT - C Runtime, SDCC needs this. Must be first and sets a starting point,
; a link to main-function and some variables and their order.
; MSXDOS starts at 0x100
; (derived from https://github.com/Konamiman/MSX/blob/master/SRC/SDCC/crt0-msxdos/crt0msx_msxdos_advanced.asm)
; author: pal.hansen@gmail.com
;

	.globl	_main

	.area _HEADER (ABS)
	.org    0x0100

    call    _main

    ; Standard program termination. 
    ld      c, #0x62                ; DOS 2 function for program termination. Termination code for DOS 2 was returned in a from _main
    ld      b, a
    call    5			            ; On DOS 2 this terminates; on DOS 1 this returns...
    ld      c, #0x0
    jp      5			            ; ... and then this one terminates (DOS 1 function for program termination).

    .area _HOME
	.area _CODE                     ; Note: The location of THIS variable is the input for the SDCC compiler (--code-loc)
	.area _GSINIT
	.area _GSFINAL
	.area _INITIALIZER
	.area _ROMDATA
	.area _DATA
	.area _INITIALIZED
	.area _HEAP
