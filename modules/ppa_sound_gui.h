#ifndef PPA_SOUND_GUI_H
#define PPA_SOUND_GUI_H
#include "ppa_module_ui.h"
void ppa_sound_gui_draw(SDL_Renderer *renderer, TTF_Font *font,
                        const PPA1ModuleInfo *info, int slot);
#endif
