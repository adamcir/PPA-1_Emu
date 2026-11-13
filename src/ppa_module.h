#ifndef PPA_MODULE_H
#define PPA_MODULE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PPAM_SLOTS 8
#define PPAM_VERSION 1u
#define PPAM_TYPE_LIGHT 1u
#define PPAM_MAX_CODE 65536u
#define PPAM_MAX_DATA (256u * 1024u * 1024u - 65536u)

typedef struct {
    bool installed;
    bool active;
    bool light_on;
    uint16_t type;
    uint32_t remaining_cycles;
    uint32_t pulse_cycles;
    uint8_t *code;
    uint32_t code_size;
    uint8_t *data;
    uint32_t data_size;
    char path[1024];
    char name[64];
} PpaModule;

typedef struct {
    PpaModule slots[PPAM_SLOTS];
} PpaModuleBus;

void ppam_bus_init(PpaModuleBus *bus);
void ppam_bus_destroy(PpaModuleBus *bus);
bool ppam_load(PpaModuleBus *bus, int slot, const char *path);
void ppam_eject(PpaModuleBus *bus, int slot);
bool ppam_set_active(PpaModuleBus *bus, int slot, bool active);
void ppam_reset(PpaModuleBus *bus);
void ppam_tick(PpaModuleBus *bus, uint32_t cycles);
uint8_t ppam_read(PpaModuleBus *bus, uint16_t bank, uint16_t addr);
void ppam_write(PpaModuleBus *bus, uint16_t bank, uint16_t addr, uint8_t value);
const PpaModule *ppam_get(const PpaModuleBus *bus, int slot);
#endif
