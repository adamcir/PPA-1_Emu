#ifndef PPA1_GUI_H
#define PPA1_GUI_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool requested;
    bool available;
} PPA1GuiState;

typedef struct {
    char rom_path[1024];
    bool tty_enabled;
} PPA1GuiLaunchOptions;

void ppa_gui_init_state(PPA1GuiState *state, bool no_gui);
void ppa_gui_log_mode(const PPA1GuiState *state);

/*
 * Opens the SDL3 launcher. Returns true when the user pressed Start.
 * initial_rom may be NULL. The selected settings are written to options.
 */
bool ppa_gui_run_launcher(const char *initial_rom, PPA1GuiLaunchOptions *options);

#endif
