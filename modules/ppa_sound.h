#ifndef PPA_SOUND_H
#define PPA_SOUND_H
#include "ppa_module.h"

void ppa_sound_init(PpaModule *module);
void ppa_sound_reset(PpaModule *module);
uint8_t ppa_sound_read(const PpaModule *module, uint32_t offset);
void ppa_sound_write(PpaModule *module, uint32_t offset, uint8_t value);
void ppam_mix_audio(PpaModuleBus *bus, int16_t *output, uint32_t frames,
                    uint32_t sample_rate);

#endif
