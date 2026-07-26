; ============================================================================
; bioshelper.s - assembler companion part for main.c (some handy dos+ routines)
; Note: Any symbol to be reached via C in SDCC is prefixed with an underscore.
; Any parameters are passed according to __sdcccall(1) found here:
; https://sdcc.sourceforge.net/doc/sdccman.pdf
; author: pal.hansen@gmail.com

    .allow_undocumented
    .area _CODE

.include "macros_constants.inc"

; ----------------------------------------------------------------------------
; LOCAL CONSTANTS

    BIOS_CHPUT  .equ 0x00A2
    CALSLT      .equ 0x001C
    RDSLT       .equ 0x000C
    EXPTBL      .equ 0xFCC1

    INIPLT      .equ 0x0141
    RSTPLT      .equ 0x0145
    BDOS        .equ 0x0005             ; "Basic Disk Operating System"
    BDOS_CONOUT .equ 0x02               ; DOS Function 02h (_CONOUT), char in E reg
    ; BDOS_STROUT .equ 9                  ;.string output
    BDOS_DOSVER .equ 0x6F
    BDOS_IOCTL  .equ 0x4B

    CHGMOD      .equ 0x005F             ; BIOS routine used to initialize the screen
    LINL40      .equ 0xF3AE             ; 40 or 80
    SCRMOD      .equ 0xFCAF             ; 

    CHGCPU      .equ 0x0180             ; tame that turbo please
    GETCPU      .equ 0x0183
    CHGET       .equ 0x009F

    ; NMI         .equ 0x0066             ; subrom stuff
    ; EXTROM      .equ 0x015f             ; subrom stuff
    ; H_NMI       .equ 0xfdd6             ; subrom stuff


; ----------------------------------------------------------------------------
; EXTERNAL REFERENCES
    .globl      _g_bRecordingInitiated
    .globl      _g_bRecordingEnabled
    .globl      _g_bRecordingCPUHammering
 
; ----------------------------------------------------------------------------
;
; Totals:  cycles
; MODIFIES: (No registers of course!)
_customISR::
    push	af

	ld		a, #1
    vdpWriteReg 15
    nop
	in		a, (VDPPORT1)			; Clear line int flag.
	; rra										; is the scanline-flag (bit 0) set?
  	; jp 		c, line_interrupts          ; we do not need to always do something...

    xor 	a                       ; get status for sreg 0 (we anyway need to reset sreg)
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

    push    ix

loop:
	ld      a, (hl)
	and     a
	jr      z, leave_me

    ld      e,a
    ld      c,#BDOS_CONOUT

    push    hl
    call    BDOS
    pop     hl

	inc     hl
	jr      loop

;     ; BIOS variant (does not support re-direction)
;     push    ix
; loop:
; 	ld      a, (hl)
; 	and     a
; 	jr      z, leave_me
;     ld      ix, #BIOS_CHPUT
;     call    callSlot

; 	inc     hl
; 	jr      loop

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
; OUT:      A - previous mode
; MODIFIES: ? (BIOS...)
; u8 changeMode(u8 uModeNum)
_changeMode::

    ld      b,a
    ld      a,(SCRMOD)
    push    af
    ld      a,b

    push    ix
    ld      ix, #CHGMOD
    call    callSlot
    pop     ix

    pop     af
    ret


; ----------------------------------------------------------------------------
; Just wait until a key is pressed.
; IN:
; OUT:      a - key code
; MODIFIES: ? (BIOS...)
; u8     waitForKey(void);
_waitForKey::

    push    ix
    ld      ix, #CHGET
    call    callSlot
    pop     ix

    ret

; ----------------------------------------------------------------------------
; Check what user has issued at command line.
; IN:
; OUT:      a - boolean, true if user output is to screen, false if redirected to file/pipe
; MODIFIES: ? (BIOS...)
; bool userOutputsToScreen(void);
_userOutputsToScreen::
    ld      c,#BDOS_DOSVER
    call    BDOS
    or      a               ; If A != 0, it's MSX-DOS 1 (Function 6Fh doesn't exist)
    jr      nz,.is_dos1
    
    ld      a,b             ; B holds the MSX-DOS kernel major version
    cp      #2
    jr      c,.is_dos1      ; If version < 2, treat as DOS 1

    ; 2. We are on MSX-DOS 2+: Safe to call _IOCTL
    ld      c,#BDOS_IOCTL
    ld      a,#0            ; Subfunction 0: Get channel attributes
    ld      b,#1            ; Handle 1 = STDOUT
    call    BDOS

    or      a               ; Check error code
    jr      nz,.not_piped   ; If call failed, default to standard screen output

    bit     7,e             ; Bit 7 = 0 if redirected to a file/pipe
    jr      z,.is_piped
    jr      .not_piped

.is_dos1:                   ; MSX-DOS 1 has no redirection/pipes, output is always directly to console
.not_piped:                 ; Direct screen output
    ld a,#1                 ; bool:true
    ret

.is_piped:                  ; Redirected output
    xor a                   ; bool:false
    ret

; ----------------------------------------------------------------------------
; Set linewidth, accepts 40 or 80, I believe. Needs changeMode call after this.
; IN:       A
; MODIFIES: ? (BIOS...)
; void setLineWidth(u8 uWidth)
_setLineWidth::
    ld     (LINL40),a
    ret

; ; ----------------------------------------------------------------------------
; ; CALSUB - from: https://map.grauw.nl/sources/callbios.php
; ;
; ; In: IX = address of routine in MSX2 SUBROM
; ;     AF, HL, DE, BC = parameters for the routine
; ;
; ; Out: AF, HL, DE, BC = depending on the routine
; ;
; ; Changes: IX, IY, AF', BC', DE', HL'
; ;
; ; Call MSX2 subrom from MSXDOS. Should work with all versions of MSXDOS.
; ;
; ; Notice: NMI hook will be changed. This should pose no problem as NMI is
; ; not supported on the MSX at all.
; ;
; CALSUB:
;     exx
;     ex      af, af'       ; store all registers
;     ld      hl, #EXTROM
;     push    hl
;     ld      hl, #0xC300
;     push    hl           ; push NOP ; JP EXTROM
;     push    ix
;     ld      hl, #0x21DD
;     push    hl           ; push LD IX,<entry>
;     ld      hl, #0x3333
;     push    hl           ; push INC SP; INC SP
;     ld      hl, #0
;     add     hl, sp        ; HL = offset of routine
;     ld      a, #0xC3
;     ld      (H_NMI), a
;     ld      (H_NMI + 1), hl ; JP <routine> in NMI hook
;     ex      af, af'
;     exx                 ; restore all registers
;     ld      ix, #NMI
;     ld      iy, (EXPTBL - 1)
;     call    CALSLT       ; call NMI-hook via NMI entry in ROMBIOS
;                         ; NMI-hook will call SUBROM
;     exx
;     ex      af, af'       ; store all returned registers
;     ld      hl, #10
;     add     hl, sp
;     ld      sp, hl        ; remove routine from stack
;     ex      af, af'
;     exx                 ; restore all returned registers
;     ret
