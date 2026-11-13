#ifndef PPA1_GUI_H
#define PPA1_GUI_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    bool requested;
    bool available;
} PPA1GuiState;

typedef struct {
    uint8_t A, B, C, D;
    uint16_t X, Y, BR;
    uint8_t IMM, DST, STEP, AOS, AAR, ABR, RS, IR, SR, SP;
    uint16_t AR, PC;

    uint16_t bus_address;
    uint16_t bus_bank;
    uint8_t bus_data;
    uint8_t bus_operation;

    bool halted;
    bool error;
} PPA1CpuSnapshot;

typedef struct {
    void *ctx;

    bool (*load_rom)(void *ctx, const char *path);
    void (*reset_emulation)(void *ctx);
    bool (*export_image)(void *ctx, const char *path);
    bool (*is_image_attached)(void *ctx);
    void (*set_power)(void *ctx, bool on);
    void (*set_running)(void *ctx, bool running);
    void (*reset)(void *ctx);
    void (*step_instruction)(void *ctx);
    void (*step_micro)(void *ctx);
    void (*tick)(void *ctx, int clock_edges);
    void (*tick_turbo)(void *ctx, int microstep_budget);

    void (*set_data_input)(void *ctx, uint8_t value);
    void (*load_bank)(void *ctx, uint16_t bank);
    void (*load_address)(void *ctx, uint16_t address);
    uint8_t (*examine)(void *ctx, uint16_t address);
    void (*deposit)(void *ctx, uint16_t address, uint8_t value);
    void (*start_at)(void *ctx, uint16_t address);

    bool (*set_tty_enabled)(void *ctx, bool enabled);
    bool (*is_tty_enabled)(void *ctx);
    const char *(*tty_name)(void *ctx);

    bool (*set_terminal_enabled)(void *ctx, bool enabled);
    bool (*is_terminal_enabled)(void *ctx);
    size_t (*terminal_read_tx)(void *ctx, uint8_t *dst, size_t capacity);
    bool (*terminal_write_rx)(void *ctx, uint8_t value);

    bool (*is_loaded)(void *ctx);
    bool (*is_powered)(void *ctx);
    bool (*is_running)(void *ctx);
    bool (*is_halted)(void *ctx);
    bool (*is_error)(void *ctx);

    uint16_t (*address_bus)(void *ctx);
    uint16_t (*bank_bus)(void *ctx);
    uint8_t (*data_bus)(void *ctx);
    uint8_t (*status_register)(void *ctx);
    uint8_t (*register_value)(void *ctx, int selector);

    void (*cpu_snapshot)(void *ctx, PPA1CpuSnapshot *out);
    uint8_t (*memory_inspect)(void *ctx, uint16_t bank, uint16_t address);
    bool (*memory_edit)(void *ctx, uint16_t bank, uint16_t address, uint8_t value);
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
