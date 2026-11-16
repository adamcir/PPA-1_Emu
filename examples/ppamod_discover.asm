; Dynamically find PPA-SOUND/1 in any PPAMOD slot using type ID 2.
; The F000:FF30..FF36 discovery ports return the first active match.
; Identical modules can be enumerated with FF32 occurrence = 0,1,2...
.org $0000
start:
    MOV BR-HI,#F0
    MOV BR-LO,#00
    MOV A,#02
    ST $FF30,A           ; type low = 2 (SOUND)
    MOV A,#00
    ST $FF31,A           ; type high = 0
    ST $FF32,A           ; occurrence = 0
    LD B,$FF35           ; slot number 1..8, FF = not found
    CMP B,#FF
    JZ no_sound
    LD C,$FF33           ; bank high
    LD D,$FF34           ; bank low
    MOV BR-HI,C
    MOV BR-LO,D          ; BR now points to selected SOUND module

    LD A,$0002           ; verify card type from common ID ROM
    CMP A,#02
    JNZ no_sound
    LD A,$0080           ; first byte of card name (ASCII 'P')
    CMP A,#50
    JNZ no_sound

    MOV A,#01
    ST $0010,A           ; SOUND master enabled on discovered card
    HLT
no_sound:
    HLT
