#ifndef PPA_PS2_H
#define PPA_PS2_H
#include "ppa_module.h"

/* PPA-PS/2/1 presents a PS/2 Set-2 keyboard through a PPAM slot register bank. */
enum {
    PPA_PS2_DATA = 0x0010,       /* R: pop next scan byte */
    PPA_PS2_STATUS = 0x0011,     /* R: bit0 available, bit1 overflow, bit2 scanning */
    PPA_PS2_CONTROL = 0x0012,    /* R/W: bit0 scanning enabled; bit7 clear FIFO */
    PPA_PS2_COMMAND = 0x0013,    /* W: PS/2 keyboard command */
    PPA_PS2_COUNT = 0x0014,      /* R: buffered byte count */
    PPA_PS2_LAST = 0x0015,       /* R: most recent scan code (without prefixes) */
    PPA_PS2_LEDS = 0x0016       /* R: LED mask, Scroll/Num/Caps */
};

void ppa_ps2_reset(PpaModule *m);
uint8_t ppa_ps2_read(PpaModule *m, uint32_t offset);
void ppa_ps2_write(PpaModule *m, uint32_t offset, uint8_t value);
void ppa_ps2_key(PpaModule *m, uint8_t scan_set2, bool extended, bool down);
#endif
