; PPA-1 Metro Driver - Ezh3-style cab demo
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

    ; Draw the old Ezh3-like brown/black control desk once.
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
    CMP X-HI,#A2
    JNZ .loop
    CMP X-LO,#80
    JNZ .loop
    RET

; Fill control desk y=130..199 (A280..F9FF) with A.
fill_panel:
    MOV BR-HI,#20
    MOV BR-LO,#01
    MOV X-HI,#A2
    MOV X-LO,#80
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

; ---------------------------------------------------------------------------
; Static Ezh3-like cab desk
; ---------------------------------------------------------------------------

draw_panel_static:
    ; Brown metal desk.
    MOV A,#06
    CALL fill_panel

    ; Black top lip, two 160-pixel halves.
    MOV Y-HI,#A2
    MOV Y-LO,#80
    MOV A,#00
    MOV B,#A0
    MOV C,#05
    CALL fill_rect

    MOV Y-HI,#A3
    MOV Y-LO,#20
    MOV A,#00
    MOV B,#A0
    MOV C,#05
    CALL fill_rect

    ; Three black instrument housings.
    MOV Y-HI,#B5
    MOV Y-LO,#54
    MOV A,#00
    MOV B,#32
    MOV C,#18
    CALL fill_rect

    MOV Y-HI,#B5
    MOV Y-LO,#9A
    MOV A,#00
    MOV B,#32
    MOV C,#18
    CALL fill_rect

    MOV Y-HI,#B5
    MOV Y-LO,#E0
    MOV A,#00
    MOV B,#32
    MOV C,#18
    CALL fill_rect

    ; Gauge needles / marks.
    MOV Y-HI,#BA
    MOV Y-LO,#58
    MOV A,#0F
    MOV B,#22
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#BA
    MOV Y-LO,#9E
    MOV A,#0A
    MOV B,#22
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#BA
    MOV Y-LO,#E4
    MOV A,#0C
    MOV B,#22
    MOV C,#02
    CALL fill_rect

    ; Speed bar bezel.
    MOV Y-HI,#DE
    MOV Y-LO,#99
    MOV A,#00
    MOV B,#50
    MOV C,#0A
    CALL fill_rect

    ; W / S indicator surrounds.
    MOV Y-HI,#C0
    MOV Y-LO,#9C
    MOV A,#00
    MOV B,#0C
    MOV C,#09
    CALL fill_rect

    MOV Y-HI,#C0
    MOV Y-LO,#B0
    MOV A,#00
    MOV B,#0C
    MOV C,#09
    CALL fill_rect

    ; Route lamps: D / C / L colors.
    MOV Y-HI,#C0
    MOV Y-LO,#E4
    MOV A,#09
    MOV B,#08
    MOV C,#06
    CALL fill_rect

    MOV Y-HI,#C0
    MOV Y-LO,#F0
    MOV A,#0A
    MOV B,#08
    MOV C,#06
    CALL fill_rect

    MOV Y-HI,#C0
    MOV Y-LO,#FC
    MOV A,#0C
    MOV B,#08
    MOV C,#06
    CALL fill_rect

    ; Controller slot.
    MOV Y-HI,#BA
    MOV Y-LO,#10
    MOV A,#00
    MOV B,#08
    MOV C,#26
    CALL fill_rect
    RET

draw_panel_dynamic:
    ; Speed bar: erase inner area first.
    MOV Y-HI,#E1
    MOV Y-LO,#1C
    MOV A,#08
    MOV B,#28
    MOV C,#05
    CALL fill_rect

    MOV BR-HI,#10
    MOV BR-LO,#00
    LD B,$0000
    CMP B,#00
    JZ .speed_done
    MOV Y-HI,#E1
    MOV Y-LO,#1C
    MOV A,#0A
    MOV C,#05
    CALL fill_rect

.speed_done:
    ; W lamp.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD D,$0001
    CMP D,#00
    JZ .w_off
    MOV A,#0A
    JMP .w_draw
.w_off:
    MOV A,#08
.w_draw:
    MOV Y-HI,#C0
    MOV Y-LO,#9E
    MOV B,#08
    MOV C,#05
    CALL fill_rect

    ; S lamp.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD D,$0002
    CMP D,#00
    JZ .s_off
    MOV A,#0C
    JMP .s_draw
.s_off:
    MOV A,#08
.s_draw:
    MOV Y-HI,#C0
    MOV Y-LO,#B2
    MOV B,#08
    MOV C,#05
    CALL fill_rect

    ; Redraw controller area to remove old knob.
    MOV Y-HI,#BA
    MOV Y-LO,#07
    MOV A,#06
    MOV B,#18
    MOV C,#2A
    CALL fill_rect

    MOV Y-HI,#BA
    MOV Y-LO,#10
    MOV A,#00
    MOV B,#08
    MOV C,#26
    CALL fill_rect

    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0002
    CMP A,#00
    JNZ .knob_brake
    LD A,$0001
    CMP A,#00
    JNZ .knob_power

    ; Neutral.
    MOV Y-HI,#D0
    MOV Y-LO,#8B
    JMP .knob_draw

.knob_power:
    MOV Y-HI,#C1
    MOV Y-LO,#8B
    JMP .knob_draw

.knob_brake:
    MOV Y-HI,#E2
    MOV Y-LO,#0B

.knob_draw:
    MOV A,#0F
    MOV B,#0E
    MOV C,#06
    CALL fill_rect
    RET

; ---------------------------------------------------------------------------
; Tunnel and stations
; ---------------------------------------------------------------------------

draw_tunnel:
    MOV A,#08
    CALL fill_view

    ; Dark tunnel mouth.
    MOV Y-HI,#1F
    MOV Y-LO,#72
    MOV A,#00
    MOV B,#DC
    MOV C,#69
    CALL fill_rect

    ; Track bed.
    MOV Y-HI,#64
    MOV Y-LO,#3C
    MOV A,#00
    MOV B,#C8
    MOV C,#32
    CALL fill_rect

    ; Ceiling lamp.
    MOV Y-HI,#12
    MOV Y-LO,#1A
    MOV A,#0E
    MOV B,#0C
    MOV C,#05
    CALL fill_rect

    ; Perspective rails.
    MOV Y-HI,#55
    MOV Y-LO,#9A
    MOV A,#07
    MOV B,#03
    MOV C,#14
    CALL fill_rect

    MOV Y-HI,#55
    MOV Y-LO,#A4
    MOV A,#07
    MOV B,#03
    MOV C,#14
    CALL fill_rect

    MOV Y-HI,#6E
    MOV Y-LO,#8A
    MOV A,#07
    MOV B,#04
    MOV C,#14
    CALL fill_rect

    MOV Y-HI,#6E
    MOV Y-LO,#B4
    MOV A,#07
    MOV B,#04
    MOV C,#14
    CALL fill_rect

    MOV Y-HI,#87
    MOV Y-LO,#70
    MOV A,#07
    MOV B,#06
    MOV C,#16
    CALL fill_rect

    MOV Y-HI,#87
    MOV Y-LO,#CC
    MOV A,#07
    MOV B,#06
    MOV C,#16
    CALL fill_rect

    ; Small right-side signal, green.
    MOV Y-HI,#4C
    MOV Y-LO,#18
    MOV A,#00
    MOV B,#14
    MOV C,#20
    CALL fill_rect

    MOV Y-HI,#53
    MOV Y-LO,#9E
    MOV A,#04
    MOV B,#08
    MOV C,#08
    CALL fill_rect

    MOV Y-HI,#62
    MOV Y-LO,#9E
    MOV A,#0A
    MOV B,#08
    MOV C,#08
    CALL fill_rect
    RET

draw_station_base:
    MOV A,#07
    CALL fill_view

    ; Track on the left.
    MOV Y-HI,#66
    MOV Y-LO,#80
    MOV A,#00
    MOV B,#CD
    MOV C,#30
    CALL fill_rect

    ; Platform on the right.
    MOV Y-HI,#78
    MOV Y-LO,#CD
    MOV A,#06
    MOV B,#73
    MOV C,#22
    CALL fill_rect

    ; Ceiling beam.
    MOV Y-HI,#19
    MOV Y-LO,#CD
    MOV A,#08
    MOV B,#73
    MOV C,#08
    CALL fill_rect

    ; Two columns.
    MOV Y-HI,#28
    MOV Y-LO,#E4
    MOV A,#08
    MOV B,#06
    MOV C,#40
    CALL fill_rect

    MOV Y-HI,#29
    MOV Y-LO,#16
    MOV A,#08
    MOV B,#06
    MOV C,#40
    CALL fill_rect

    ; Black station sign.
    MOV Y-HI,#49
    MOV Y-LO,#6E
    MOV A,#00
    MOV B,#2A
    MOV C,#12
    CALL fill_rect

    ; Track rails.
    MOV Y-HI,#83
    MOV Y-LO,#72
    MOV A,#07
    MOV B,#06
    MOV C,#18
    CALL fill_rect

    MOV Y-HI,#83
    MOV Y-LO,#B8
    MOV A,#07
    MOV B,#06
    MOV C,#18
    CALL fill_rect
    RET

draw_station_depo:
    CALL draw_station_base

    ; Blue station stripe.
    MOV Y-HI,#3C
    MOV Y-LO,#CD
    MOV A,#09
    MOV B,#73
    MOV C,#06
    CALL fill_rect

    ; White block letter D.
    MOV Y-HI,#4F
    MOV Y-LO,#B8
    MOV A,#0F
    MOV B,#03
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#4F
    MOV Y-LO,#B8
    MOV B,#0B
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#52
    MOV Y-LO,#40
    MOV B,#03
    MOV C,#06
    CALL fill_rect
    MOV Y-HI,#59
    MOV Y-LO,#B8
    MOV B,#0B
    MOV C,#02
    CALL fill_rect
    RET

draw_station_centrum:
    CALL draw_station_base

    ; Green station stripe.
    MOV Y-HI,#3C
    MOV Y-LO,#CD
    MOV A,#0A
    MOV B,#73
    MOV C,#06
    CALL fill_rect

    ; White block letter C.
    MOV Y-HI,#4F
    MOV Y-LO,#B8
    MOV A,#0F
    MOV B,#0B
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#4F
    MOV Y-LO,#B8
    MOV B,#03
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#59
    MOV Y-LO,#B8
    MOV B,#0B
    MOV C,#02
    CALL fill_rect
    RET

draw_station_luka:
    CALL draw_station_base

    ; Red station stripe.
    MOV Y-HI,#3C
    MOV Y-LO,#CD
    MOV A,#0C
    MOV B,#73
    MOV C,#06
    CALL fill_rect

    ; White block letter L.
    MOV Y-HI,#4F
    MOV Y-LO,#B8
    MOV A,#0F
    MOV B,#03
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#59
    MOV Y-LO,#B8
    MOV B,#0B
    MOV C,#02
    CALL fill_rect
    RET

; Draw alternating sleepers only while in tunnel.
draw_motion:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0006
    CMP A,#01
    JNZ .done

    ; Erase both sleeper positions.
    MOV Y-HI,#73
    MOV Y-LO,#82
    MOV A,#00
    MOV B,#78
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#78
    MOV Y-LO,#7E
    MOV B,#78
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#84
    MOV Y-LO,#EE
    MOV B,#96
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#8C
    MOV Y-LO,#68
    MOV B,#96
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#98
    MOV Y-LO,#D0
    MOV B,#BE
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#9D
    MOV Y-LO,#C8
    MOV B,#BE
    MOV C,#02
    CALL fill_rect

    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0004
    AND A,#80
    JZ .pattern_a

.pattern_b:
    MOV Y-HI,#78
    MOV Y-LO,#7E
    MOV A,#06
    MOV B,#78
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#8C
    MOV Y-LO,#68
    MOV B,#96
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#9D
    MOV Y-LO,#C8
    MOV B,#BE
    MOV C,#02
    CALL fill_rect
    JMP .done

.pattern_a:
    MOV Y-HI,#73
    MOV Y-LO,#82
    MOV A,#06
    MOV B,#78
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#84
    MOV Y-LO,#EE
    MOV B,#96
    MOV C,#02
    CALL fill_rect

    MOV Y-HI,#98
    MOV Y-LO,#D0
    MOV B,#BE
    MOV C,#02
    CALL fill_rect

.done:
    RET

; Basic frame pacing. Rendering itself also consumes emulated cycles.
frame_delay:
    MOV D,#04
.outer:
    MOV C,#C0
.inner:
    DEC C
    JNZ .inner
    DEC D
    JNZ .outer
    RET
