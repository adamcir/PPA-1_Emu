#include "ppa_module.h"
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
         type == PPAM_TYPE_LIGHT && code_len <= PPAM_MAX_CODE &&
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
    snprintf(m->name, sizeof(m->name), "LIGHT");
    snprintf(m->path, sizeof(m->path), "%s", path);
    return true;
}
bool ppam_set_active(PpaModuleBus *bus, int slot, bool active) {
    if (!bus || slot < 0 || slot >= PPAM_SLOTS ||
        !bus->slots[slot].installed) return false;
    PpaModule *m = &bus->slots[slot];
    m->active = active;
    m->light_on = false;
    m->remaining_cycles = 0;
    return true;
}
void ppam_reset(PpaModuleBus *bus) {
    if (!bus) return;
    for (int i = 0; i < PPAM_SLOTS; ++i) {
        bus->slots[i].light_on = false;
        bus->slots[i].remaining_cycles = 0;
    }
}
void ppam_tick(PpaModuleBus *bus, uint32_t cycles) {
    if (!bus) return;
    for (int i = 0; i < PPAM_SLOTS; ++i) {
        PpaModule *m = &bus->slots[i];
        if (!m->active || !m->light_on) continue;
        if (cycles >= m->remaining_cycles) {
            m->remaining_cycles = 0;
            m->light_on = false;
        } else m->remaining_cycles -= cycles;
    }
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
        case 0x10: return m->light_on ? 1 : 0;
        case 0x11: return (uint8_t)m->remaining_cycles;
        case 0x12: return (uint8_t)(m->remaining_cycles >> 8);
        default: break;
    }
    if (offset >= 0x10000u && offset - 0x10000u < m->data_size)
        return m->data[offset - 0x10000u];
    return 0xFF;
}
void ppam_write(PpaModuleBus *bus, uint16_t bank,
                uint16_t addr, uint8_t value) {
    PpaModule *m = get_bank(bus, bank);
    if (!m) return;
    uint32_t offset = (((uint32_t)bank & 0x0FFFu) << 16) | addr;
    for (uint32_t i = 0; i < m->code_size; i += 8) {
        const uint8_t *rule = m->code + i;
        if (rule[0] != 1 || rule[1] != 1 || le32(rule+2) != offset) continue;
        uint16_t rule_duration = le16(rule+6);
        m->light_on = value != 0;
        m->remaining_cycles = m->light_on
            ? (rule_duration ? rule_duration : m->pulse_cycles) : 0;
    }
}
const PpaModule *ppam_get(const PpaModuleBus *bus, int slot) {
    return bus && slot >= 0 && slot < PPAM_SLOTS ? &bus->slots[slot] : NULL;
}
