#include "ppa_module.h"
#include "ppa_sound.h"
#include "ppa_light.h"
#include "ppa_vga.h"
#include "ppa_ps2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * PPAM 1.0: binary, little-endian. No native executable code is loaded.
 * Header (24 bytes):
 *   00  "PPAM"
 *   04  version u16 (1)
 *   06  type u16 (1 = LIGHT)
 *   08  code_size u32
 *   0C  data_size u32 (may be larger than 2 MiB)
 *   10  default_pulse_cycles u32
 *   14  flags u32 (reserved, must be 0)
 * Code is a series of eight-byte records:
 *   event u8 (1=WRITE), action u8 (1=LIGHT_PULSE),
 *   slot_offset u32, pulse_cycles u16 (0=default).
 * Data after code is read-only, bank mapped from relative offset 0x10000.
 */
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void ppam_bus_init(PpaModuleBus *bus) {
    if (bus) memset(bus, 0, sizeof(*bus));
}
void ppam_eject(PpaModuleBus *bus, int slot) {
    if (!bus || slot < 0 || slot >= PPAM_SLOTS) return;
    PpaModule *m = &bus->slots[slot];
    free(m->code);
    free(m->data);
    free(m->vga_vram);
    memset(m, 0, sizeof(*m));
}
void ppam_bus_destroy(PpaModuleBus *bus) {
    if (!bus) return;
    for (int i = 0; i < PPAM_SLOTS; ++i) ppam_eject(bus, i);
}
bool ppam_load(PpaModuleBus *bus, int slot, const char *path) {
    if (!bus || slot < 0 || slot >= PPAM_SLOTS || !path || !*path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[24];
    bool ok = fread(h, 1, sizeof(h), f) == sizeof(h);
    uint16_t type = ok ? le16(h + 6) : 0;
    uint32_t code_len = ok ? le32(h + 8) : 0;
    uint32_t data_len = ok ? le32(h + 12) : 0;
    uint32_t pulse = ok ? le32(h + 16) : 0;
    ok = ok && !memcmp(h, "PPAM", 4) && le16(h + 4) == PPAM_VERSION &&
         (type == PPAM_TYPE_LIGHT || type == PPAM_TYPE_SOUND ||
          type == PPAM_TYPE_VGA || type == PPAM_TYPE_PS2) && code_len <= PPAM_MAX_CODE &&
         code_len % 8 == 0 && data_len <= PPAM_MAX_DATA &&
         le32(h + 20) == 0 && strlen(path) < sizeof(bus->slots[slot].path);
    uint8_t *code = NULL, *data = NULL;
    if (ok && code_len) {
        code = malloc(code_len);
        ok = code && fread(code, 1, code_len, f) == code_len;
    }
    if (ok && data_len) {
        data = malloc(data_len);
        ok = data && fread(data, 1, data_len, f) == data_len;
    }
    /* Fail closed: malformed/truncated files never replace a live card. */
    if (ok) ok = fgetc(f) == EOF && !ferror(f);
    fclose(f);
    if (!ok) { free(code); free(data); return false; }
    for (uint32_t i = 0; i < code_len; i += 8) {
        if (code[i] != 1 || code[i+1] != 1 ||
            le32(code+i+2) >= 65536u) {
            free(code); free(data); return false;
        }
    }
    ppam_eject(bus, slot);
    PpaModule *m = &bus->slots[slot];
    m->installed = true;
    m->active = false; /* Explicit activation in MODULES UI. */
    m->type = type;
    m->pulse_cycles = pulse ? pulse : 1000u;
    m->code = code;
    m->code_size = code_len;
    m->data = data;
    m->data_size = data_len;
    if (type == PPAM_TYPE_SOUND) {
        snprintf(m->name, sizeof(m->name), "%s", "PPA-SOUND/1");
        ppa_sound_init(m);
    } else if (type == PPAM_TYPE_PS2) {
        snprintf(m->name, sizeof(m->name), "%s", "PPA-PS/2/1");
        ppa_ps2_reset(m);
    } else if (type == PPAM_TYPE_VGA) {
        snprintf(m->name, sizeof(m->name), "%s", "PPA-VGA/1");
        ppa_vga_init(m);
    } else {
        snprintf(m->name, sizeof(m->name), "%s", "LIGHT");
    }
    snprintf(m->path, sizeof(m->path), "%s", path);
    return true;
}
bool ppam_set_active(PpaModuleBus *bus, int slot, bool active) {
    if (!bus || slot < 0 || slot >= PPAM_SLOTS ||
        !bus->slots[slot].installed) return false;
    PpaModule *m = &bus->slots[slot];
    m->active = active;
    ppa_light_reset(m);
    if (m->type == PPAM_TYPE_VGA) ppa_vga_reset(m);
    if (m->type == PPAM_TYPE_PS2) ppa_ps2_reset(m);
    return true;
}
void ppam_reset(PpaModuleBus *bus) {
    if (!bus) return;
    for (int i = 0; i < PPAM_SLOTS; ++i) {
        ppa_light_reset(&bus->slots[i]);
        ppa_sound_reset(&bus->slots[i]);
        ppa_vga_reset(&bus->slots[i]);
        ppa_ps2_reset(&bus->slots[i]);
    }
}
void ppam_tick(PpaModuleBus *bus, uint32_t cycles) {
    if (!bus) return;
    for (int i = 0; i < PPAM_SLOTS; ++i)
        if (bus->slots[i].type == PPAM_TYPE_LIGHT)
            ppa_light_tick(&bus->slots[i], cycles);
}
static PpaModule *get_bank(PpaModuleBus *bus, uint16_t bank) {
    if (!bus || bank < 0x2000u || bank > 0x9FFFu) return NULL;
    PpaModule *m = &bus->slots[(bank - 0x2000u) >> 12];
    return m->installed && m->active ? m : NULL;
}
uint8_t ppam_read(PpaModuleBus *bus, uint16_t bank, uint16_t addr) {
    PpaModule *m = get_bank(bus, bank);
    if (!m) return 0xFF;
    uint32_t offset = (((uint32_t)bank & 0x0FFFu) << 16) | addr;
    switch (offset) {
        case 0x00: return 'P';
        case 0x01: return 'P';
        case 0x02: return (uint8_t)m->type;
        case 0x03: return PPAM_VERSION;
        default: break;
    }
    if (m->type == PPAM_TYPE_SOUND && offset < 0x10000u)
        return ppa_sound_read(m, offset);
    if (m->type == PPAM_TYPE_LIGHT && offset < 0x10000u)
        return ppa_light_read(m, offset);
    if (m->type == PPAM_TYPE_VGA)
        return ppa_vga_read(m, offset);
    if (m->type == PPAM_TYPE_PS2 && offset < 0x10000u)
        return ppa_ps2_read(m, offset);
    if (offset >= 0x10000u && offset - 0x10000u < m->data_size)
        return m->data[offset - 0x10000u];
    return 0xFF;
}
void ppam_write(PpaModuleBus *bus, uint16_t bank,
                uint16_t addr, uint8_t value) {
    PpaModule *m = get_bank(bus, bank);
    if (!m) return;
    uint32_t offset = (((uint32_t)bank & 0x0FFFu) << 16) | addr;
    if (m->type == PPAM_TYPE_SOUND) ppa_sound_write(m, offset, value);
    else if (m->type == PPAM_TYPE_LIGHT) ppa_light_write(m, offset, value);
    else if (m->type == PPAM_TYPE_VGA) ppa_vga_write(m, offset, value);
    else if (m->type == PPAM_TYPE_PS2 && offset < 0x10000u)
        ppa_ps2_write(m, offset, value);
}
const PpaModule *ppam_get(const PpaModuleBus *bus, int slot) {
    return bus && slot >= 0 && slot < PPAM_SLOTS ? &bus->slots[slot] : NULL;
}
