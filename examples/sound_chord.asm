; PPA-SOUND/1 test. Insert/activate modules/ppa-sound-1.ppamod in SLOT 1.
; Load assembled EEPROM image, then PWR ON and RUN. Try 100 kHz.
.org $0000
    JMP start
.include "includes/ppa-sound.inc"

start:
    CALL sound_init
    ; Am: A3=220 C4=262 E4=330 A4=440 Hz
    MOV B,#DC
    MOV C,#00
    CALL sound_voice1
    MOV B,#06
    MOV C,#01
    CALL sound_voice2
    MOV B,#4A
    MOV C,#01
    CALL sound_voice3
    MOV B,#B8
    MOV C,#01
    CALL sound_voice4
    MOV D,#A0
hold_outer:
    MOV X-LO,#FF
hold_inner:
    DEC X-LO
    JNZ hold_inner
    DEC D
    JNZ hold_outer
    CALL sound_stop
    HLT
