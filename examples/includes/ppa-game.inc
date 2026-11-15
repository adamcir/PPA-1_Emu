; PPA-1 helper library for simple games (keyboard, pacing and RNG)
; (c) 2026 Adam Cir (Adava), Adava Software, Adava Development
; Requires PPA-PS/2/1 in slot 2 (bank 3000).
; RAM 1000:0040 and 0041 reserved for this library.
;
; game_keyboard_init   -- initialize PS/2 scanner (set-2), flush FIFO
; game_poll_key        -- A=key scan code (0 if empty), B=0 make / B=1 break
; game_delay           -- D=number of short delay iterations, clobbers C/D
; game_random8         -- A=pseudorandom byte, seed 1000:0041

GAME_BREAK_FLAG EQU $0040
GAME_RNG_SEED   EQU $0041

game_keyboard_init:
    MOV BR-HI,#10
    MOV BR-LO,#00
    MOV A,#00
    ST $0040,A
    MOV A,#5D
    ST $0041,A
    MOV BR-HI,#30
    MOV BR-LO,#00
    MOV A,#81
    ST $0012,A
    RET

game_poll_key:
.next:
    MOV BR-HI,#30
    MOV BR-LO,#00
    LD A,$0011
    AND A,#01
    JZ .none
    LD A,$0010
    CMP A,#F0
    JZ .break_prefix
    CMP A,#E0
    JZ .next
    MOV C,A
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD B,$0040
    MOV A,#00
    ST $0040,A
    MOV A,C
    RET
.break_prefix:
    MOV BR-HI,#10
    MOV BR-LO,#00
    MOV A,#01
    ST $0040,A
    JMP .next
.none:
    MOV A,#00
    MOV B,#00
    RET

game_delay:
    CMP D,#00
    JZ .done
.outer:
    MOV C,#80
.inner:
    DEC C
    JNZ .inner
    DEC D
    JNZ .outer
.done:
    RET

game_random8:
    ; Simple deterministic XOR/shift PRNG for game variation.
    MOV BR-HI,#10
    MOV BR-LO,#00
    LD A,$0041
    CMP A,#00
    JNZ .seed_ok
    MOV A,#5D
.seed_ok:
    MOV B,A
    SHL A
    XOR A,B
    MOV B,A
    SHR A
    SHR A
    XOR A,B
    ST $0041,A
    RET
