#include "ppa_sound_gui.h"
#include "ppa_sound_export.h"
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
        const float y = 366.0f + 32.0f * ch;
        snprintf(text, sizeof(text), "CH%d  %5u Hz   W%d   VOL %3u   %s",
            ch+1, (unsigned)info->freq[ch], (unsigned)info->waveform[ch],
            (unsigned)info->voice_volume[ch],
            info->voice_enabled[ch] ? "ON" : "OFF");
        ppa_mod_text(r, font, 34, y,
            info->voice_enabled[ch] ? (SDL_Color){137,244,157,255} :
                                      (SDL_Color){143,150,145,255}, text);
        snprintf(text, sizeof(text), "    ADSR  %3u/%3u/%3u/%3u   GATE %u   ENV %3u",
            (unsigned)info->attack[ch], (unsigned)info->decay[ch],
            (unsigned)info->sustain[ch], (unsigned)info->release[ch],
            (unsigned)info->gate[ch], (unsigned)info->envelope_level[ch]);
        ppa_mod_text(r, font, 34, y + 15,
                     (SDL_Color){240,167,65,255}, text);
    }
    ppa_mod_text(r, font, 28, 498, (SDL_Color){164,171,166,255},
                 "ADSR: +5/+6/+7/+8   GATE: +9   (per channel)");

    /* Host-side rendering controls are not mapped into the PPA bus.
     * These sit below the original 510px hardware instrument panel. */
    SDL_FRect exporter = {16, 514, 508, 94};
    SDL_SetRenderDrawColor(r, 41, 46, 44, 255);
    SDL_RenderFillRect(r, &exporter);
    SDL_SetRenderDrawColor(r, 114, 122, 117, 255);
    SDL_RenderRect(r, &exporter);
    ppa_mod_text(r, font, 28, 519, (SDL_Color){204,221,203,255},
                 "AUDIO RENDER  /  44.1 kHz MONO / 24-bit source");

    SDL_FRect fmt = {28, 544, 250, 34};
    SDL_FRect action = {288, 544, 222, 34};
    SDL_SetRenderDrawColor(r, 63, 77, 71, 255);
    SDL_RenderFillRect(r, &fmt);
    SDL_SetRenderDrawColor(r, 140, 169, 147, 255);
    SDL_RenderRect(r, &fmt);
    SDL_SetRenderDrawColor(r, info->rendering ? 125 : 44,
                          info->rendering ? 41 : 91,
                          info->rendering ? 40 : 61, 255);
    SDL_RenderFillRect(r, &action);
    SDL_SetRenderDrawColor(r, 178, 196, 183, 255);
    SDL_RenderRect(r, &action);

    static const char *names[] = {
        "WAV 24-bit", "FLAC 24-bit", "OGG Vorbis HQ", "MP3 320 kbps"
    };
    unsigned index = info->render_format < 4 ? info->render_format : 0;
    snprintf(text, sizeof(text), "FORMAT: %s", names[index]);
    ppa_mod_text(r, font, 38, 552, (SDL_Color){238,242,232,255}, text);
    ppa_mod_text(r, font, 314, 552, (SDL_Color){244,247,237,255},
                 info->rendering ? "STOP + SAVE" : "START RENDER");

    if (info->rendering) {
        unsigned seconds = (unsigned)(info->rendered_frames / 44100u);
        snprintf(text, sizeof(text), "RECORDING  %02u:%02u  |  %s",
                 seconds / 60u, seconds % 60u,
                 ppa_sound_export_format_name((PpaSoundExportFormat)index));
    } else if (info->render_status[0]) {
        snprintf(text, sizeof(text), "%s", info->render_status);
    } else {
        snprintf(text, sizeof(text),
                 "Select a format, choose a file, then play your ROM.");
    }
    ppa_mod_text(r, font, 29, 585, (SDL_Color){177,218,180,255}, text);
}
