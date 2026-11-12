#include "gui.h"
#include "logger.h"

void ppa_gui_init_state(PPA1GuiState *state, bool no_gui) {
    if (!state) return;

    state->requested = !no_gui;

    /*
     * Foundation commit:
     * SDL3 + Dear ImGui frontend will set this to true once the renderer
     * and CPU frontend callbacks are wired. Until then the same emulator
     * core continues in compatibility-console mode.
     */
    state->available = false;
}

void ppa_gui_log_mode(const PPA1GuiState *state) {
    if (!state) return;

    if (!state->requested) {
        ppa_log_printf("Mode: --nogui (console)\n");
        return;
    }

    if (!state->available) {
        ppa_log_printf("Mode: GUI requested; frontend foundation active, using console compatibility mode\n");
        return;
    }

    ppa_log_printf("Mode: GUI\n");
}
