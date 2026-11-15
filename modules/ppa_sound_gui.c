#include "ppa_sound_gui.h"
#include "ppa_module_draw.h"
#include <stdio.h>

/* Entire PPA-SOUND/1 VST-style face lives in the module, not the emulator.
 * The emulator supplies only a renderer, a font and a device-state snapshot. */
void ppa_sound_gui_draw(SDL_Renderer *r, TTF_Font *font,
                        const PPA1ModuleInfo *info, int slot) {
    if (!r || !info) return;
    ppa_mod_face(r, "PPA-SOUND/1  |  SYNTHESIZER", font);
    char text[160];
    snprintf(text, sizeof(text), "SLOT %d  /  %s  /  MASTER %s",
             slot + 1, info->active ? "ACTIVE" : "DISABLED",
             info->sound_master ? "ON" : "OFF");
    ppa_mod_text(r, font, 30, 83, (SDL_Color){169,213,170,255}, text);

    /* Built-in virtual loudspeaker and acoustic grille */
    ppa_mod_circle(r, 102, 217, 71, (SDL_Color){13,16,17,255});
    ppa_mod_circle(r, 102, 217, 59, (SDL_Color){98,104,100,255});
    ppa_mod_circle(r, 102, 217, 51, (SDL_Color){21,26,27,255});
    ppa_mod_circle(r, 102, 217, 25, (SDL_Color){68,75,72,255});
    ppa_mod_circle(r, 102, 217, 15, (SDL_Color){22,25,26,255});
    ppa_mod_text(r, font, 61, 298, (SDL_Color){221,226,218,255}, "SPEAKER");

    /* Continuous live audio oscilloscope with calibrated-style graticule */
    SDL_FRect screen = {195, 138, 304, 168};
    SDL_SetRenderDrawColor(r, 4, 17, 10, 255);
    SDL_RenderFillRect(r, &screen);
    SDL_SetRenderDrawColor(r, 90, 114, 94, 255);
    SDL_RenderRect(r, &screen);
    SDL_FRect plot = {202, 145, 290, 152};
    SDL_SetRenderDrawColor(r, 14, 47, 29, 255);
    for (int i = 1; i < 10; ++i) {
        float x = plot.x + plot.w * i / 10.f;
        SDL_RenderLine(r, x, plot.y, x, plot.y + plot.h);
    }
    for (int i = 1; i < 8; ++i) {
        float y = plot.y + plot.h * i / 8.f;
        SDL_RenderLine(r, plot.x, y, plot.x + plot.w, y);
    }
    SDL_SetRenderDrawColor(r, 34, 107, 64, 255);
    SDL_RenderLine(r, plot.x, plot.y + plot.h / 2,
                   plot.x + plot.w, plot.y + plot.h / 2);
    int trigger = 0;
    for (int i = 1; i < 127; ++i) {
        if (info->scope[i-1] <= 0 && info->scope[i] > 0) { trigger = i; break; }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 1; i < 128; ++i) {
        int prev = (trigger + i - 1) % 128, cur = (trigger + i) % 128;
        float x0 = plot.x + (i-1) * plot.w / 127.f;
        float x1 = plot.x + i * plot.w / 127.f;
        float y0 = plot.y + plot.h * .5f - info->scope[prev] * plot.h * .43f / 127.f;
        float y1 = plot.y + plot.h * .5f - info->scope[cur] * plot.h * .43f / 127.f;
        SDL_SetRenderDrawColor(r, 18, 118, 48, 120);
        SDL_RenderLine(r, x0, y0 - 1, x1, y1 - 1);
        SDL_RenderLine(r, x0, y0 + 1, x1, y1 + 1);
        SDL_SetRenderDrawColor(r, 132, 255, 154, 255);
        SDL_RenderLine(r, x0, y0, x1, y1);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    ppa_mod_text(r, font, 202, 311, (SDL_Color){103,233,127,255},
                 "OSCILLOSCOPE   /   LIVE WAVEFORM");

    snprintf(text, sizeof(text), "MASTER VOLUME: %u / 255", info->sound_volume);
    ppa_mod_text(r, font, 34, 345, (SDL_Color){240,241,234,255}, text);
    for (int ch=0; ch<4; ++ch) {
        snprintf(text, sizeof(text), "CH%d  %5u Hz   W%d   VOL %3u   %s",
            ch+1, (unsigned)info->freq[ch], (unsigned)info->waveform[ch],
            (unsigned)info->voice_volume[ch],
            info->voice_enabled[ch] ? "ON" : "OFF");
        ppa_mod_text(r, font, 34, 374 + 25.f*ch,
            info->voice_enabled[ch] ? (SDL_Color){137,244,157,255} :
                                      (SDL_Color){143,150,145,255}, text);
    }
    ppa_mod_text(r, font, 28, 478, (SDL_Color){164,171,166,255},
                 "CPU BUS: 0010 POWER  /  0011 VOL  /  0020-0054 CHANNELS");
}
