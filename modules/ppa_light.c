#include "ppa_light.h"

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
void ppa_light_reset(PpaModule *m) {
    m->light_on = false;
    m->remaining_cycles = 0;
}
void ppa_light_tick(PpaModule *m, uint32_t cycles) {
    if (!m->active || !m->light_on) return;
    if (cycles >= m->remaining_cycles) {
        m->remaining_cycles = 0;
        m->light_on = false;
    } else m->remaining_cycles -= cycles;
}
uint8_t ppa_light_read(const PpaModule *m, uint32_t offset) {
    switch (offset) {
        case 0x10: return m->light_on ? 1 : 0;
        case 0x11: return (uint8_t)m->remaining_cycles;
        case 0x12: return (uint8_t)(m->remaining_cycles >> 8);
        default: return 0xFF;
    }
}
void ppa_light_write(PpaModule *m, uint32_t offset, uint8_t value) {
    for (uint32_t i = 0; i < m->code_size; i += 8) {
        const uint8_t *rule = m->code + i;
        if (rule[0] != 1 || rule[1] != 1 || le32(rule + 2) != offset) continue;
        uint16_t rule_duration = le16(rule + 6);
        m->light_on = value != 0;
        m->remaining_cycles = m->light_on
            ? (rule_duration ? rule_duration : m->pulse_cycles) : 0;
    }
}
