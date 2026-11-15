#ifndef PPA_MODULE_UI_H
#define PPA_MODULE_UI_H
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdint.h>
#include <stdbool.h>

/* Snapshot supplied by the emulator backend; module UIs own their graphics. */
typedef struct {
    bool installed;
    bool active;
    bool light_on;
    uint16_t type;
    bool sound_master;
    uint8_t sound_volume;
    uint16_t freq[4];
    uint8_t voice_volume[4];
    uint8_t waveform[4];
    bool voice_enabled[4];
    int8_t scope[128];
    uint32_t cycles_remaining;
    char name[64];
    char path[1024];
} PPA1ModuleInfo;

void ppa_module_ui_draw(SDL_Renderer *renderer, TTF_Font *font,
                        const PPA1ModuleInfo *info, int slot);

#endif
