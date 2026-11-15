#ifndef PPA_VGA_H
#define PPA_VGA_H
#include "ppa_module.h"

enum {
    PPA_VGA_REG_CONTROL = 0x0010,
    PPA_VGA_REG_STATUS = 0x0011,
    PPA_VGA_REG_MODE = 0x0012,
    PPA_VGA_REG_BORDER = 0x0013,
    PPA_VGA_REG_DAC_INDEX = 0x0014,
    PPA_VGA_REG_DAC_R = 0x0015,
    PPA_VGA_REG_DAC_G = 0x0016,
    PPA_VGA_REG_DAC_B = 0x0017,
    PPA_VGA_REG_WIDTH_LO = 0x0018,
    PPA_VGA_REG_WIDTH_HI = 0x0019,
    PPA_VGA_REG_HEIGHT_LO = 0x001A,
    PPA_VGA_REG_HEIGHT_HI = 0x001B
};

#define PPA_VGA_VRAM_OFFSET 0x10000u

void ppa_vga_init(PpaModule *module);
void ppa_vga_reset(PpaModule *module);
uint8_t ppa_vga_read(const PpaModule *module, uint32_t offset);
void ppa_vga_write(PpaModule *module, uint32_t offset, uint8_t value);

#endif
