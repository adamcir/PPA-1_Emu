#include "ppa_module_ui.h"
#include "ppa_sound_gui.h"
#include "ppa_light_gui.h"
#include "ppa_vga_gui.h"
#include "ppa_ps2_gui.h"

void ppa_module_ui_draw(SDL_Renderer *renderer, TTF_Font *font,
                        const PPA1ModuleInfo *info, int slot) {
    if (!renderer || !info || !info->installed) return;
    if (info->type == 4) ppa_ps2_gui_draw(renderer, font, info, slot);
    else if (info->type == 3) ppa_vga_gui_draw(renderer, font, info, slot);
    else if (info->type == 2) ppa_sound_gui_draw(renderer, font, info, slot);
    else if (info->type == 1) ppa_light_gui_draw(renderer, font, info, slot);
}
