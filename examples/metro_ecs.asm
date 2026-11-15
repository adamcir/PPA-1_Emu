; PPA-1 Metro Driver - Ecs cab driving game
; (c) 2026 Adam Cir (Adava), Adava Software, Adava Development
;
; Required modules:
;   SLOT 1 = PPA-VGA/1   (register bank 2000, VRAM bank 2001)
;   SLOT 2 = PPA-PS/2/1  (register bank 3000)
;   SLOT 3 = PPA-SOUND/1 (register bank 4000)
;
; Controls:
;   W = increase controller notch (one step per key press)
;   S = decrease controller notch (one step per key press)
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

MAX_SPEED       EQU $50
; Extended state in RAM bank 1000 (notch indices 0..6):
; 0=T2, 1=T1A, 2=T1, 3=0, 4=X1, 5=X2, 6=X3
VAR_TARGET       EQU $0008
VAR_ACTUAL       EQU $0009
VAR_SHIFT_TICKS  EQU $000A
VAR_ACCEL_TICKS  EQU $000B
VAR_LIMIT        EQU $000C
VAR_WARNING      EQU $000D
VAR_LAST_PHASE   EQU $000E
VAR_SHIFT_PHASE  EQU $000F

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
    ST $000A,A
    ST $000B,A
    ST $000D,A
    ST $000F,A
    MOV A,#03
    ST $0008,A
    ST $0009,A
    MOV A,#50
    ST $000C,A
    MOV A,#FF
    ST $0006,A
    ST $000E,A

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
    CALL update_controller
    CALL update_physics
    CALL update_scene
    CALL update_ars
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

    ; PS/2 set-2 F0 means key released, not another notch.
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
    ; Each make code increments one detent, even if held.
    LD A,$0001
    CMP A,#00
    JNZ .poll
    MOV A,#01
    ST $0001,A
    LD A,$0008
    CMP A,#06
    JNC .poll
    INC A
    ST $0008,A
    JMP .poll

.s_down:
    LD A,$0002
    CMP A,#00
    JNZ .poll
    MOV A,#01
    ST $0002,A
    LD A,$0008
    CMP A,#00
    JZ .poll
    DEC A
    ST $0008,A
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
; Metrostroi-inspired KV-35 notches.
; ControllerSet (target) and RealControllerPosition (physical) are distinct.
; Target changes per keypress; actual lever follows over several frames.
; ---------------------------------------------------------------------------

update_controller:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0008
    LD B,$0009
    CMP A,B
    JZ .stable

    LD A,$000A
    INC A
    CMP A,#03
    JC .wait
    MOV A,#00
    ST $000A,A
    ST $000B,A

    LD A,$0008
    LD B,$0009
    CMP B,A
    JC .up
    DEC B
    ST $0009,B
    RET
.up:
    INC B
    ST $0009,B
    RET

.wait:
    ST $000A,A
    RET

.stable:
    MOV A,#00
    ST $000A,A
    RET

; ---------------------------------------------------------------------------
; Simplified frame-based acceleration, friction and electric braking.
; Distinct notch thresholds; no instantaneous speed changes.
; Speed is an integer in km/h: 0 .. 80 (50h).
; ---------------------------------------------------------------------------


update_physics:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0009
    MOV D,#40         ; coast: lose 1 km/h after 64 frames
    CMP A,#03
    JZ .timed
    CMP A,#00
    JZ .t2
    CMP A,#01
    JZ .t1a
    CMP A,#02
    JZ .t1
    CMP A,#04
    JZ .x1
    CMP A,#05
    JZ .x2
    MOV D,#04         ; X3: fastest traction
    JMP .timed
.x2:
    MOV D,#07
    JMP .timed
.x1:
    MOV D,#0E
    JMP .timed
.t1:
    MOV D,#08
    JMP .timed
.t1a:
    MOV D,#04
    JMP .timed
.t2:
    MOV D,#02

.timed:
    LD A,$000B
    INC A
    CMP A,D
    JC .wait_acc
    MOV A,#00
    ST $000B,A
    LD A,$0009
    CMP A,#03
    JZ .reduce
    JC .reduce

    ; Electric traction in X1/X2/X3.
    LD A,$0000
    CMP A,#50
    JNC .advance
    INC A
    ST $0000,A
    JMP .advance

.reduce:
    LD A,$0000
    CMP A,#00
    JZ .advance
    DEC A
    ST $0000,A
    JMP .advance

.wait_acc:
    ST $000B,A

.advance:
    ; Fixed route coordinate for VGA parallax / moving signals.
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
; Metrostroi-inspired ALS/ARS demo sections.
; Signal digits are km/h (0,40,60,80); 70 is supported by the model
; but unused on this very short example line.
; Not a full implementation of ARS, EPK or interlocking.
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
    MOV A,#FF
    ST $000E,A

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

update_ars:
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0005
    CMP A,#20
    JC .limit80
    CMP A,#30
    JC .limit70
    CMP A,#40
    JC .limit80
    CMP A,#60
    JC .limit60
    CMP A,#70
    JC .limit40
    CMP A,#B0
    JC .limit80
    CMP A,#C0
    JC .limit60
    CMP A,#D0
    JC .limit40
    CMP A,#F0
    JC .limit80
    MOV B,#00
    JMP .commit
.limit80:
    MOV B,#50
    JMP .commit
.limit70:
    MOV B,#46
    JMP .commit
.limit60:
    MOV B,#3C
    JMP .commit
.limit40:
    MOV B,#28
.commit:
    ST $000C,B
    LD A,$0000
    CMP A,B
    JZ .clear_warning
    JC .clear_warning
    MOV A,#01
    ST $000D,A
    RET
.clear_warning:
    MOV A,#00
    ST $000D,A
    RET

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

    ; 80 + speed * 2 Hz, max 240 Hz. Uses SOUND in slot 3.
    SHL B
    ADD B,#50
    MOV BR-HI,#40
    MOV BR-LO,#00
    ST $0020,B
    MOV A,#00
    ST $0021,A
    RET
.silent:
    MOV BR-HI,#40
    MOV BR-LO,#00
    MOV A,#00
    ST $0020,A
    ST $0021,A
    RET

; ---------------------------------------------------------------------------
; VGA drawing helpers (unchanged from first Ecs revision)
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
    MOV D,#00
    SHL B
    JNC .font_mul2
    INC D
.font_mul2:
    SHL B
    JNC .font_mul4
    INC D
.font_mul4:
    ADD B,C
    JNC .font_mul5
    INC D
.font_mul5:
    MOV X-HI,#60
    ADD X-HI,D
    MOV X-LO,B
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
    ; The analog speed bar covers 0..80 km/h.
    MOV Y-HI,#B9
    MOV Y-LO,#AD
    MOV A,#00
    MOV B,#28
    MOV C,#04
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD B,$0000
    SHR B
    CMP B,#00
    JZ .bar_done
    MOV Y-HI,#B9
    MOV Y-LO,#AD
    MOV A,#0A
    MOV C,#04
    CALL fill_rect
.bar_done:
    ; Digital km/h readout using two real VGA font characters.
    MOV Y-HI,#B0
    MOV Y-LO,#EF
    MOV A,#0F
    MOV B,#18
    MOV C,#06
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0000
    MOV C,#00
.tens:
    CMP A,#0A
    JC .digits_ready
    SUB A,#0A
    INC C
    JMP .tens
.digits_ready:
    ST $0015,A
    ST $0016,C
    MOV B,C
    SHL B
    MOV X-HI,#50
    MOV X-LO,#TXT_DIGIT0-5000
    ADD X-LO,B
    MOV Y-HI,#B0
    MOV Y-LO,#F2
    MOV A,#00
    CALL draw_text

    MOV BR-HI,#10
    MOV BR-LO,#00
    LD B,$0015
    SHL B
    MOV X-HI,#50
    MOV X-LO,#TXT_DIGIT0-5000
    ADD X-LO,B
    MOV Y-HI,#B0
    MOV Y-LO,#F7
    MOV A,#00
    CALL draw_text
    ; Lamps indicate W/S key-down, but notch stays latched on release.
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
    MOV X-HI,#50
    MOV X-LO,#TXT_W-5000
    MOV Y-HI,#EC
    MOV Y-LO,#DE
    MOV A,#00
    CALL draw_text
    MOV X-HI,#50
    MOV X-LO,#TXT_S-5000
    MOV Y-HI,#EC
    MOV Y-LO,#E9
    MOV A,#00
    CALL draw_text
    ; ARS lamp panel reflects actual approach codes 0/40/60/70/80.
    MOV Y-HI,#A9
    MOV Y-LO,#56
    MOV A,#00
    MOV B,#08
    MOV C,#12
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$000C
    CMP A,#00
    JZ .ars_red
    CMP A,#50
    JZ .ars_green
    MOV A,#0E
    MOV Y-HI,#B2
    MOV Y-LO,#16
    MOV B,#08
    MOV C,#04
    CALL fill_rect
    JMP .ars_text
.ars_red:
    MOV Y-HI,#A9
    MOV Y-LO,#56
    MOV A,#0C
    MOV B,#08
    MOV C,#04
    CALL fill_rect
    JMP .ars_text
.ars_green:
    MOV Y-HI,#B9
    MOV Y-LO,#96
    MOV A,#0A
    MOV B,#08
    MOV C,#04
    CALL fill_rect
.ars_text:
    MOV Y-HI,#C1
    MOV Y-LO,#11
    MOV A,#08
    MOV B,#12
    MOV C,#06
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$000C
    CMP A,#00
    JZ .ars_0
    CMP A,#28
    JZ .ars_40
    CMP A,#3C
    JZ .ars_60
    CMP A,#46
    JZ .ars_70
    JMP .ars_80
.ars_0:
    MOV X-HI,#50
    MOV X-LO,#TXT_ARS0-5000
    MOV Y-HI,#C1
    MOV Y-LO,#15
    MOV A,#0F
    CALL draw_text
    JMP .warning
.ars_40:
    MOV X-HI,#50
    MOV X-LO,#TXT_ARS40-5000
    MOV Y-HI,#C1
    MOV Y-LO,#14
    MOV A,#0F
    CALL draw_text
    JMP .warning
.ars_60:
    MOV X-HI,#50
    MOV X-LO,#TXT_ARS60-5000
    MOV Y-HI,#C1
    MOV Y-LO,#14
    MOV A,#0F
    CALL draw_text
    JMP .warning
.ars_70:
    MOV X-HI,#50
    MOV X-LO,#TXT_ARS70-5000
    MOV Y-HI,#C1
    MOV Y-LO,#14
    MOV A,#0F
    CALL draw_text
    JMP .warning
.ars_80:
    MOV X-HI,#50
    MOV X-LO,#TXT_ARS80-5000
    MOV Y-HI,#C1
    MOV Y-LO,#14
    MOV A,#0F
    CALL draw_text
.warning:
    ; Distinct overspeed warning next to ARS.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$000D
    CMP A,#00
    JNZ .warn_on
    MOV Y-HI,#B8
    MOV Y-LO,#5F
    MOV A,#08
    MOV B,#04
    MOV C,#04
    CALL fill_rect
    JMP .controller
.warn_on:
    MOV Y-HI,#B8
    MOV Y-LO,#5F
    MOV A,#0C
    MOV B,#04
    MOV C,#04
    CALL fill_rect
.controller:
    ; Analog-style KV-35 controller notch indicator + grip.
    MOV Y-HI,#D3
    MOV Y-LO,#5E
    MOV A,#0F
    MOV B,#58
    MOV C,#15
    CALL fill_rect
    MOV Y-HI,#D3
    MOV Y-LO,#75
    MOV A,#08
    MOV B,#17
    MOV C,#15
    CALL fill_rect
    MOV Y-HI,#D2
    MOV Y-LO,#3E
    MOV A,#00
    MOV B,#09
    MOV C,#17
    CALL fill_rect
    MOV Y-HI,#CE
    MOV Y-LO,#57
    MOV A,#0F
    MOV B,#14
    MOV C,#06
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0009
    CMP A,#00
    JZ .set_pos_0
    CMP A,#01
    JZ .set_pos_1
    CMP A,#02
    JZ .set_pos_2
    CMP A,#03
    JZ .set_pos_3
    CMP A,#04
    JZ .set_pos_4
    CMP A,#05
    JZ .set_pos_5
    CMP A,#06
    JZ .set_pos_6
    JMP .set_pos_3
.set_pos_0:
    MOV Y-HI,#E4
    MOV Y-LO,#ED
    MOV X-HI,#50
    MOV X-LO,#TXT_T2-5000
    JMP .grip
.set_pos_1:
    MOV Y-HI,#E2
    MOV Y-LO,#6D
    MOV X-HI,#50
    MOV X-LO,#TXT_T1A-5000
    JMP .grip
.set_pos_2:
    MOV Y-HI,#DF
    MOV Y-LO,#ED
    MOV X-HI,#50
    MOV X-LO,#TXT_T1-5000
    JMP .grip
.set_pos_3:
    MOV Y-HI,#DD
    MOV Y-LO,#6D
    MOV X-HI,#50
    MOV X-LO,#TXT_ZERO-5000
    JMP .grip
.set_pos_4:
    MOV Y-HI,#DA
    MOV Y-LO,#ED
    MOV X-HI,#50
    MOV X-LO,#TXT_X1-5000
    JMP .grip
.set_pos_5:
    MOV Y-HI,#D8
    MOV Y-LO,#6D
    MOV X-HI,#50
    MOV X-LO,#TXT_X2-5000
    JMP .grip
.set_pos_6:
    MOV Y-HI,#D5
    MOV Y-LO,#ED
    MOV X-HI,#50
    MOV X-LO,#TXT_X3-5000
    JMP .grip
.grip:
    ; Save Y before text call: draw_text changes both pointers.
    MOV BR-HI,#10
    MOV BR-LO,#00
    ST $0017,Y-HI
    ST $0018,Y-LO
    ; Fixed notch text at upper left of controller housing.
    MOV Y-HI,#CE
    MOV Y-LO,#58
    MOV A,#00
    CALL draw_text
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD Y-HI,$0017
    LD Y-LO,$0018
    MOV A,#06
    MOV B,#46
    MOV C,#04
    CALL fill_rect
    ; One dark Bakelite tip at fixed right edge of the handle.
    ; The controller's full handle moves up and down with notch.
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
    MOV Y-HI,#02
    MOV Y-LO,#44
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
    ; Do not animate when the position is unchanged.
    ; Phase is selected from actual track position (RAM 1000:0004).
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0004
    SHR A
    SHR A
    SHR A
    SHR A
    SHR A
    AND A,#03
    ST $000F,A
    LD B,$000E
    CMP A,B
    JZ .done
    ST $000E,A

    ; Erase the old rail-side signal, sleepers and wall ribs.
    MOV Y-HI,#24
    MOV Y-LO,#EE
    MOV A,#00
    MOV B,#1C
    MOV C,#54
    CALL fill_rect
    MOV Y-HI,#63
    MOV Y-LO,#3C
    MOV A,#00
    MOV B,#4C
    MOV C,#25
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0006
    CMP A,#01
    JNZ .station
    CALL moving_walls
    JMP .track
.station:
    CALL moving_platform
.track:
    CALL moving_sleepers
    CALL moving_rails
    CALL moving_signal
.done:
    RET

moving_walls:
    ; Wide/ridged concrete tunnel walls shift in perspective.
    MOV Y-HI,#1D
    MOV Y-LO,#35
    MOV A,#00
    MOV B,#11
    MOV C,#39
    CALL fill_rect
    MOV Y-HI,#1D
    MOV Y-LO,#7C
    MOV A,#00
    MOV B,#0C
    MOV C,#39
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$000F
    CMP A,#00
    JZ .phase0
    CMP A,#01
    JZ .phase1
    CMP A,#02
    JZ .phase2
    JMP .phase3
.phase0:
    MOV Y-HI,#24
    MOV Y-LO,#B5
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#24
    MOV Y-LO,#FC
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#39
    MOV Y-LO,#F5
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#3A
    MOV Y-LO,#3C
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#51
    MOV Y-LO,#B5
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#51
    MOV Y-LO,#FC
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    RET
.phase1:
    MOV Y-HI,#28
    MOV Y-LO,#75
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#28
    MOV Y-LO,#BC
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#3E
    MOV Y-LO,#F5
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#3F
    MOV Y-LO,#3C
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#55
    MOV Y-LO,#75
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#55
    MOV Y-LO,#BC
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    RET
.phase2:
    MOV Y-HI,#2C
    MOV Y-LO,#35
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#2C
    MOV Y-LO,#7C
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#43
    MOV Y-LO,#F5
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#44
    MOV Y-LO,#3C
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#59
    MOV Y-LO,#35
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#59
    MOV Y-LO,#7C
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    RET
.phase3:
    MOV Y-HI,#2F
    MOV Y-LO,#F5
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#30
    MOV Y-LO,#3C
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#48
    MOV Y-LO,#F5
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#49
    MOV Y-LO,#3C
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#5C
    MOV Y-LO,#F5
    MOV A,#08
    MOV B,#11
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#5D
    MOV Y-LO,#3C
    MOV A,#08
    MOV B,#0C
    MOV C,#02
    CALL fill_rect
    RET

moving_platform:
    ; Platform tiles/pillars scroll laterally while train passes stations.
    MOV Y-HI,#6C
    MOV Y-LO,#59
    MOV A,#07
    MOV B,#59
    MOV C,#1A
    CALL fill_rect
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$000F
    CMP A,#00
    JZ .phase0
    CMP A,#01
    JZ .phase1
    CMP A,#02
    JZ .phase2
    JMP .phase3
.phase0:
    MOV Y-HI,#6C
    MOV Y-LO,#5E
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#7A
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#96
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#AC
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#59
    MOV A,#0E
    MOV B,#59
    MOV C,#02
    CALL fill_rect
    RET
.phase1:
    MOV Y-HI,#6C
    MOV Y-LO,#5A
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#76
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#92
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#A8
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#59
    MOV A,#0E
    MOV B,#59
    MOV C,#02
    CALL fill_rect
    RET
.phase2:
    MOV Y-HI,#6C
    MOV Y-LO,#72
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#8E
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#A4
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#59
    MOV A,#0E
    MOV B,#59
    MOV C,#02
    CALL fill_rect
    RET
.phase3:
    MOV Y-HI,#6C
    MOV Y-LO,#6E
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#8A
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#A0
    MOV A,#08
    MOV B,#03
    MOV C,#19
    CALL fill_rect
    MOV Y-HI,#6C
    MOV Y-LO,#59
    MOV A,#0E
    MOV B,#59
    MOV C,#02
    CALL fill_rect
    RET

moving_sleepers:
    ; Tie spacing widens towards the front of the cab;
    ; phase increments are a function of distance, never wall-clock time.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$000F
    CMP A,#00
    JZ .phase0
    CMP A,#01
    JZ .phase1
    CMP A,#02
    JZ .phase2
    JMP .phase3
.phase0:
    MOV Y-HI,#65
    MOV Y-LO,#DA
    MOV A,#06
    MOV B,#0D
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#71
    MOV Y-LO,#14
    MOV A,#06
    MOV B,#1A
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#81
    MOV Y-LO,#4B
    MOV A,#06
    MOV B,#2B
    MOV C,#02
    CALL fill_rect
    RET
.phase1:
    MOV Y-HI,#67
    MOV Y-LO,#1A
    MOV A,#06
    MOV B,#0D
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#73
    MOV Y-LO,#94
    MOV A,#06
    MOV B,#1A
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#85
    MOV Y-LO,#0B
    MOV A,#06
    MOV B,#2B
    MOV C,#02
    CALL fill_rect
    RET
.phase2:
    MOV Y-HI,#68
    MOV Y-LO,#5A
    MOV A,#06
    MOV B,#0D
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#76
    MOV Y-LO,#14
    MOV A,#06
    MOV B,#1A
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#88
    MOV Y-LO,#CB
    MOV A,#06
    MOV B,#2B
    MOV C,#02
    CALL fill_rect
    RET
.phase3:
    MOV Y-HI,#69
    MOV Y-LO,#9A
    MOV A,#06
    MOV B,#0D
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#78
    MOV Y-LO,#94
    MOV A,#06
    MOV B,#1A
    MOV C,#02
    CALL fill_rect
    MOV Y-HI,#8C
    MOV Y-LO,#8B
    MOV A,#06
    MOV B,#2B
    MOV C,#02
    CALL fill_rect
    RET

moving_rails:
    ; Fixed rail vanishing point; moving ties make translation visible.
    MOV Y-HI,#62
    MOV Y-LO,#1B
    MOV A,#07
    MOV B,#02
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#62
    MOV Y-LO,#24
    MOV A,#07
    MOV B,#02
    MOV C,#0A
    CALL fill_rect
    MOV Y-HI,#6E
    MOV Y-LO,#97
    MOV A,#07
    MOV B,#02
    MOV C,#08
    CALL fill_rect
    MOV Y-HI,#6E
    MOV Y-LO,#A8
    MOV A,#07
    MOV B,#02
    MOV C,#08
    CALL fill_rect
    MOV Y-HI,#78
    MOV Y-LO,#93
    MOV A,#07
    MOV B,#03
    MOV C,#09
    CALL fill_rect
    MOV Y-HI,#78
    MOV Y-LO,#AD
    MOV A,#07
    MOV B,#03
    MOV C,#09
    CALL fill_rect
    MOV Y-HI,#83
    MOV Y-LO,#CC
    MOV A,#07
    MOV B,#03
    MOV C,#0B
    CALL fill_rect
    MOV Y-HI,#83
    MOV Y-LO,#F7
    MOV A,#07
    MOV B,#03
    MOV C,#0B
    CALL fill_rect
    RET

moving_signal:
    ; Signal head approaches from the distance, grows, passes, reappears.
    ; Sample aspects follow Metrostroi-like ALS/ARS codes.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0005
    AND A,#1F
    CMP A,#08
    JC .far
    CMP A,#10
    JC .medium
    CMP A,#18
    JC .near
    JMP .close
.far:
    MOV Y-HI,#32
    MOV Y-LO,#B2
    MOV A,#08
    MOV B,#05
    MOV C,#09
    CALL fill_rect
    MOV Y-HI,#35
    MOV Y-LO,#33
    MOV B,#03
    MOV C,#03
    JMP .lamp
.medium:
    MOV Y-HI,#36
    MOV Y-LO,#74
    MOV A,#08
    MOV B,#07
    MOV C,#0E
    CALL fill_rect
    MOV Y-HI,#3B
    MOV Y-LO,#76
    MOV B,#03
    MOV C,#04
    JMP .lamp
.near:
    MOV Y-HI,#3C
    MOV Y-LO,#B7
    MOV A,#08
    MOV B,#0A
    MOV C,#16
    CALL fill_rect
    MOV Y-HI,#44
    MOV Y-LO,#3A
    MOV B,#04
    MOV C,#06
    JMP .lamp
.close:
    MOV Y-HI,#49
    MOV Y-LO,#3B
    MOV A,#08
    MOV B,#0D
    MOV C,#23
    CALL fill_rect
    MOV Y-HI,#53
    MOV Y-LO,#3E
    MOV B,#06
    MOV C,#08
.lamp:
    ; A = green, yellow, or red. Preserve Y/B/C from above.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$000C
    CMP A,#00
    JZ .red
    CMP A,#50
    JZ .green
    MOV A,#0E
    JMP .draw
.red:
    MOV A,#0C
    JMP .draw
.green:
    MOV A,#0A
.draw:
    CALL fill_rect
    RET


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

TXT_T2:
    .asciz "T2"
TXT_T1A:
    .asciz "T1A"
TXT_T1:
    .asciz "T1"
TXT_ZERO:
    .asciz "0"
TXT_X1:
    .asciz "X1"
TXT_X2:
    .asciz "X2"
TXT_X3:
    .asciz "X3"
TXT_ARS0:
    .asciz "0"
TXT_ARS40:
    .asciz "40"
TXT_ARS60:
    .asciz "60"
TXT_ARS70:
    .asciz "70"
TXT_ARS80:
    .asciz "80"
TXT_DIGIT0:
    .asciz "0"
TXT_DIGIT1:
    .asciz "1"
TXT_DIGIT2:
    .asciz "2"
TXT_DIGIT3:
    .asciz "3"
TXT_DIGIT4:
    .asciz "4"
TXT_DIGIT5:
    .asciz "5"
TXT_DIGIT6:
    .asciz "6"
TXT_DIGIT7:
    .asciz "7"
TXT_DIGIT8:
    .asciz "8"
TXT_DIGIT9:
    .asciz "9"

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
