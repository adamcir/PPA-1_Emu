; PPA-1 Metro Driver - Ecs cab driving game
; (c) 2026 Adam Cir (Adava), Adava Software, Adava Development
;
; Required modules:
;   SLOT 1 = PPA-VGA/1   (register bank 2000, VRAM bank 2001)
;   SLOT 2 = PPA-PS/2/1  (register bank 3000)
;   SLOT 3 = PPA-SOUND/1 (register bank 4000)
;
; Controls:
;   W = traction / accelerate
;   S = brake
;
; The route loops through three simple stations:
;   D = DEPO, C = CENTRUM, L = LUKA
;
; Recommended CPU clock: about 1 MHz.

.org $0000
    JMP start

.include "includes/ppa-vga.inc"
.include "includes/ppa-ps2.inc"

; PPA-SOUND/1 registers in SLOT 3 / bank 4000.
SND_ON          EQU $0010
SND_MASTER_VOL  EQU $0011
SND_CH1_LO      EQU $0020
SND_CH1_HI      EQU $0021
SND_CH1_WAVE    EQU $0022
SND_CH1_VOL     EQU $0023
SND_CH1_ENABLE  EQU $0024

; RAM variables in bank 1000.
VAR_SPEED       EQU $0000
VAR_W_HELD      EQU $0001
VAR_S_HELD      EQU $0002
VAR_BREAK       EQU $0003
VAR_POS_LO      EQU $0004
VAR_POS_HI      EQU $0005
VAR_SCENE       EQU $0006
VAR_FRICTION    EQU $0007

MAX_SPEED       EQU $28

start:
    ; VGA: 320x200x8 mode 13h.
    CALL vga_init
    MOV A,#13
    CALL vga_set_mode

    ; Clear game state.
    MOV BR-HI,#10
    MOV BR-LO,#00
    MOV A,#00
    ST $0000,A
    ST $0001,A
    ST $0002,A
    ST $0003,A
    ST $0004,A
    ST $0005,A
    ST $0007,A
    MOV A,#FF
    ST $0006,A

    ; PS/2 keyboard in SLOT 2: scan on + flush FIFO.
    MOV BR-HI,#30
    MOV BR-LO,#00
    MOV A,#81
    ST $0012,A

    ; Sound card in SLOT 3. Triangle traction motor on channel 1.
    CALL sound_init_slot3

    ; Draw the Ecs-style light-gray control desk once.
    CALL draw_panel_static
    CALL update_scene
    CALL draw_panel_dynamic

main_loop:
    CALL poll_keyboard
    CALL update_physics
    CALL update_scene
    CALL draw_motion
    CALL draw_panel_dynamic
    CALL update_sound
    CALL frame_delay
    JMP main_loop

; ---------------------------------------------------------------------------
; Keyboard
; ---------------------------------------------------------------------------

poll_keyboard:
.poll:
    MOV BR-HI,#30
    MOV BR-LO,#00
    LD A,$0011
    AND A,#01
    JZ .done

    LD A,$0010
    CMP A,#F0
    JZ .break_prefix
    CMP A,#E0
    JZ .poll

    MOV B,A
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0003
    CMP A,#00
    JZ .make_code

    ; Break code: clear pending prefix and release key.
    MOV A,#00
    ST $0003,A
    CMP B,#1D
    JZ .w_up
    CMP B,#1B
    JZ .s_up
    JMP .poll

.make_code:
    CMP B,#1D
    JZ .w_down
    CMP B,#1B
    JZ .s_down
    JMP .poll

.break_prefix:
    MOV BR-HI,#10
    MOV BR-LO,#00
    MOV A,#01
    ST $0003,A
    JMP .poll

.w_down:
    MOV A,#01
    ST $0001,A
    JMP .poll

.s_down:
    MOV A,#01
    ST $0002,A
    JMP .poll

.w_up:
    MOV A,#00
    ST $0001,A
    JMP .poll

.s_up:
    MOV A,#00
    ST $0002,A
    JMP .poll

.done:
    RET

; ---------------------------------------------------------------------------
; Driving physics
; ---------------------------------------------------------------------------

update_physics:
    MOV BR-HI,#10
    MOV BR-LO,#00

    ; Brake wins if both keys are held.
    LD A,$0002
    CMP A,#00
    JNZ .brake

    LD A,$0001
    CMP A,#00
    JNZ .traction

    ; Coasting friction: lose 1 speed unit every 8 frames.
    LD A,$0007
    INC A
    CMP A,#08
    JNZ .store_friction
    MOV A,#00
    ST $0007,A
    LD A,$0000
    CMP A,#00
    JZ .advance
    DEC A
    ST $0000,A
    JMP .advance

.store_friction:
    ST $0007,A
    JMP .advance

.traction:
    MOV A,#00
    ST $0007,A
    LD A,$0000
    CMP A,#28
    JNC .advance
    INC A
    ST $0000,A
    JMP .advance

.brake:
    MOV A,#00
    ST $0007,A
    LD A,$0000
    CMP A,#00
    JZ .advance
    SUB A,#02
    JC .brake_zero
    ST $0000,A
    JMP .advance

.brake_zero:
    MOV A,#00
    ST $0000,A

.advance:
    ; 16-bit route position += speed.
    LD B,$0000
    CMP B,#00
    JZ .done

    LD A,$0004
    ADD A,B
    ST $0004,A
    JNC .done
    LD A,$0005
    INC A
    ST $0005,A

.done:
    RET

; ---------------------------------------------------------------------------
; Route / station scene selection
; scene 0 = DEPO, 1 = tunnel, 2 = CENTRUM, 3 = LUKA
; ---------------------------------------------------------------------------

update_scene:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0005

    CMP A,#10
    JC .want_depo
    CMP A,#60
    JC .want_tunnel
    CMP A,#70
    JC .want_centrum
    CMP A,#C0
    JC .want_tunnel
    CMP A,#D0
    JC .want_luka
    JMP .want_tunnel

.want_depo:
    MOV B,#00
    JMP .set

.want_tunnel:
    MOV B,#01
    JMP .set

.want_centrum:
    MOV B,#02
    JMP .set

.want_luka:
    MOV B,#03

.set:
    LD A,$0006
    CMP A,B
    JZ .done
    MOV A,B
    ST $0006,A

    CMP B,#00
    JZ .draw_depo
    CMP B,#02
    JZ .draw_centrum
    CMP B,#03
    JZ .draw_luka
    CALL draw_tunnel
    JMP .done

.draw_depo:
    CALL draw_station_depo
    JMP .done

.draw_centrum:
    CALL draw_station_centrum
    JMP .done

.draw_luka:
    CALL draw_station_luka

.done:
    RET

; ---------------------------------------------------------------------------
; Sound - SLOT 3 / bank 4000
; ---------------------------------------------------------------------------

sound_init_slot3:
    MOV BR-HI,#40
    MOV BR-LO,#00
    MOV A,#C0
    ST $0011,A
    MOV A,#01
    ST $0010,A
    MOV A,#02
    ST $0022,A
    MOV A,#A0
    ST $0023,A
    MOV A,#01
    ST $0024,A
    MOV A,#00
    ST $0020,A
    ST $0021,A
    RET

update_sound:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD B,$0000
    CMP B,#00
    JZ .silent

    ; 80 Hz + speed*4, up to 240 Hz.
    SHL B
    SHL B
    ADD B,#50

    MOV BR-HI,#40
    MOV BR-LO,#00
    ST $0020,B
    MOV A,#00
    ST $0021,A
    MOV A,#01
    ST $0024,A
    RET

.silent:
    MOV BR-HI,#40
    MOV BR-LO,#00
    MOV A,#00
    ST $0020,A
    ST $0021,A
    RET

; ---------------------------------------------------------------------------
; VGA drawing helpers
; fill_rect input:
;   Y = top-left VRAM offset
;   A = palette index
;   B = width  (1..255)
;   C = height (1..255)
; Clobbers X, D, C and Y. Keeps A and B.
; ---------------------------------------------------------------------------

fill_rect:
    MOV BR-HI,#20
    MOV BR-LO,#01
.row:
    MOV X-HI,Y-HI
    MOV X-LO,Y-LO
    MOV D,B
.pixel:
    ST X,A
    INC X-LO
    JNZ .pixel_no_wrap
    INC X-HI
.pixel_no_wrap:
    DEC D
    JNZ .pixel

    ; Y += 320 (0140h)
    ADD Y-LO,#40
    JNC .row_no_carry
    INC Y-HI
.row_no_carry:
    INC Y-HI

    DEC C
    JNZ .row
    RET

; Fill viewport y=0..129 (0000..A27F) with A.
fill_view:
    MOV BR-HI,#20
    MOV BR-LO,#01
    MOV X-HI,#00
    MOV X-LO,#00
.loop:
    ST X,A
    INC X-LO
    JNZ .check
    INC X-HI
.check:
    CMP X-HI,#96
    JNZ .loop
    CMP X-LO,#00
    JNZ .loop
    RET

; Lower instrument area, y=120..199.
fill_panel:
    MOV BR-HI,#20
    MOV BR-LO,#01
    MOV X-HI,#96
    MOV X-LO,#00
.loop:
    ST X,A
    INC X-LO
    JNZ .check
    INC X-HI
.check:
    CMP X-HI,#FA
    JNZ .loop
    CMP X-LO,#00
    JNZ .loop
    RET

; Tiny 3x5 pixel font. ROM strings and font; no host-side rendering.
; Input: X=zero-terminated ROM string, Y=VRAM offset, A=palette color.
; Scratch variables in RAM bank 1000:0010..0014.
draw_text:
    MOV BR-HI,#10
    MOV BR-LO,#00
    ST $0010,X-HI
    ST $0011,X-LO
    ST $0012,Y-HI
    ST $0013,Y-LO
    ST $0014,A
.next_char:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD X-HI,$0010
    LD X-LO,$0011
    MOV BR-HI,#00
    LD B,X
    INC X-LO
    JNZ .ptr_ok
    INC X-HI
.ptr_ok:
    MOV BR-HI,#10
    MOV BR-LO,#00
    ST $0010,X-HI
    ST $0011,X-LO
    CMP B,#00
    JZ .done
    ; Font rows are indexed by ASCII from 20h through 5Ah.
    SUB B,#20
    MOV C,B
    SHL B
    SHL B
    ADD B,C
    MOV X-HI,#60
    MOV X-LO,B
    JNC .font_ok
    INC X-HI
.font_ok:
    LD Y-HI,$0012
    LD Y-LO,$0013
    LD A,$0014
    MOV D,#05
.row:
    MOV BR-HI,#00
    MOV BR-LO,#00
    LD C,X
    INC X-LO
    JNZ .font_ptr_ok
    INC X-HI
.font_ptr_ok:
    MOV B,C
    AND B,#04
    JZ .skip_p1
    MOV BR-HI,#20
    MOV BR-LO,#01
    ST Y,A
.skip_p1:
    INC Y-LO
    JNZ .x1_ok
    INC Y-HI
.x1_ok:
    MOV B,C
    AND B,#02
    JZ .skip_p2
    MOV BR-HI,#20
    MOV BR-LO,#01
    ST Y,A
.skip_p2:
    INC Y-LO
    JNZ .x2_ok
    INC Y-HI
.x2_ok:
    MOV B,C
    AND B,#01
    JZ .skip_p3
    MOV BR-HI,#20
    MOV BR-LO,#01
    ST Y,A
.skip_p3:
    INC Y-LO
    JNZ .x3_ok
    INC Y-HI
.x3_ok:
    ; Y advanced three pixels: add 317 to move down one 320px row.
    ADD Y-LO,#3D
    JNC .row_no_carry
    INC Y-HI
.row_no_carry:
    INC Y-HI
    DEC D
    JNZ .row

    MOV BR-HI,#10
    MOV BR-LO,#00
    LD Y-HI,$0012
    LD Y-LO,$0013
    ADD Y-LO,#04
    JNC .next_x_ok
    INC Y-HI
.next_x_ok:
    ST $0012,Y-HI
    ST $0013,Y-LO
    JMP .next_char
.done:
    RET

draw_panel_static:
    MOV A,#07
    CALL fill_panel
    ; Ecs: pale steel, two-tier console, wood trim and panel edges.
    MOV Y-HI,#96
    MOV Y-LO,#00
    MOV A,#08
    MOV B,#A0
    MOV C,#05
    CALL fill_rect
    MOV Y-HI,#96
    MOV Y-LO,#A0
    MOV A,#08
    MOV B,#A0
    MOV C,#05
    CALL fill_rect
    MOV Y-HI,#9B
    MOV Y-LO,#00
    MOV A,#0F
    MOV B,#A0
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#9B
    MOV Y-LO,#A0
    MOV A,#0F
    MOV B,#A0
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#C8
    MOV Y-LO,#00
    MOV A,#08
    MOV B,#A0
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#C8
    MOV Y-LO,#A0
    MOV A,#08
    MOV B,#A0
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#CD
    MOV Y-LO,#00
    MOV A,#07
    MOV B,#A0
    MOV C,#1F
    CALL fill_rect
    MOV Y-HI,#CD
    MOV Y-LO,#A0
    MOV A,#07
    MOV B,#A0
    MOV C,#1F
    CALL fill_rect
    MOV Y-HI,#F3
    MOV Y-LO,#C0
    MOV A,#06
    MOV B,#A0
    MOV C,#05
    CALL fill_rect
    MOV Y-HI,#F4
    MOV Y-LO,#60
    MOV A,#06
    MOV B,#A0
    MOV C,#05
    CALL fill_rect
    MOV Y-HI,#CB
    MOV Y-LO,#C0
    MOV A,#08
    MOV B,#04
    MOV C,#20
    CALL fill_rect
    MOV Y-HI,#CC
    MOV Y-LO,#FB
    MOV A,#08
    MOV B,#05
    MOV C,#20
    CALL fill_rect
    MOV Y-HI,#CD
    MOV Y-LO,#01
    MOV A,#0F
    MOV B,#03
    MOV C,#1F
    CALL fill_rect
    MOV Y-HI,#CE
    MOV Y-LO,#37
    MOV A,#08
    MOV B,#04
    MOV C,#1F
    CALL fill_rect
    MOV Y-HI,#98
    MOV Y-LO,#80
    MOV A,#00
    MOV B,#03
    MOV C,#26
    CALL fill_rect
    MOV Y-HI,#99
    MOV Y-LO,#BD
    MOV A,#00
    MOV B,#03
    MOV C,#26
    CALL fill_rect
    MOV Y-HI,#A1
    MOV Y-LO,#47
    MOV A,#0F
    MOV B,#92
    MOV C,#1B
    CALL fill_rect
    MOV Y-HI,#A1
    MOV Y-LO,#E1
    MOV A,#0F
    MOV B,#98
    MOV C,#1B
    CALL fill_rect
    MOV Y-HI,#A1
    MOV Y-LO,#47
    MOV A,#08
    MOV B,#92
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#A1
    MOV Y-LO,#E1
    MOV A,#08
    MOV B,#98
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#AA
    MOV Y-LO,#0B
    MOV A,#08
    MOV B,#27
    MOV C,#13
    CALL fill_rect
    MOV Y-HI,#AC
    MOV Y-LO,#8D
    MOV A,#0F
    MOV B,#23
    MOV C,#0F
    CALL fill_rect
    MOV Y-HI,#BC
    MOV Y-LO,#CE
    MOV A,#08
    MOV B,#21
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#11
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#19
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#21
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#29
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B0
    MOV Y-LO,#5D
    MOV A,#0C
    MOV B,#02
    MOV C,#09
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_BAT-5000
    MOV Y-HI,#A1
    MOV Y-LO,#4E
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_V-5000
    MOV Y-HI,#C3
    MOV Y-LO,#19
    MOV A,#00
    CALL draw_text
    MOV Y-HI,#AA
    MOV Y-LO,#39
    MOV A,#08
    MOV B,#27
    MOV C,#13
    CALL fill_rect
    MOV Y-HI,#AC
    MOV Y-LO,#BB
    MOV A,#0F
    MOV B,#23
    MOV C,#0F
    CALL fill_rect
    MOV Y-HI,#BC
    MOV Y-LO,#FC
    MOV A,#08
    MOV B,#21
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#3F
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#47
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#4F
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#57
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B0
    MOV Y-LO,#99
    MOV A,#0C
    MOV B,#02
    MOV C,#09
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_PROUD-5000
    MOV Y-HI,#A1
    MOV Y-LO,#7C
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_A-5000
    MOV Y-HI,#C3
    MOV Y-LO,#47
    MOV A,#00
    CALL draw_text
    MOV Y-HI,#AA
    MOV Y-LO,#66
    MOV A,#08
    MOV B,#26
    MOV C,#13
    CALL fill_rect
    MOV Y-HI,#AC
    MOV Y-LO,#E8
    MOV A,#0F
    MOV B,#22
    MOV C,#0F
    CALL fill_rect
    MOV Y-HI,#BD
    MOV Y-LO,#29
    MOV A,#08
    MOV B,#20
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#6C
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#74
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#7C
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#84
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B0
    MOV Y-LO,#B4
    MOV A,#0C
    MOV B,#02
    MOV C,#09
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_TRAK-5000
    MOV Y-HI,#A1
    MOV Y-LO,#A9
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_V-5000
    MOV Y-HI,#C3
    MOV Y-LO,#74
    MOV A,#00
    CALL draw_text
    MOV Y-HI,#AA
    MOV Y-LO,#A9
    MOV A,#08
    MOV B,#34
    MOV C,#13
    CALL fill_rect
    MOV Y-HI,#AD
    MOV Y-LO,#2B
    MOV A,#0F
    MOV B,#30
    MOV C,#0F
    CALL fill_rect
    MOV Y-HI,#BD
    MOV Y-LO,#6C
    MOV A,#08
    MOV B,#2E
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#AF
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#BC
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#C9
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#D6
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_RYCH-5000
    MOV Y-HI,#A1
    MOV Y-LO,#EC
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_KMH-5000
    MOV Y-HI,#C3
    MOV Y-LO,#BE
    MOV A,#00
    CALL draw_text
    MOV Y-HI,#AA
    MOV Y-LO,#E6
    MOV A,#08
    MOV B,#27
    MOV C,#13
    CALL fill_rect
    MOV Y-HI,#AD
    MOV Y-LO,#68
    MOV A,#0F
    MOV B,#23
    MOV C,#0F
    CALL fill_rect
    MOV Y-HI,#BD
    MOV Y-LO,#A9
    MOV A,#08
    MOV B,#21
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#EC
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#F4
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#AF
    MOV Y-LO,#FC
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B0
    MOV Y-LO,#04
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B1
    MOV Y-LO,#41
    MOV A,#0C
    MOV B,#02
    MOV C,#09
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_TM-5000
    MOV Y-HI,#A2
    MOV Y-LO,#29
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_BAR-5000
    MOV Y-HI,#C3
    MOV Y-LO,#F4
    MOV A,#00
    CALL draw_text
    MOV Y-HI,#AB
    MOV Y-LO,#14
    MOV A,#08
    MOV B,#26
    MOV C,#13
    CALL fill_rect
    MOV Y-HI,#AD
    MOV Y-LO,#96
    MOV A,#0F
    MOV B,#22
    MOV C,#0F
    CALL fill_rect
    MOV Y-HI,#BD
    MOV Y-LO,#D7
    MOV A,#08
    MOV B,#20
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#B0
    MOV Y-LO,#1A
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B0
    MOV Y-LO,#22
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B0
    MOV Y-LO,#2A
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B0
    MOV Y-LO,#32
    MOV A,#08
    MOV B,#01
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#B1
    MOV Y-LO,#66
    MOV A,#0C
    MOV B,#02
    MOV C,#09
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_TC-5000
    MOV Y-HI,#A2
    MOV Y-LO,#57
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_BAR-5000
    MOV Y-HI,#C4
    MOV Y-LO,#22
    MOV A,#00
    CALL draw_text
    MOV Y-HI,#A6
    MOV Y-LO,#D1
    MOV A,#00
    MOV B,#12
    MOV C,#17
    CALL fill_rect
    MOV Y-HI,#AA
    MOV Y-LO,#96
    MOV A,#04
    MOV B,#08
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#B2
    MOV Y-LO,#16
    MOV A,#0E
    MOV B,#08
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#B9
    MOV Y-LO,#96
    MOV A,#0A
    MOV B,#08
    MOV C,#05
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_ARS-5000
    MOV Y-HI,#9F
    MOV Y-LO,#52
    MOV A,#00
    CALL draw_text
    ; Bakelite buttons and lighting/door/communication labels.
    MOV Y-HI,#CF
    MOV Y-LO,#FE
    MOV A,#0F
    MOV B,#B7
    MOV C,#1C
    CALL fill_rect
    MOV Y-HI,#CF
    MOV Y-LO,#FE
    MOV A,#08
    MOV B,#B7
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#F0
    MOV Y-LO,#7E
    MOV A,#08
    MOV B,#B7
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#D6
    MOV Y-LO,#40
    MOV A,#07
    MOV B,#B3
    MOV C,#13
    CALL fill_rect
    MOV Y-HI,#D6
    MOV Y-LO,#40
    MOV A,#08
    MOV B,#B3
    MOV C,#01
    CALL fill_rect
    MOV Y-HI,#DB
    MOV Y-LO,#4B
    MOV A,#00
    MOV B,#0C
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#DC
    MOV Y-LO,#8C
    MOV A,#0C
    MOV B,#0A
    MOV C,#08
    CALL fill_rect
    MOV Y-HI,#DB
    MOV Y-LO,#6B
    MOV A,#00
    MOV B,#0C
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#DC
    MOV Y-LO,#AC
    MOV A,#09
    MOV B,#0A
    MOV C,#08
    CALL fill_rect
    MOV Y-HI,#DB
    MOV Y-LO,#8C
    MOV A,#00
    MOV B,#0C
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#DC
    MOV Y-LO,#CD
    MOV A,#0E
    MOV B,#0A
    MOV C,#08
    CALL fill_rect
    MOV Y-HI,#DB
    MOV Y-LO,#AC
    MOV A,#00
    MOV B,#0C
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#DC
    MOV Y-LO,#ED
    MOV A,#0A
    MOV B,#0A
    MOV C,#08
    CALL fill_rect
    MOV Y-HI,#D6
    MOV Y-LO,#D2
    MOV A,#08
    MOV B,#1F
    MOV C,#14
    CALL fill_rect
    MOV Y-HI,#DA
    MOV Y-LO,#95
    MOV A,#0F
    MOV B,#19
    MOV C,#0F
    CALL fill_rect
    MOV Y-HI,#E2
    MOV Y-LO,#1B
    MOV A,#00
    MOV B,#0E
    MOV C,#07
    CALL fill_rect
    MOV Y-HI,#D8
    MOV Y-LO,#25
    MOV A,#08
    MOV B,#04
    MOV C,#12
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_OSV-5000
    MOV Y-HI,#D1
    MOV Y-LO,#49
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_DVERE-5000
    MOV Y-HI,#D1
    MOV Y-LO,#61
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_ZVON-5000
    MOV Y-HI,#D1
    MOV Y-LO,#88
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_ARS-5000
    MOV Y-HI,#D1
    MOV Y-LO,#AC
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_BRZDA-5000
    MOV Y-HI,#ED
    MOV Y-LO,#56
    MOV A,#00
    CALL draw_text
    ; Heavy Ecs traction controller: slot and metal base.
    MOV Y-HI,#CF
    MOV Y-LO,#93
    MOV A,#08
    MOV B,#67
    MOV C,#1B
    CALL fill_rect
    MOV Y-HI,#D2
    MOV Y-LO,#15
    MOV A,#0F
    MOV B,#63
    MOV C,#17
    CALL fill_rect
    MOV Y-HI,#CF
    MOV Y-LO,#A5
    MOV A,#07
    MOV B,#4C
    MOV C,#16
    CALL fill_rect
    MOV Y-HI,#D3
    MOV Y-LO,#75
    MOV A,#08
    MOV B,#17
    MOV C,#14
    CALL fill_rect
    MOV Y-HI,#D0
    MOV Y-LO,#FE
    MOV A,#00
    MOV B,#09
    MOV C,#18
    CALL fill_rect
    MOV Y-HI,#EB
    MOV Y-LO,#1A
    MOV A,#08
    MOV B,#60
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#F0
    MOV Y-LO,#16
    MOV A,#06
    MOV B,#66
    MOV C,#03
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_CTL-5000
    MOV Y-HI,#F1
    MOV Y-LO,#59
    MOV A,#0F
    CALL draw_text
    ; Actual top gauge indicator baseline and fixed speed scale.
    MOV Y-HI,#B9
    MOV Y-LO,#AD
    MOV A,#00
    MOV B,#28
    MOV C,#04
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_40-5000
    MOV Y-HI,#AD
    MOV Y-LO,#3E
    MOV A,#08
    CALL draw_text
    MOV Y-HI,#9E
    MOV Y-LO,#C5
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#9F
    MOV Y-LO,#5A
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#9F
    MOV Y-LO,#63
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#9F
    MOV Y-LO,#F7
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#C4
    MOV Y-LO,#45
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#C5
    MOV Y-LO,#77
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#EE
    MOV Y-LO,#C8
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#EF
    MOV Y-LO,#37
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#CE
    MOV Y-LO,#C2
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#CF
    MOV Y-LO,#73
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#F0
    MOV Y-LO,#82
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#F1
    MOV Y-LO,#33
    MOV A,#08
    MOV B,#02
    MOV C,#02
    CALL fill_rect
    RET

draw_panel_dynamic:
    ; Real speed indicator 0..40, with indicator lights.
    MOV Y-HI,#B9
    MOV Y-LO,#AD
    MOV A,#00
    MOV B,#28
    MOV C,#04
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD B,$0000
    CMP B,#00
    JZ .bar_done
    MOV Y-HI,#B9
    MOV Y-LO,#AD
    MOV A,#0A
    MOV C,#04
    CALL fill_rect
.bar_done:
    ; W/S press state lights in the lower panel.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD D,$0001
    MOV A,#08
    CMP D,#00
    JZ .light_w
    MOV A,#0A
.light_w:
    MOV Y-HI,#E7
    MOV Y-LO,#DE
    MOV B,#06
    MOV C,#04
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD D,$0002
    MOV A,#08
    CMP D,#00
    JZ .light_s
    MOV A,#0C
.light_s:
    MOV Y-HI,#E7
    MOV Y-LO,#E7
    MOV B,#06
    MOV C,#04
    CALL fill_rect
    ; Controller handle moves up for traction, down while braking.
    MOV Y-HI,#D5
    MOV Y-LO,#E0
    MOV A,#0F
    MOV B,#57
    MOV C,#11
    CALL fill_rect
    MOV Y-HI,#D5
    MOV Y-LO,#F5
    MOV A,#08
    MOV B,#17
    MOV C,#11
    CALL fill_rect
    MOV Y-HI,#D3
    MOV Y-LO,#7E
    MOV A,#00
    MOV B,#09
    MOV C,#14
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD D,$0002
    CMP D,#00
    JNZ .brake_handle
    LD D,$0001
    CMP D,#00
    JNZ .traction_handle
    MOV Y-HI,#DF
    MOV Y-LO,#ED
    JMP .handle
.traction_handle:
    MOV Y-HI,#D7
    MOV Y-LO,#2D
    JMP .handle
.brake_handle:
    MOV Y-HI,#E4
    MOV Y-LO,#ED
.handle:
    MOV A,#06
    MOV B,#46
    MOV C,#05
    CALL fill_rect
    ; Brown grip, dark Bakelite tip.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD D,$0002
    CMP D,#00
    JNZ .cap_brake
    LD D,$0001
    CMP D,#00
    JNZ .cap_power
    MOV Y-HI,#DE
    MOV Y-LO,#EB
    JMP .cap_draw
.cap_power:
    MOV Y-HI,#D6
    MOV Y-LO,#2B
    JMP .cap_draw
.cap_brake:
    MOV Y-HI,#E3
    MOV Y-LO,#EB
.cap_draw:
    MOV A,#00
    MOV B,#0A
    MOV C,#07
    CALL fill_rect
    ; Identify the actual W/S inputs next to the controller lights.
    MOV X-HI,#50
    MOV X-LO,#TXT_W-5000
    MOV Y-HI,#ED
    MOV Y-LO,#3E
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_S-5000
    MOV Y-HI,#ED
    MOV Y-LO,#48
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_TAH-5000
    MOV Y-HI,#E8
    MOV Y-LO,#64
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_BRAKE-5000
    MOV Y-HI,#E8
    MOV Y-LO,#8C
    MOV A,#00
    CALL draw_text
    RET
draw_view_common:
    MOV A,#08
    CALL fill_view
    ; Tunnel geometry, lamps, distant vanishing point and rails.
    MOV Y-HI,#0C
    MOV Y-LO,#8A
    MOV A,#00
    MOV B,#9B
    MOV C,#69
    CALL fill_rect
    MOV Y-HI,#0D
    MOV Y-LO,#25
    MOV A,#00
    MOV B,#91
    MOV C,#69
    CALL fill_rect
    MOV Y-HI,#0F
    MOV Y-LO,#12
    MOV A,#08
    MOV B,#58
    MOV C,#66
    CALL fill_rect
    MOV Y-HI,#0F
    MOV Y-LO,#D4
    MOV A,#08
    MOV B,#5E
    MOV C,#66
    CALL fill_rect
    MOV Y-HI,#0F
    MOV Y-LO,#74
    MOV A,#00
    MOV B,#51
    MOV C,#68
    CALL fill_rect
    MOV Y-HI,#11
    MOV Y-LO,#8E
    MOV A,#07
    MOV B,#56
    MOV C,#03
    CALL fill_rect
    MOV Y-HI,#12
    MOV Y-LO,#55
    MOV A,#07
    MOV B,#5C
    MOV C,#03
    CALL fill_rect
    MOV Y-HI,#16
    MOV Y-LO,#8E
    MOV A,#08
    MOV B,#06
    MOV C,#56
    CALL fill_rect
    MOV Y-HI,#17
    MOV Y-LO,#AA
    MOV A,#08
    MOV B,#07
    MOV C,#56
    CALL fill_rect
    MOV Y-HI,#15
    MOV Y-LO,#B8
    MOV A,#08
    MOV B,#49
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#33
    MOV Y-LO,#CC
    MOV A,#08
    MOV B,#28
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#46
    MOV Y-LO,#91
    MOV A,#08
    MOV B,#1E
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#4C
    MOV Y-LO,#DB
    MOV A,#07
    MOV B,#0C
    MOV C,#03
    CALL fill_rect
    MOV Y-HI,#21
    MOV Y-LO,#1B
    MOV A,#0E
    MOV B,#0B
    MOV C,#03
    CALL fill_rect
    MOV Y-HI,#2D
    MOV Y-LO,#9C
    MOV A,#0E
    MOV B,#09
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#3C
    MOV Y-LO,#9E
    MOV A,#0E
    MOV B,#05
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#55
    MOV Y-LO,#87
    MOV A,#08
    MOV B,#31
    MOV C,#2C
    CALL fill_rect
    MOV Y-HI,#55
    MOV Y-LO,#90
    MOV A,#00
    MOV B,#1F
    MOV C,#2E
    CALL fill_rect
    MOV Y-HI,#6A
    MOV Y-LO,#A2
    MOV A,#06
    MOV B,#0F
    MOV C,#1D
    CALL fill_rect
    MOV Y-HI,#6B
    MOV Y-LO,#0F
    MOV A,#06
    MOV B,#0C
    MOV C,#1D
    CALL fill_rect
    MOV Y-HI,#72
    MOV Y-LO,#94
    MOV A,#06
    MOV B,#58
    MOV C,#18
    CALL fill_rect
    MOV Y-HI,#71
    MOV Y-LO,#D1
    MOV A,#06
    MOV B,#55
    MOV C,#18
    CALL fill_rect
    MOV Y-HI,#73
    MOV Y-LO,#D4
    MOV A,#0E
    MOV B,#58
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#73
    MOV Y-LO,#12
    MOV A,#0E
    MOV B,#54
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#40
    MOV Y-LO,#DB
    MOV A,#00
    MOV B,#0D
    MOV C,#23
    CALL fill_rect
    MOV Y-HI,#48
    MOV Y-LO,#5E
    MOV A,#04
    MOV B,#07
    MOV C,#06
    CALL fill_rect
    MOV Y-HI,#59
    MOV Y-LO,#DE
    MOV A,#0A
    MOV B,#07
    MOV C,#06
    CALL fill_rect
    MOV Y-HI,#50
    MOV Y-LO,#9C
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#50
    MOV Y-LO,#A4
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#55
    MOV Y-LO,#99
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#55
    MOV Y-LO,#A7
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#5A
    MOV Y-LO,#97
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#5A
    MOV Y-LO,#A9
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#5F
    MOV Y-LO,#94
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#5F
    MOV Y-LO,#AC
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#64
    MOV Y-LO,#91
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#64
    MOV Y-LO,#AF
    MOV A,#07
    MOV B,#03
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#69
    MOV Y-LO,#8F
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#69
    MOV Y-LO,#B2
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#6E
    MOV Y-LO,#8C
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#6E
    MOV Y-LO,#B4
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#73
    MOV Y-LO,#89
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#73
    MOV Y-LO,#B7
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#78
    MOV Y-LO,#86
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#78
    MOV Y-LO,#BA
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#7D
    MOV Y-LO,#84
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#7D
    MOV Y-LO,#BC
    MOV A,#07
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#82
    MOV Y-LO,#81
    MOV A,#07
    MOV B,#05
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#82
    MOV Y-LO,#BF
    MOV A,#07
    MOV B,#05
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#87
    MOV Y-LO,#7E
    MOV A,#07
    MOV B,#05
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#87
    MOV Y-LO,#C2
    MOV A,#07
    MOV B,#05
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#8C
    MOV Y-LO,#7C
    MOV A,#07
    MOV B,#05
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#8C
    MOV Y-LO,#C4
    MOV A,#07
    MOV B,#05
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#5F
    MOV Y-LO,#96
    MOV A,#06
    MOV B,#15
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#6D
    MOV Y-LO,#51
    MOV A,#06
    MOV B,#1F
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#7C
    MOV Y-LO,#49
    MOV A,#06
    MOV B,#31
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#8C
    MOV Y-LO,#7B
    MOV A,#06
    MOV B,#4C
    MOV C,#02
    CALL fill_rect
    ; Cab window frames and front door (real Ecs layout).
    MOV Y-HI,#00
    MOV Y-LO,#00
    MOV A,#07
    MOV B,#A0
    MOV C,#08
    CALL fill_rect
    MOV Y-HI,#00
    MOV Y-LO,#A0
    MOV A,#07
    MOV B,#A0
    MOV C,#08
    CALL fill_rect
    MOV Y-HI,#0A
    MOV Y-LO,#00
    MOV A,#07
    MOV B,#09
    MOV C,#6E
    CALL fill_rect
    MOV Y-HI,#0B
    MOV Y-LO,#37
    MOV A,#07
    MOV B,#09
    MOV C,#6E
    CALL fill_rect
    MOV Y-HI,#0A
    MOV Y-LO,#66
    MOV A,#07
    MOV B,#0C
    MOV C,#6E
    CALL fill_rect
    MOV Y-HI,#0A
    MOV Y-LO,#CA
    MOV A,#07
    MOV B,#0B
    MOV C,#6E
    CALL fill_rect
    MOV Y-HI,#0A
    MOV Y-LO,#07
    MOV A,#0F
    MOV B,#04
    MOV C,#6E
    CALL fill_rect
    MOV Y-HI,#0A
    MOV Y-LO,#6E
    MOV A,#0F
    MOV B,#04
    MOV C,#6D
    CALL fill_rect
    MOV Y-HI,#0A
    MOV Y-LO,#CA
    MOV A,#0F
    MOV B,#03
    MOV C,#6D
    CALL fill_rect
    MOV Y-HI,#0B
    MOV Y-LO,#33
    MOV A,#0F
    MOV B,#04
    MOV C,#6D
    CALL fill_rect
    MOV Y-HI,#91
    MOV Y-LO,#00
    MOV A,#07
    MOV B,#A0
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#91
    MOV Y-LO,#A0
    MOV A,#07
    MOV B,#A0
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#0B
    MOV Y-LO,#A6
    MOV A,#08
    MOV B,#06
    MOV C,#6C
    CALL fill_rect
    MOV Y-HI,#0C
    MOV Y-LO,#0E
    MOV A,#08
    MOV B,#05
    MOV C,#6C
    CALL fill_rect
    MOV Y-HI,#60
    MOV Y-LO,#FE
    MOV A,#08
    MOV B,#08
    MOV C,#0E
    CALL fill_rect
    MOV Y-HI,#66
    MOV Y-LO,#00
    MOV A,#0F
    MOV B,#03
    MOV C,#07
    CALL fill_rect
    MOV Y-HI,#08
    MOV Y-LO,#11
    MOV A,#08
    MOV B,#16
    MOV C,#03
    CALL fill_rect
    MOV Y-HI,#0B
    MOV Y-LO,#4A
    MOV A,#08
    MOV B,#09
    MOV C,#03
    CALL fill_rect
    MOV Y-HI,#0C
    MOV Y-LO,#6C
    MOV A,#08
    MOV B,#09
    MOV C,#03
    CALL fill_rect
    ; Car identification on the pale cab header.
    MOV X-HI,#50
    MOV X-LO,#TXT_ECS-5000
    MOV Y-HI,#01
    MOV Y-LO,#10
    MOV A,#00
    CALL draw_text
    RET

draw_tunnel:
    CALL draw_view_common
    RET

draw_station_base:
    CALL draw_view_common
    ; Illuminated side platform and Ecs-view station sign.
    MOV Y-HI,#68
    MOV Y-LO,#95
    MOV A,#07
    MOV B,#5E
    MOV C,#1F
    CALL fill_rect
    MOV Y-HI,#68
    MOV Y-LO,#95
    MOV A,#0E
    MOV B,#5E
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#2F
    MOV Y-LO,#17
    MOV A,#07
    MOV B,#5B
    MOV C,#27
    CALL fill_rect
    MOV Y-HI,#2D
    MOV Y-LO,#D7
    MOV A,#08
    MOV B,#5B
    MOV C,#04
    CALL fill_rect
    MOV Y-HI,#31
    MOV Y-LO,#9E
    MOV A,#0F
    MOV B,#45
    MOV C,#15
    CALL fill_rect
    MOV Y-HI,#35
    MOV Y-LO,#62
    MOV A,#00
    MOV B,#3E
    MOV C,#0F
    CALL fill_rect
    MOV Y-HI,#50
    MOV Y-LO,#DE
    MOV A,#08
    MOV B,#4F
    MOV C,#03
    CALL fill_rect
    MOV Y-HI,#54
    MOV Y-LO,#A6
    MOV A,#08
    MOV B,#05
    MOV C,#11
    CALL fill_rect
    MOV Y-HI,#54
    MOV Y-LO,#DA
    MOV A,#08
    MOV B,#05
    MOV C,#11
    CALL fill_rect
    MOV Y-HI,#5F
    MOV Y-LO,#ED
    MOV A,#08
    MOV B,#02
    MOV C,#20
    CALL fill_rect
    MOV Y-HI,#60
    MOV Y-LO,#23
    MOV A,#08
    MOV B,#02
    MOV C,#20
    CALL fill_rect
    MOV Y-HI,#55
    MOV Y-LO,#0D
    MOV A,#07
    MOV B,#54
    MOV C,#03
    CALL fill_rect
    MOV Y-HI,#5A
    MOV Y-LO,#12
    MOV A,#07
    MOV B,#06
    MOV C,#27
    CALL fill_rect
    MOV Y-HI,#5A
    MOV Y-LO,#55
    MOV A,#07
    MOV B,#06
    MOV C,#27
    CALL fill_rect
    RET

draw_station_depo:
    CALL draw_station_base
    MOV Y-HI,#4F
    MOV Y-LO,#97
    MOV A,#09
    MOV B,#59
    MOV C,#04
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_DEPO-5000
    MOV Y-HI,#3C
    MOV Y-LO,#F4
    MOV A,#0F
    CALL draw_text
    RET

draw_station_centrum:
    CALL draw_station_base
    MOV Y-HI,#4F
    MOV Y-LO,#97
    MOV A,#0A
    MOV B,#59
    MOV C,#04
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_CENTRUM-5000
    MOV Y-HI,#3C
    MOV Y-LO,#E8
    MOV A,#0F
    CALL draw_text
    RET

draw_station_luka:
    CALL draw_station_base
    MOV Y-HI,#4F
    MOV Y-LO,#97
    MOV A,#0C
    MOV B,#59
    MOV C,#04
    CALL fill_rect
    MOV X-HI,#50
    MOV X-LO,#TXT_LUKA-5000
    MOV Y-HI,#3C
    MOV Y-LO,#F4
    MOV A,#0F
    CALL draw_text
    RET

draw_motion:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0006
    CMP A,#01
    JNZ .done
    LD A,$0000
    CMP A,#00
    JZ .done
    MOV Y-HI,#83
    MOV Y-LO,#C2
    MOV A,#00
    MOV B,#3F
    MOV C,#03
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0004
    AND A,#80
    JZ .line_a
    MOV Y-HI,#83
    MOV Y-LO,#C6
    MOV A,#06
    MOV B,#34
    MOV C,#02
    CALL fill_rect
    JMP .done
.line_a:
    MOV Y-HI,#87
    MOV Y-LO,#8E
    MOV A,#06
    MOV B,#2D
    MOV C,#02
    CALL fill_rect
.done:
    RET

; Basic frame pacing. Rendering itself also consumes emulated cycles.
frame_delay:
    MOV D,#02
.outer:
    MOV C,#C0
.inner:
    DEC C
    JNZ .inner
    DEC D
    JNZ .outer
    RET


; All texts are real VGA pixels rendered by draw_text.
.org $5000
TXT_ECS:
    .asciz "ECS 1083"
TXT_BAT:
    .asciz "BAT"
TXT_PROUD:
    .asciz "PROUD"
TXT_TRAK:
    .asciz "TRAK"
TXT_ARS:
    .asciz "ARS"
TXT_RYCH:
    .asciz "RYCHL."
TXT_TM:
    .asciz "TM"
TXT_TC:
    .asciz "TC"
TXT_V:
    .asciz "V"
TXT_A:
    .asciz "A"
TXT_KMH:
    .asciz "KM/H"
TXT_BAR:
    .asciz "BAR"
TXT_CTL:
    .asciz "KONTROLER"
TXT_BRZDA:
    .asciz "BRZDA"
TXT_OSV:
    .asciz "OSV"
TXT_DVERE:
    .asciz "DVERE"
TXT_ZVON:
    .asciz "ZVON"
TXT_RADIO:
    .asciz "RADIO"
TXT_TAH:
    .asciz "TAH"
TXT_BRAKE:
    .asciz "BRZD"
TXT_DEPO:
    .asciz "DEPO"
TXT_CENTRUM:
    .asciz "CENTRUM"
TXT_LUKA:
    .asciz "LUKA"
TXT_STOP:
    .asciz "STOP"
TXT_40:
    .asciz "40"
TXT_80:
    .asciz "80"
TXT_SIGN:
    .asciz "SIGNAL"
TXT_START:
    .asciz "ROUTE 1"
TXT_W:
    .asciz "W"
TXT_S:
    .asciz "S"

; 3x5 font: five bit masks per printable character, 20h..5Ah.
.org $6000
font_3x5:
    .byte 00,00,00,00,00 ;  
    .byte 02,02,02,00,02 ; !
    .byte 00,00,00,00,00 ; "
    .byte 00,00,00,00,00 ; #
    .byte 00,00,00,00,00 ; $
    .byte 00,00,00,00,00 ; %
    .byte 00,00,00,00,00 ; &
    .byte 00,00,00,00,00 ; '
    .byte 00,00,00,00,00 ; (
    .byte 00,00,00,00,00 ; )
    .byte 00,00,00,00,00 ; *
    .byte 00,00,00,00,00 ; +
    .byte 00,00,00,00,00 ; ,
    .byte 00,00,07,00,00 ; -
    .byte 00,00,00,00,02 ; .
    .byte 01,01,02,04,04 ; /
    .byte 07,05,05,05,07 ; 0
    .byte 02,06,02,02,07 ; 1
    .byte 07,01,07,04,07 ; 2
    .byte 07,01,07,01,07 ; 3
    .byte 05,05,07,01,01 ; 4
    .byte 07,04,07,01,07 ; 5
    .byte 07,04,07,05,07 ; 6
    .byte 07,01,01,01,01 ; 7
    .byte 07,05,07,05,07 ; 8
    .byte 07,05,07,01,07 ; 9
    .byte 00,02,00,02,00 ; :
    .byte 00,00,00,00,00 ; ;
    .byte 00,00,00,00,00 ; <
    .byte 00,00,00,00,00 ; =
    .byte 00,00,00,00,00 ; >
    .byte 00,00,00,00,00 ; ?
    .byte 00,00,00,00,00 ; @
    .byte 02,05,07,05,05 ; A
    .byte 06,05,06,05,06 ; B
    .byte 03,04,04,04,03 ; C
    .byte 06,05,05,05,06 ; D
    .byte 07,04,06,04,07 ; E
    .byte 07,04,06,04,04 ; F
    .byte 03,04,05,05,03 ; G
    .byte 05,05,07,05,05 ; H
    .byte 07,02,02,02,07 ; I
    .byte 01,01,01,05,02 ; J
    .byte 05,05,06,05,05 ; K
    .byte 04,04,04,04,07 ; L
    .byte 05,07,07,05,05 ; M
    .byte 05,07,07,07,05 ; N
    .byte 02,05,05,05,02 ; O
    .byte 06,05,06,04,04 ; P
    .byte 07,05,05,07,01 ; Q
    .byte 06,05,06,05,05 ; R
    .byte 03,04,02,01,06 ; S
    .byte 07,02,02,02,02 ; T
    .byte 05,05,05,05,07 ; U
    .byte 05,05,05,05,02 ; V
    .byte 05,05,07,07,05 ; W
    .byte 05,05,02,05,05 ; X
    .byte 05,05,02,02,02 ; Y
    .byte 07,01,02,04,07 ; Z
