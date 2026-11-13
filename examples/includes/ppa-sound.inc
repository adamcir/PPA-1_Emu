; PPA-SOUND/1 shared ASM driver (SLOT 1 / bank 2000).
; Include after an initial JMP start; routines live in EEPROM.
; CALL sound_init: enable master + four voices, config waveforms/volume.
; CALL sound_voice1..4: B=frequency low, C=frequency high.
; CALL sound_silence: all frequency registers zero.
; CALL sound_stop: master off.
; Calls may modify BR, sound_init / sound_stop also modify A.
; Keep BR=0 when reading EEPROM with LD A,Y.

SOUND_ON EQU $0010
SOUND_MASTER_VOL EQU $0011
SOUND_CH1_LO EQU $0020
SOUND_CH1_HI EQU $0021
SOUND_CH1_WAVE EQU $0022
SOUND_CH1_VOL EQU $0023
SOUND_CH1_ENABLE EQU $0024
SOUND_CH2_LO EQU $0030
SOUND_CH2_HI EQU $0031
SOUND_CH2_WAVE EQU $0032
SOUND_CH2_VOL EQU $0033
SOUND_CH2_ENABLE EQU $0034
SOUND_CH3_LO EQU $0040
SOUND_CH3_HI EQU $0041
SOUND_CH3_WAVE EQU $0042
SOUND_CH3_VOL EQU $0043
SOUND_CH3_ENABLE EQU $0044
SOUND_CH4_LO EQU $0050
SOUND_CH4_HI EQU $0051
SOUND_CH4_WAVE EQU $0052
SOUND_CH4_VOL EQU $0053
SOUND_CH4_ENABLE EQU $0054

sound_init:
    MOV BR-HI,#20
    MOV BR-LO,#00
    MOV A,#FF
    ST $0011,A
    MOV A,#01
    ST $0010,A
    CALL sound_waveforms
    RET

sound_waveforms:
    MOV BR-HI,#20
    MOV BR-LO,#00
    MOV A,#02
    ST $0022,A
    ST $0042,A
    MOV A,#01
    ST $0032,A
    MOV A,#03
    ST $0052,A
    MOV A,#C8
    ST $0023,A
    MOV A,#A0
    ST $0033,A
    ST $0043,A
    MOV A,#FF
    ST $0053,A
    MOV A,#01
    ST $0024,A
    ST $0034,A
    ST $0044,A
    ST $0054,A
    RET

sound_voice1:
    MOV BR-HI,#20
    MOV BR-LO,#00
    ST $0020,B
    ST $0021,C
    RET
sound_voice2:
    MOV BR-HI,#20
    MOV BR-LO,#00
    ST $0030,B
    ST $0031,C
    RET
sound_voice3:
    MOV BR-HI,#20
    MOV BR-LO,#00
    ST $0040,B
    ST $0041,C
    RET
sound_voice4:
    MOV BR-HI,#20
    MOV BR-LO,#00
    ST $0050,B
    ST $0051,C
    RET

sound_silence:
    MOV B,#00
    MOV C,#00
    CALL sound_voice1
    CALL sound_voice2
    CALL sound_voice3
    CALL sound_voice4
    RET

sound_stop:
    MOV BR-HI,#20
    MOV BR-LO,#00
    MOV A,#00
    ST $0010,A
    RET
