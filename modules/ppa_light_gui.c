#include "ppa_light_gui.h"
#include "ppa_module_draw.h"
#include <stdio.h>
void ppa_light_gui_draw(SDL_Renderer *r, TTF_Font *font,
                        const PPA1ModuleInfo *info, int slot) {
    if (!r || !info) return;
    ppa_mod_face(r, "LIGHT/1  |  INDICATOR CARD", font);
    char text[128];
    snprintf(text,sizeof(text),"SLOT %d  /  %s",slot+1,
             info->active?"ACTIVE":"DISABLED");
    ppa_mod_text(r,font,30,84,(SDL_Color){190,208,191,255},text);
    ppa_mod_circle(r,155,259,108,(SDL_Color){15,19,18,255});
    ppa_mod_circle(r,155,259,80,
        info->active && info->light_on ? (SDL_Color){106,236,92,255} :
                                         (SDL_Color){39,63,44,255});
    ppa_mod_text(r,font,300,210,
        info->light_on ? (SDL_Color){135,255,112,255} :
                         (SDL_Color){154,164,155,255},
        info->light_on ? "LIGHT ON" : "LIGHT OFF");
    snprintf(text,sizeof(text),"PULSE: %u cycles",info->cycles_remaining);
    ppa_mod_text(r,font,300,262,(SDL_Color){235,235,225,255},text);
    ppa_mod_text(r,font,300,305,(SDL_Color){172,190,173,255},
                 "REGISTER: 0010");
    ppa_mod_text(r,font,28,464,(SDL_Color){158,168,161,255},
                 "WRITE NONZERO: PULSE ON   /   WRITE ZERO: OFF");
}
