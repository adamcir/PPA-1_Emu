#ifndef PPA1_GUI_H
#define PPA1_GUI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool requested;
    bool available;
} PPA1GuiState;

typedef struct {
    void *ctx;

    bool (*load_rom)(void *ctx, const char *path);
    void (*set_power)(void *ctx, bool on);
    void (*set_running)(void *ctx, bool running);
    void (*reset)(void *ctx);
    void (*step_instruction)(void *ctx);
    void (*step_micro)(void *ctx);
    void (*tick)(void *ctx, int instruction_budget);

    void (*set_data_input)(void *ctx, uint8_t value);
    void (*load_address)(void *ctx, uint16_t address);
    uint8_t (*examine)(void *ctx, uint16_t address);
    void (*deposit)(void *ctx, uint16_t address, uint8_t value);
    void (*start_at)(void *ctx, uint16_t address);

    bool (*set_tty_enabled)(void *ctx, bool enabled);
    bool (*is_tty_enabled)(void *ctx);
    const char *(*tty_name)(void *ctx);

    bool (*is_loaded)(void *ctx);
    bool (*is_powered)(void *ctx);
    bool (*is_running)(void *ctx);
    bool (*is_halted)(void *ctx);

    uint16_t (*address_bus)(void *ctx);
    uint8_t (*data_bus)(void *ctx);
} PPA1GuiBackend;

void ppa_gui_init_state(PPA1GuiState *state, bool no_gui);
void ppa_gui_log_mode(const PPA1GuiState *state);

/*
 * Persistent graphical front panel. The panel owns the SDL event loop while
 * the backend callbacks operate the emulator core.
 */
int ppa_gui_run_panel(const char *initial_rom,
                      bool initial_tty_enabled,
                      const PPA1GuiBackend *backend);

#endif
