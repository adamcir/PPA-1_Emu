#ifndef PPA1_GUI_H
#define PPA1_GUI_H

#include <stdbool.h>

typedef struct {
    bool requested;
    bool available;
} PPA1GuiState;

void ppa_gui_init_state(PPA1GuiState *state, bool no_gui);
void ppa_gui_log_mode(const PPA1GuiState *state);

#endif
