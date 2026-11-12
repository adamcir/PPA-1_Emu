#include "gui.h"
#include "logger.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char rom_path[1024];
    bool dialog_open;
} FileDialogState;

static bool point_in_rect(float x, float y, const SDL_FRect *r) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static void file_dialog_callback(void *userdata, const char * const *filelist, int filter) {
    (void)filter;
    FileDialogState *state = (FileDialogState *)userdata;
    state->dialog_open = false;

    if (!filelist || !filelist[0]) {
        ppa_log_printf("GUI: ROM selection cancelled.\n");
        return;
    }

    snprintf(state->rom_path, sizeof(state->rom_path), "%s", filelist[0]);
    ppa_log_printf("GUI: selected ROM: %s\n", state->rom_path);
}

static void draw_button(SDL_Renderer *renderer, const SDL_FRect *rect, const char *label, bool enabled) {
    if (enabled) SDL_SetRenderDrawColor(renderer, 48, 64, 82, 255);
    else SDL_SetRenderDrawColor(renderer, 36, 39, 44, 255);
    SDL_RenderFillRect(renderer, rect);

    SDL_SetRenderDrawColor(renderer, enabled ? 210 : 100, enabled ? 220 : 100, enabled ? 232 : 100, 255);
    SDL_RenderRect(renderer, rect);
    SDL_RenderDebugText(renderer, rect->x + 12.0f, rect->y + 10.0f, label);
}

void ppa_gui_init_state(PPA1GuiState *state, bool no_gui) {
    if (!state) return;
    state->requested = !no_gui;
    state->available = !no_gui;
}

void ppa_gui_log_mode(const PPA1GuiState *state) {
    if (!state) return;
    ppa_log_printf("Mode: %s\n", state->requested ? "GUI" : "--nogui (console)");
}

bool ppa_gui_run_launcher(const char *initial_rom, PPA1GuiLaunchOptions *options) {
    if (!options) return false;
    memset(options, 0, sizeof(*options));

    if (initial_rom && *initial_rom) {
        snprintf(options->rom_path, sizeof(options->rom_path), "%s", initial_rom);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        ppa_log_printf("GUI: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer("PPA-1 Emulator", 900, 620, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        ppa_log_printf("GUI: SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetRenderVSync(renderer, 1);

    FileDialogState file_state = {0};
    if (options->rom_path[0]) {
        snprintf(file_state.rom_path, sizeof(file_state.rom_path), "%s", options->rom_path);
    }

    const SDL_DialogFileFilter filters[] = {
        { "PPA-1 EEPROM image", "bin" },
        { "All files", "*" }
    };

    bool running = true;
    bool start = false;
    bool tty_enabled = false;

    SDL_FRect open_btn = { 40, 130, 180, 42 };
    SDL_FRect start_btn = { 40, 480, 180, 42 };
    SDL_FRect quit_btn = { 240, 480, 120, 42 };
    SDL_FRect tty_box = { 42, 300, 20, 20 };

    ppa_log_printf("GUI: launcher opened.\n");

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                const float x = event.button.x;
                const float y = event.button.y;

                if (point_in_rect(x, y, &open_btn) && !file_state.dialog_open) {
                    file_state.dialog_open = true;
                    SDL_ShowOpenFileDialog(
                        file_dialog_callback,
                        &file_state,
                        window,
                        filters,
                        2,
                        NULL,
                        false
                    );
                } else if (point_in_rect(x, y, &tty_box)) {
                    tty_enabled = !tty_enabled;
                    ppa_log_printf("GUI: PTY serial %s.\n", tty_enabled ? "enabled" : "disabled");
                } else if (point_in_rect(x, y, &start_btn) && file_state.rom_path[0]) {
                    snprintf(options->rom_path, sizeof(options->rom_path), "%s", file_state.rom_path);
                    options->tty_enabled = tty_enabled;
                    start = true;
                    running = false;
                } else if (point_in_rect(x, y, &quit_btn)) {
                    running = false;
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 18, 21, 26, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 235, 239, 245, 255);
        SDL_RenderDebugText(renderer, 40, 30, "PPA-1 Emulator");
        SDL_RenderDebugText(renderer, 40, 50, "Adava Software / Adava Development");
        SDL_RenderDebugText(renderer, 40, 82, "Launcher and debugger foundation");

        draw_button(renderer, &open_btn, "Open ROM...", true);

        SDL_SetRenderDrawColor(renderer, 180, 190, 202, 255);
        SDL_RenderDebugText(renderer, 40, 195, "ROM / EEPROM image:");
        SDL_RenderDebugText(renderer, 40, 215,
            file_state.rom_path[0] ? file_state.rom_path : "(no file selected)");

        SDL_RenderDebugText(renderer, 40, 260, "Settings");

        SDL_SetRenderDrawColor(renderer, 40, 44, 50, 255);
        SDL_RenderFillRect(renderer, &tty_box);
        SDL_SetRenderDrawColor(renderer, 200, 210, 220, 255);
        SDL_RenderRect(renderer, &tty_box);
        if (tty_enabled) {
            SDL_SetRenderDrawColor(renderer, 70, 220, 110, 255);
            SDL_FRect mark = { tty_box.x + 4, tty_box.y + 4, tty_box.w - 8, tty_box.h - 8 };
            SDL_RenderFillRect(renderer, &mark);
        }

        SDL_SetRenderDrawColor(renderer, 200, 210, 220, 255);
        SDL_RenderDebugText(renderer, 75, 304, "Create PTY serial terminal");

        SDL_RenderDebugText(renderer, 40, 345, "Logging: ENABLED");
        SDL_RenderDebugText(renderer, 40, 365, "Log file: latest.log");
        SDL_RenderDebugText(renderer, 40, 405, "More panels: CPU, memory, stack, bus and serial monitor will be added here.");

        draw_button(renderer, &start_btn, "Start Emulator", file_state.rom_path[0] != '\0');
        draw_button(renderer, &quit_btn, "Quit", true);

        SDL_SetRenderDrawColor(renderer, 120, 130, 142, 255);
        SDL_RenderDebugText(renderer, 40, 565, "CLI mode: PPA-1_Emu --nogui rom.bin [options]");

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (start) {
        ppa_log_printf("GUI: starting emulator with ROM: %s\n", options->rom_path);
    } else {
        ppa_log_printf("GUI: launcher closed without starting emulation.\n");
    }

    return start;
}
