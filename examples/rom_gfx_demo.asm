; PPA-1 game/graphics library ROM-image demo.
; (c) 2026 Adam Cir (Adava), Adava Software, Adava Development
;
; Requires PPA-VGA/1 slot 1 (bank 2000/2001), PPA-PS/2/1 slot 2.
; To install a 320x200 BMP into ROM 0001 use:
;   make rom-graphics-demo BMP=path/to/exported-from-gimp.bmp
; Then load out/examples/rom_gfx_demo_packed.bin in the PPA-1 emulator.
; W: redraw the packed BMP from ROM to VGA. Without one, fallback stripes.
.org $0000
    JMP start

.include "includes/ppa-vga.inc"
.include "includes/ppa-ps2.inc"
.include "includes/ppa-gfx.inc"
.include "includes/ppa-game.inc"

start:
    CALL vga_init
    MOV A,#13
    CALL vga_set_mode
    CALL game_keyboard_init
    CALL show_image

loop:
    CALL game_poll_key
    CMP A,#1D
    JNZ .idle
    CMP B,#00
    JNZ .idle
    CALL show_image
.idle:
    MOV D,#03
    CALL game_delay
    JMP loop

show_image:
    MOV A,#01
    CALL gfx_show_screen
    CMP A,#01
    JZ .done
    ; No PIMG in EEPROM bank 0001: diagnostic solid fill.
    MOV BR-HI,#20
    MOV BR-LO,#01
    MOV X-HI,#00
    MOV X-LO,#00
    MOV A,#01
.fill:
    ST X,A
    INC X-LO
    JNZ .check
    INC X-HI
.check:
    CMP X-HI,#FA
    JNZ .fill
    CMP X-LO,#00
    JNZ .fill
.done:
    RET
