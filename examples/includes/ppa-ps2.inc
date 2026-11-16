; PPA-PS/2/1 register definitions (any active slot, discovered by type 4).
; Include ppa-slots.inc before using slots_bind_ps2 / slots_use_ps2.
PS2_DATA EQU $0010
PS2_STATUS EQU $0011
PS2_CONTROL EQU $0012
PS2_COMMAND EQU $0013
PS2_COUNT EQU $0014
PS2_LAST EQU $0015
PS2_LEDS EQU $0016

; Commands: F2 identify, F4 scan on, F5 scan off, FF reset,
; ED set LED (follow with LED mask), EE echo.
; STATUS bit 0 = DATA READY, bit 1 = OVERFLOW, bit 2 = SCANNING.
