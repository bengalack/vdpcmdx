; ============================================================================
; vdp.s - assembler companion part for main.c (vdp + int + some handy routines)
; note: any symbol to be reached via C in SDCC is prefixed with an underscore
; any parameters are passed according to __sdcccall(1) found here:
; https://sdcc.sourceforge.net/doc/sdccman.pdf
; author: pal.hansen@gmail.com

    .allow_undocumented
    .area _CODE

; ----------------------------------------------------------------------------
; CONSTANTS
    BIOS_CHPUT  .equ 0x00A2
    CALSLT      .equ 0x001C
    RDSLT       .equ 0x000C
    EXPTBL      .equ 0xFCC1

    INIPLT      .equ 0x0141
    RSTPLT      .equ 0x0145
    ; BDOS        .equ 0x0005             ; "Basic Disk Operating System"
    ; BDOS_STROUT .equ 9                  ;.string output

    CHGMOD      .equ 0x005F             ; BIOS routine used to initialize the screen
    LINL40      .equ 0xF3AE             ; 40 or 80

    CHGCPU      .equ 0x0180             ; tame that turbo please
    GETCPU      .equ 0x0183

    VDPIO		.equ 0x98				; VRAM Data (Read/Write)
    VDPPORT1	.equ 0x99
    VDPSTREAM   .equ 0x9B

    VDP_REG1    .equ 0xF3E0             ; ram copy
    SCR_BITMASK .equ #0b01000000        ; to be used with VDP_REG1. Flag(1) means enabled

    VDP_REG8    .equ 0xFFE7             ; ram copy
    SPR_BITMASK .equ #0b00000010        ; to be used with VDP_REG8. Flag(1) means disabled

    VDP_REG9    .equ 0xFFE8             ; ram copy
    FRQ_BITMASK .equ #0b00000010        ; to be used with VDP_REG9. Flag(1) means PAL

    NMI         .equ 0x0066             ; subrom stuff
    EXTROM      .equ 0x015f             ; subrom stuff
    H_NMI       .equ 0xfdd6             ; subrom stuff

; ----------------------------------------------------------------------------
; EXTERNAL REFERENCES
    .globl      _g_bRecordingInitiated
    .globl      _g_bRecordingEnabled
    .globl      _g_bRecordingCPUHammering
    .globl      _g_uXPosL
    .globl      _HAMMERING_CODE_BLOCK
    ; .globl      _NON_ZERO_BLOCK

;-------------------------
; IN:  			A - the value
; 				reg - reg-number
; Modifies: A
; Set a value in reg number n
; Cost: 32 cycles (6 bytes)
;-------------------------
.macro vdpWriteReg reg
	out 	( VDPPORT1 ), a
	ld  	a, #reg | 0x80   ; sets write bit
	out 	( VDPPORT1 ), a
.endm

; ----------------------------------------------------------------------------
; MODIFIES: AF
;
; bool getPALRefreshRate();
_getPALRefreshRate::
    ld      a, (VDP_REG9)
    and     #FRQ_BITMASK
    srl     a
    ret

;-----------------------------------------------
;extern void vdpSpritesEnabled(bool bEnabled);
_vdpSpritesEnabled::

    or      a
    jr      z,disable_sprites

enable_sprites:
	ld		a,(VDP_REG8)
	and		#~SPR_BITMASK
	ld		(VDP_REG8), a
    
    jr      setup_done

disable_sprites:
	ld		a,(VDP_REG8)
	or		#SPR_BITMASK
	ld		(VDP_REG8), a

setup_done:

    di
    vdpWriteReg 8
    ei
    ret

;-----------------------------------------------
;extern void vdpScreenEnabled(bool bEnabled);
_vdpScreenEnabled::

    or      a
    jr      z,disable_screen

enable_screen:
	ld		a,(VDP_REG1)
	or		#SCR_BITMASK
	ld		(VDP_REG1), a
    
    jr      setup_done2

disable_screen:
	ld		a,(VDP_REG1)
	and		#~SCR_BITMASK
	ld		(VDP_REG1), a

setup_done2:

    di
    vdpWriteReg 1
    ei
    ret

; ----------------------------------------------------------------------------
; Enable VDP port #98 for start writing at address (A&3)DE 
; IN:       A:  Bits: 0W0000UU, W = Write, U means Upper VRAM address(bit 17-18)
;           DE: VRAM address, 16 lowest bits
; MODIFIES: AF, B, DE
; setVRAMAddress(u8 uBitCodes, u16 nVRAMAddress);
_setVRAMAddress::

    ld      b, a
    and     #3                      ; first bits

	rlc     d
	rla
	rlc     d
	rla
	srl     d
	srl     d

    di
	; out 	(VDPPORT1), a           ; set bits 14-16
	; ld  	a, #14|0x80             ; indicate value being a register by setting bit 7
	vdpWriteReg 14

	ld      a, e                    ; set bits 0-7
	out     (VDPPORT1), a

    ld      a, b                    ; prepare write flag in b
    and     #0b01000000
    ld      b, a   

	ld      a, d                    ; set bits 8-13
	or      b                       ; + write access via bit 6?
	out     (VDPPORT1), a       
    ei
    ret

; -----------------------------------------------------------------------------
; Just for fun, we make this loop as fast as possible
; (we put counter in HB(!), and use undocumented instruction)
; Reading from a rect, it seems like we need to allow 26 cycles between each
; read (we are at 41 at the shortest path)
; IN:       HL:  Count in pixels
; MODIFIES:
; u16 countWrittenPixels(u16 nNumPixels);  // HL: number of pixels in area to be read
_countWrittenPixelsNI::

    ; in a,(0x2e)

	ld	    a,#7                    ; select status reg 7
    vdpWriteReg 15

    ld      de,#0                   ; count

    ld      c,#VDPPORT1             ; port

    ld      a,h
    or      l

    ret     z                       ; return 0 if search are is empty

    ld      a,l
    or      a
    jr      z, amend
    inc     h                       ; because we will have a double loop, we must modify MSB
amend:
    ld      b,l

reado:
    ; in      f,(c)                   ; 14 cycles undocumented and avoids "or a" for testing for 0
    .db     0xed, 0x70              ; opcodes for "in f,(c)" - needed for sdcc assembler
    jr      z,skippy
    inc     de                      ; count filled pixel
skippy:
    djnz    reado                   ; this must be the fastest way to do 16-bits loops
    dec     h
    jp      nz, reado

    ; restore 0 as selected status reg
	xor a
    vdpWriteReg 15

    ret                             ; return value in DE

; -----------------------------------------------------------------------------
; MODIFIES: AF, BC, DE, HL
; void setVDPCmdParamsNI(u8 uPageCode, VDPParams* p); // uPageCode: SSSSDDDD, S=source page, D=dest page
_setVDPCmdParamsNI::
    ld      b,a
	ld    	a,#32				; Set "Stream mode"
    vdpWriteReg 17

	ld    	c, #VDPSTREAM

    ex      de,hl

    ld      a,(_g_uXPosL)
	out     (VDPSTREAM),a       ;SXL
    xor     a

	out     (VDPSTREAM),a       ;SXH
    nop                         ;obey speed

	out     (VDPSTREAM),a       ;SYL

    ld      a,b
    and     #0b11110000         ;also clears carry (needed!)
    rra
    rra
    rra
    rra
	out     (VDPSTREAM),a       ;SYH

    ld      a,(_g_uXPosL)
	out     (VDPSTREAM),a       ;DXL
    xor     a

	out     (VDPSTREAM),a       ;DXH
    nop                         ;obey speed

	out     (VDPSTREAM),a       ;DYL

    ld      a,b
    and     #0b00001111
	out     (VDPSTREAM),a       ;DYH (page)
    nop                         ;obey speed

.rept 6
	outi
.endm

	ret

; -----------------------------------------------------------------------------
; 
; void _executeCmdWithPreppedParams(u8 uCmd); // A
_executeCmdWithPreppedParamsNI::
	out   	(VDPSTREAM),a
    ret

;-------------------------
; Modifies: AF
; Wait for VDP-commands. No parameter should be given
; void waitForVDPCmd(void);
;-------------------------
_waitForVDPCmd::
	di
	ld	a,#2
    vdpWriteReg 15              ;select status register 2

	ei					        ; always happens AFTER next command
	in	a,(VDPPORT1)
	and #1
	jp	nz, _waitForVDPCmd      ; as this one allows interrupts, the interrupt will set another status reg, so we need to re-set it
    
	xor a
    vdpWriteReg 15              ; restore 0 as selected status reg

    ret

; ----------------------------------------------------------------------------
; Just a "trampoline", to ease in a call to the generated hammering/unrolled code.
; MODIFIES:
;
; void eternalVDPHammeringByCPU(void);
_eternalVDPHammeringByCPU::
    ; in a,(0x2e)
    ld      a,#0xFF                     ; used for OUT(VDPIO),a only
    ; ld      c,#VDPIO                    ; used for OUTI/INI only
    ; ld      hl,#_NON_ZERO_BLOCK         ; used for OUTI only
    ld      iy,#_HAMMERING_CODE_BLOCK   ; "call" this one, but we will return when eventually the interrupt kills it, and returns at caller
    jp      (iy)
 
; ----------------------------------------------------------------------------
;
; Totals:  cycles
; MODIFIES: (No registers of course!)
_customISR::
    push	af

    xor 	a                       ; get status for sreg 0
    vdpWriteReg 15
    nop								; obey speed
    in		a, (VDPPORT1)			; read VDP S#n to reset VBLANK IRQ

    ld      a, (_g_bRecordingEnabled)
    or      a
    jr      z, leave_isr

    ld      a, (_g_bRecordingInitiated)
    or      a
    jr      z, leave_isr

    ; in a,(0x2e)

	xor		a
    ld      (_g_bRecordingEnabled),a
    ld      (_g_bRecordingInitiated),a

    vdpWriteReg 46                  ; R#46 := 0 => stop vdp command

    ld      a,(_g_bRecordingCPUHammering)
    or      a
    jr      z, leave_isr

    ; in a,(0x2e)

    xor     a
    ld      (_g_bRecordingCPUHammering),a
    pop		af  ; we leave ISR too, but with a special twist
    inc     sp  ; should NOT affect any flags, and were good!
    inc     sp  ; should NOT affect any flags, and were good!
    ei
    ret

leave_isr:

    pop		af
    ei
    ret

; ----------------------------------------------------------------------------
; Print to console. Both '\r\n' is needed for a carriage return and newline.
; Heavy(!), as it does interslot calls per character (but print performance is
; of no concern in this program)
; IN:       HL - pointer to zero-terminated string
; MODIFIES: ? (BIOS...)
; void print(u8* szMessage)
_print::

    ; ; BDOS Variant (needs $ as ending character)
    ; ex      de, hl                  ; p to msg in de
    ; ld      c, #BDOS_STROUT         ; function code
    ; jp      BDOS

    ; BIOS variant (heavy)
    push    ix
loop:
	ld      a, (hl)
	and     a
	jr      z, leave_me
    ld      ix, #BIOS_CHPUT
    call    callSlot

	inc     hl
	jr      loop

leave_me:
    pop     ix
    ret

; ----------------------------------------------------------------------------
; Uses Matsushita device
; from here: https://map.grauw.nl/resources/msx_io_ports.php#expanded_io
;
; MODIFIES:     AF, BC
; RETURN:       A (bool)
;
; bool hasTurboFeature(void) __preserves_regs(d,e,h,l,iyl,iyh);
_hasTurboFeature::

    ld      b, #0           ; return value, default 0 (false)
    in      a,(0x40)
    cpl
    ld      c,a
    ld      a,#8
    out     (0x40),a        ; out the manufacturer code 8 (Panasonic) to I/O port 40h
    in      a,(0x40)        ; read the value you have just written
    cpl                     ; complement all bits of the value
    cp      #8              ; if it does not match the value you originally wrote,
    jr      nz,bye_bye      ; it does not have the Panasonic expanded I/O ports
    in      a,(0x41)
    bit     2,a             ; is turbo mode available?
    jr      nz,bye_bye
    ld      b, #1           ; yes, it is enabled

bye_bye:

    ld      a,c
    out     (0x40),a
    ld      a, b
    ret

; ----------------------------------------------------------------------------
; Uses Matsushita device
; from here: https://map.grauw.nl/resources/msx_io_ports.php#expanded_io
;
; MODIFIES:     AF, BC
; RETURN:       A (bool)
;
; bool isTurboEnabled(void) __preserves_regs(d,e,h,l,iyl,iyh);
_isTurboEnabled::

    ld      c,#0x40
    in      a,(c)
    cpl
    ld      b,a
    ld      a,#8
    out     (c),a        ; out the manufacturer code 8 (Panasonic) to I/O port 40h

    in      a,(0x41)
    rra                     ; bit 0: is turbo mode on? 0==on
    ld      a,#0
    jr      c,bye_bye2
    inc     a
bye_bye2:
    out     (c),b
    ret

; ----------------------------------------------------------------------------
; Uses Matsushita device
; from here: https://map.grauw.nl/resources/msx_io_ports.php#expanded_io
;
; bit 0 is turbo or not. 0=turbo, 1=normal
;
; MODIFIES: AF, BC, D
;
; void enableTurbo(bool bEnable) __preserves_regs(e,h,l,iyl,iyh);
_enableTurbo::

    xor     #1              ; flip the bit
    ld      b,a

    ld      c,#0x40
    in      a,(c)
    cpl
    ld      d,a
    ld      a,#8
    out     (c),a           ; out the manufacturer code 8 (Panasonic) to I/O port 40h

    in      a,(0x41)
    and     #0b11111110
    or      b
    out     (0x41),a        ; enable turbo(?)

    out     (c),d           ; retstore org device
    ret
    
; ----------------------------------------------------------------------------
; MSX version number http://map.grauw.nl/resources/msxsystemvars.php
;
; 0 = MSX 1
; 1 = MSX 2
; 2 = MSX 2+
; 3 = MSX turbo R
;
; MODIFIES: ? (BIOS...)
; u8 getMSXType()
_getMSXType::
    push    ix                  ; just in case, as SDCC is peculiar about this register
    ld      a, (EXPTBL)         ; BIOS slot
    ld      hl, #0x002D         ; Location to read
    di
    call    RDSLT               ; interslot call. RDSLT needs slot in A, returns value in A. address in HL
    pop     ix
    ret

; --------------------
; Tiny internal helper
; IN:       IX: address of BIOS routine
callSlot:
    ld     iy, (EXPTBL-1)       ;BIOS slot in iyh
    jp      CALSLT              ;interslot call

; ----------------------------------------------------------------------------
; https://map.grauw.nl/resources/msxbios.php#msxtrbios
; IN:  A = 0 0 0 0 0 0 x x
;                      0 0 = Z80 (ROM) mode
;                      0 1 = R800 ROM  mode
;                      1 0 = R800 DRAM mode
;
; MODIFIES: ? (BIOS...)
; void change CPU();
_changeCPU::

    push    ix
    ld      ix, #CHGCPU
    call    callSlot
    pop     ix
    ret

; ----------------------------------------------------------------------------
; https://map.grauw.nl/resources/msxbios.php#msxtrbios
; OUT: A = 0 0 0 0 0 0 x x
;                      0 0 = Z80 (ROM) mode
;                      0 1 = R800 ROM  mode
;                      1 0 = R800 DRAM mode
;
; MODIFIES: ? (BIOS...)
; u8 getCPU();
_getCPU::

    push    ix
    ld      ix, #GETCPU
    call    callSlot
    pop     ix
    ret

; ----------------------------------------------------------------------------
; Set screen.
; IN:       A - mode, as in screen (https://www.msx.org/wiki/SCREEN)
; MODIFIES: ? (BIOS...)
; u8 changeMode(u8 uModeNum)
_changeMode::

    push    ix
    ld      ix, #CHGMOD
    call    callSlot
    pop     ix
    ret

; ----------------------------------------------------------------------------
; Set linewidth, accepts 40 or 80, I believe. Needs changeMode call after this.
; IN:       A
; MODIFIES: ? (BIOS...)
; void setLineWidth(u8 uWidth)
_setLineWidth::
    ld     (LINL40),a
    ret

; ----------------------------------------------------------------------------
; CALSUB - from: https://map.grauw.nl/sources/callbios.php
;
; In: IX = address of routine in MSX2 SUBROM
;     AF, HL, DE, BC = parameters for the routine
;
; Out: AF, HL, DE, BC = depending on the routine
;
; Changes: IX, IY, AF', BC', DE', HL'
;
; Call MSX2 subrom from MSXDOS. Should work with all versions of MSXDOS.
;
; Notice: NMI hook will be changed. This should pose no problem as NMI is
; not supported on the MSX at all.
;
CALSUB:
    exx
    ex      af, af'       ; store all registers
    ld      hl, #EXTROM
    push    hl
    ld      hl, #0xC300
    push    hl           ; push NOP ; JP EXTROM
    push    ix
    ld      hl, #0x21DD
    push    hl           ; push LD IX,<entry>
    ld      hl, #0x3333
    push    hl           ; push INC SP; INC SP
    ld      hl, #0
    add     hl, sp        ; HL = offset of routine
    ld      a, #0xC3
    ld      (H_NMI), a
    ld      (H_NMI + 1), hl ; JP <routine> in NMI hook
    ex      af, af'
    exx                 ; restore all registers
    ld      ix, #NMI
    ld      iy, (EXPTBL - 1)
    call    CALSLT       ; call NMI-hook via NMI entry in ROMBIOS
                        ; NMI-hook will call SUBROM
    exx
    ex      af, af'       ; store all returned registers
    ld      hl, #10
    add     hl, sp
    ld      sp, hl        ; remove routine from stack
    ex      af, af'
    exx                 ; restore all returned registers
    ret

; ============================================================================
; HEAP / RAM
; 
    .area _HEAP