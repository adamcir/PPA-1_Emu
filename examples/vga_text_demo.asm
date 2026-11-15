; PPA-VGA/1 text mode 02h demo (SLOT 1, module active).
; Text VRAM: bank 2001, 80x25, char/attribute pairs.
.org $0000
    JMP start

.include "includes/ppa-vga.inc"

start:
    MOV BR-HI,#20
    MOV BR-LO,#00
    MOV A,#02
    ST $0012,A          ; mode 02h, 80x25
    MOV A,#01
    ST $0010,A          ; enable video

    MOV BR-LO,#01
    MOV A,#50           ; P
    ST $0000,A
    MOV A,#1E           ; yellow on blue
    ST $0001,A
    MOV A,#50           ; P
    ST $0002,A
    MOV A,#1E
    ST $0003,A
    MOV A,#41           ; A
    ST $0004,A
    MOV A,#1E
    ST $0005,A
    MOV A,#2D           ; -
    ST $0006,A
    MOV A,#1E
    ST $0007,A
    MOV A,#56           ; V
    ST $0008,A
    MOV A,#1E
    ST $0009,A
    MOV A,#47           ; G
    ST $000A,A
    MOV A,#1E
    ST $000B,A
    MOV A,#41           ; A
    ST $000C,A
    MOV A,#1E
    ST $000D,A
    MOV A,#2F           ; /
    ST $000E,A
    MOV A,#1E
    ST $000F,A
    MOV A,#31           ; 1
    ST $0010,A
    MOV A,#1E
    ST $0011,A

    HLT
