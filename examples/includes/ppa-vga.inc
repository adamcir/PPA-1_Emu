; PPA-VGA/1 shared ASM driver (automatic type-3 slot discovery).
; Include ppa-slots.inc first. Register bank is discovered; VRAM is its +1 bank.
; Mode 0 = 320x200x8 indexed color.

VGA_CONTROL EQU $0010
VGA_STATUS EQU $0011
VGA_MODE EQU $0012
VGA_BORDER EQU $0013
VGA_DAC_INDEX EQU $0014
VGA_DAC_R EQU $0015
VGA_DAC_G EQU $0016
VGA_DAC_B EQU $0017
VGA_WIDTH_LO EQU $0018
VGA_WIDTH_HI EQU $0019
VGA_HEIGHT_LO EQU $001A
VGA_HEIGHT_HI EQU $001B
VGA_PAGE_LO EQU $001C
VGA_PAGE_HI EQU $001D
VGA_BPP EQU $001E
VGA_PLANE_WRITE EQU $0020
VGA_PLANE_READ EQU $0021
VGA_VRAM_SIZE_LO EQU $0022
VGA_VRAM_SIZE_MI EQU $0023
VGA_VRAM_SIZE_HI EQU $0024

VGA_MODE_TEXT_02 EQU $02
VGA_MODE_TEXT_03 EQU $03
VGA_MODE_12 EQU $12
VGA_MODE_13 EQU $13
VGA_MODE_800X600_RGB EQU $80
VGA_MODE_1024X768_RGB EQU $81
VGA_MODE_1280X720_RGB EQU $82
VGA_MODE_1920X1080_RGB EQU $83

vga_init:
    CALL slots_bind_vga
    CMP A,#FF
    JZ .missing
    CALL slots_use_vga
    MOV A,#00
    ST $0012,A
    ST $0013,A
    MOV A,#01
    ST $0010,A
    RET
.missing:
    RET

vga_off:
    CALL slots_use_vga
    MOV A,#00
    ST $0010,A
    RET

; Modesetting uses bank 2000, register 0012h.
; The BIOS-style mode number is passed in A.
vga_set_mode:
    CALL slots_use_vga
    ST $0012,A
    RET

; Select 64 KiB VRAM aperture page (A=low byte, B=high byte).
; This changes the physical VRAM behind bank 2001.
vga_set_page:
    CALL slots_use_vga
    ST $001C,A
    MOV A,B
    ST $001D,A
    RET
