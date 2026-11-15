#ifndef PPA_PS2_GUI_H
#define PPA_PS2_GUI_H
#include "ppa_module_ui.h"
void ppa_ps2_gui_draw(SDL_Renderer *r, TTF_Font *font,
                      const PPA1ModuleInfo *info, int slot);
#endif
