#include "ppa_vga.h"
#include <string.h>

static uint8_t expand3(unsigned v) { return (uint8_t)((v * 255u) / 7u); }
static uint8_t expand2(unsigned v) { return (uint8_t)((v * 255u) / 3u); }

static void ppa_vga_default_palette(PpaModule *m) {
    for (unsigned i = 0; i < PPAM_VGA_PALETTE_COLORS; ++i) {
        m->vga_palette[i * 3u + 0u] = expand3((i >> 5) & 7u);
        m->vga_palette[i * 3u + 1u] = expand3((i >> 2) & 7u);
        m->vga_palette[i * 3u + 2u] = expand2(i & 3u);
    }
}

void ppa_vga_init(PpaModule *m) {
    if (!m) return;
    ppa_vga_reset(m);
}

void ppa_vga_reset(PpaModule *m) {
    if (!m || m->type != PPAM_TYPE_VGA) return;
    m->vga_enabled = false;
    m->vga_mode = 0;
    m->vga_border = 0;
    m->vga_palette_index = 0;
    memset(m->vga_vram, 0, sizeof(m->vga_vram));
    ppa_vga_default_palette(m);
}

uint8_t ppa_vga_read(const PpaModule *m, uint32_t offset) {
    if (!m || m->type != PPAM_TYPE_VGA) return 0xFF;

    if (offset >= PPA_VGA_VRAM_OFFSET &&
        offset < PPA_VGA_VRAM_OFFSET + PPAM_VGA_VRAM_SIZE)
        return m->vga_vram[offset - PPA_VGA_VRAM_OFFSET];

    switch (offset) {
        case PPA_VGA_REG_CONTROL: return m->vga_enabled ? 1u : 0u;
        case PPA_VGA_REG_STATUS: return (uint8_t)(1u | (m->vga_enabled ? 2u : 0u));
        case PPA_VGA_REG_MODE: return m->vga_mode;
        case PPA_VGA_REG_BORDER: return m->vga_border;
        case PPA_VGA_REG_DAC_INDEX: return m->vga_palette_index;
        case PPA_VGA_REG_DAC_R:
            return m->vga_palette[(unsigned)m->vga_palette_index * 3u + 0u];
        case PPA_VGA_REG_DAC_G:
            return m->vga_palette[(unsigned)m->vga_palette_index * 3u + 1u];
        case PPA_VGA_REG_DAC_B:
            return m->vga_palette[(unsigned)m->vga_palette_index * 3u + 2u];
        case PPA_VGA_REG_WIDTH_LO: return (uint8_t)PPAM_VGA_WIDTH;
        case PPA_VGA_REG_WIDTH_HI: return (uint8_t)(PPAM_VGA_WIDTH >> 8);
        case PPA_VGA_REG_HEIGHT_LO: return (uint8_t)PPAM_VGA_HEIGHT;
        case PPA_VGA_REG_HEIGHT_HI: return (uint8_t)(PPAM_VGA_HEIGHT >> 8);
        default: return 0xFF;
    }
}

void ppa_vga_write(PpaModule *m, uint32_t offset, uint8_t value) {
    if (!m || m->type != PPAM_TYPE_VGA) return;

    if (offset >= PPA_VGA_VRAM_OFFSET &&
        offset < PPA_VGA_VRAM_OFFSET + PPAM_VGA_VRAM_SIZE) {
        m->vga_vram[offset - PPA_VGA_VRAM_OFFSET] = value;
        return;
    }

    switch (offset) {
        case PPA_VGA_REG_CONTROL: m->vga_enabled = (value & 1u) != 0; break;
        case PPA_VGA_REG_MODE:
            if (value == 0) m->vga_mode = 0;
            break;
        case PPA_VGA_REG_BORDER: m->vga_border = value; break;
        case PPA_VGA_REG_DAC_INDEX: m->vga_palette_index = value; break;
        case PPA_VGA_REG_DAC_R:
            m->vga_palette[(unsigned)m->vga_palette_index * 3u + 0u] = value;
            break;
        case PPA_VGA_REG_DAC_G:
            m->vga_palette[(unsigned)m->vga_palette_index * 3u + 1u] = value;
            break;
        case PPA_VGA_REG_DAC_B:
            m->vga_palette[(unsigned)m->vga_palette_index * 3u + 2u] = value;
            m->vga_palette_index++;
            break;
        default: break;
    }
}
