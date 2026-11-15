#ifndef PPA_LIGHT_H
#define PPA_LIGHT_H
#include "ppa_module.h"

void ppa_light_reset(PpaModule *module);
void ppa_light_tick(PpaModule *module, uint32_t cycles);
uint8_t ppa_light_read(const PpaModule *module, uint32_t offset);
void ppa_light_write(PpaModule *module, uint32_t offset, uint8_t value);

#endif
