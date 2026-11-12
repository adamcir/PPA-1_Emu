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

static void text(SDL_Renderer *r, float x, float y, const char *s,
                 Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, s);
}

static void text_scaled(SDL_Renderer *r, float x, float y, float scale,
                        const char *s, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderScale(r, scale, scale);
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x / scale, y / scale, s);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
}

static void panel_rect(SDL_Renderer *r, SDL_FRect rect,
                       Uint8 br, Uint8 bg, Uint8 bb,
                       Uint8 rr, Uint8 rg, Uint8 rb)
{
    SDL_SetRenderDrawColor(r, br, bg, bb, 255);
    SDL_RenderFillRect(r, &rect);
    SDL_SetRenderDrawColor(r, rr, rg, rb, 255);
    SDL_RenderRect(r, &rect);
}

static void draw_button(SDL_Renderer *r, const SDL_FRect *rect,
                        const char *label, bool enabled, bool active)
{
    if (!enabled) {
        SDL_SetRenderDrawColor(r, 38, 40, 42, 255);
    } else if (active) {
        SDL_SetRenderDrawColor(r, 74, 112, 72, 255);
    } else {
        SDL_SetRenderDrawColor(r, 61, 65, 69, 255);
    }
    SDL_RenderFillRect(r, rect);

    SDL_SetRenderDrawColor(r,
        enabled ? 205 : 92,
        enabled ? 205 : 92,
        enabled ? 195 : 92, 255);
    SDL_RenderRect(r, rect);

    text(r, rect->x + 10.0f, rect->y + (rect->h - 8.0f) / 2.0f,
         label,
         enabled ? 235 : 110,
         enabled ? 235 : 110,
         enabled ? 220 : 110);
}

static void draw_led(SDL_Renderer *r, float x, float y,
                     const char *label, bool on,
                     Uint8 on_r, Uint8 on_g, Uint8 on_b)
{
    SDL_FRect led = { x, y + 14.0f, 28.0f, 22.0f };

    text(r, x + 10.0f, y, label, 194, 194, 178);

    SDL_SetRenderDrawColor(r, 10, 10, 10, 255);
    SDL_RenderFillRect(r, &led);
    SDL_SetRenderDrawColor(r, 116, 116, 106, 255);
    SDL_RenderRect(r, &led);

    SDL_FRect inner = { led.x + 4.0f, led.y + 4.0f, led.w - 8.0f, led.h - 8.0f };
    if (on)
        SDL_SetRenderDrawColor(r, on_r, on_g, on_b, 255);
    else
        SDL_SetRenderDrawColor(r, on_r / 7, on_g / 7, on_b / 7, 255);
    SDL_RenderFillRect(r, &inner);
}

static void draw_switch(SDL_Renderer *r, float x, float y,
                        const char *label, bool on)
{
    text(r, x + 10.0f, y, label, 196, 196, 180);

    SDL_FRect body = { x + 5.0f, y + 16.0f, 30.0f, 54.0f };
    SDL_SetRenderDrawColor(r, 25, 25, 24, 255);
    SDL_RenderFillRect(r, &body);
    SDL_SetRenderDrawColor(r, 120, 120, 112, 255);
    SDL_RenderRect(r, &body);

    SDL_FRect stem = {
        x + 16.0f,
        on ? y + 23.0f : y + 46.0f,
        8.0f,
        19.0f
    };
    SDL_SetRenderDrawColor(r, 205, 205, 190, 255);
    SDL_RenderFillRect(r, &stem);

    SDL_FRect tip = {
        x + 10.0f,
        on ? y + 19.0f : y + 57.0f,
        20.0f,
        10.0f
    };
    SDL_SetRenderDrawColor(r, 95, 95, 88, 255);
    SDL_RenderFillRect(r, &tip);
}

static void draw_menu_item(SDL_Renderer *r, float x, const char *label, bool selected)
{
    if (selected) {
        SDL_FRect bg = { x - 6.0f, 4.0f, (float)strlen(label) * 8.0f + 12.0f, 22.0f };
        SDL_SetRenderDrawColor(r, 66, 71, 76, 255);
        SDL_RenderFillRect(r, &bg);
    }
    text(r, x, 10.0f, label, 224, 224, 214);
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

    if (initial_rom && *initial_rom)
        snprintf(options->rom_path, sizeof(options->rom_path), "%s", initial_rom);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        ppa_log_printf("GUI: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer(
            "PROGRAMBLE PROCESSOR A-1",
            1180, 760,
            SDL_WINDOW_RESIZABLE,
            &window, &renderer)) {
        ppa_log_printf("GUI: SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetRenderVSync(renderer, 1);

    FileDialogState file_state = {0};
    if (options->rom_path[0])
        snprintf(file_state.rom_path, sizeof(file_state.rom_path), "%s", options->rom_path);

    const SDL_DialogFileFilter filters[] = {
        { "PPA-1 EEPROM image", "bin" },
        { "All files", "*" }
    };

    bool running = true;
    bool start = false;
    bool tty_enabled = false;
    bool run_lamp = false;
    bool switches[16] = {0};

    const SDL_FRect open_btn  = { 30, 690, 112, 34 };
    const SDL_FRect pref_btn  = { 150, 690, 112, 34 };
    const SDL_FRect log_btn   = { 270, 690, 112, 34 };
    const SDL_FRect start_btn = { 930, 690, 190, 34 };

    const SDL_FRect run_btn   = { 40, 103, 80, 34 };
    const SDL_FRect stop_btn  = { 128, 103, 80, 34 };
    const SDL_FRect step_btn  = { 216, 103, 80, 34 };
    const SDL_FRect ustep_btn = { 304, 103, 96, 34 };
    const SDL_FRect reset_btn = { 408, 103, 80, 34 };
    const SDL_FRect examine_btn = { 496, 103, 100, 34 };
    const SDL_FRect deposit_btn = { 604, 103, 100, 34 };

    ppa_log_printf("GUI: PPA-1 front panel opened.\n");

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                       event.button.button == SDL_BUTTON_LEFT) {
                const float x = event.button.x;
                const float y = event.button.y;

                if (point_in_rect(x, y, &open_btn) && !file_state.dialog_open) {
                    file_state.dialog_open = true;
                    SDL_ShowOpenFileDialog(file_dialog_callback, &file_state, window,
                                           filters, 2, NULL, false);
                } else if (point_in_rect(x, y, &pref_btn)) {
                    tty_enabled = !tty_enabled;
                    ppa_log_printf("GUI: Preferences: PTY serial %s.\n",
                                   tty_enabled ? "enabled" : "disabled");
                } else if (point_in_rect(x, y, &log_btn)) {
                    ppa_log_printf("GUI: Log panel requested. Log file is latest.log.\n");
                } else if (point_in_rect(x, y, &start_btn) && file_state.rom_path[0]) {
                    snprintf(options->rom_path, sizeof(options->rom_path),
                             "%s", file_state.rom_path);
                    options->tty_enabled = tty_enabled;
                    start = true;
                    running = false;
                } else if (point_in_rect(x, y, &run_btn)) {
                    run_lamp = true;
                    ppa_log_printf("GUI: RUN pressed.\n");
                } else if (point_in_rect(x, y, &stop_btn)) {
                    run_lamp = false;
                    ppa_log_printf("GUI: STOP pressed.\n");
                } else if (point_in_rect(x, y, &step_btn)) {
                    ppa_log_printf("GUI: STEP pressed.\n");
                } else if (point_in_rect(x, y, &ustep_btn)) {
                    ppa_log_printf("GUI: MICROSTEP pressed.\n");
                } else if (point_in_rect(x, y, &reset_btn)) {
                    memset(switches, 0, sizeof(switches));
                    run_lamp = false;
                    ppa_log_printf("GUI: RESET pressed.\n");
                } else if (point_in_rect(x, y, &examine_btn)) {
                    ppa_log_printf("GUI: EXAMINE pressed.\n");
                } else if (point_in_rect(x, y, &deposit_btn)) {
                    ppa_log_printf("GUI: DEPOSIT pressed.\n");
                } else {
                    for (int i = 0; i < 16; i++) {
                        SDL_FRect sw = { 58.0f + i * 61.0f, 493.0f, 40.0f, 74.0f };
                        if (point_in_rect(x, y, &sw)) {
                            switches[i] = !switches[i];
                            ppa_log_printf("GUI: switch %X -> %d\n", i, switches[i] ? 1 : 0);
                            break;
                        }
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 19, 20, 20, 255);
        SDL_RenderClear(renderer);

        /* Top application menu. */
        SDL_FRect menu = { 0, 0, 1180, 32 };
        SDL_SetRenderDrawColor(renderer, 42, 44, 45, 255);
        SDL_RenderFillRect(renderer, &menu);
        draw_menu_item(renderer, 18, "Open", false);
        draw_menu_item(renderer, 78, "Preferences", false);
        draw_menu_item(renderer, 184, "View", false);
        draw_menu_item(renderer, 236, "Log", false);
        draw_menu_item(renderer, 280, "Help", false);

        /* Main metal front panel. */
        SDL_FRect front = { 20, 45, 1140, 620 };
        panel_rect(renderer, front, 49, 50, 47, 126, 126, 114);

        /* Branding. */
        text_scaled(renderer, 40, 58, 2.0f, "PROGRAMBLE PROCESSOR", 235, 234, 215);

        SDL_FRect badge = { 890, 54, 225, 68 };
        panel_rect(renderer, badge, 23, 23, 22, 190, 190, 174);
        text_scaled(renderer, 951, 64, 3.0f, "A-1", 244, 242, 218);

        /* Adava Electronics is deliberately large and constrained to badge width. */
        text_scaled(renderer, 895, 126, 1.45f, "ADAVA ELECTRONICS", 220, 219, 203);

        /* Machine control strip near the top. */
        draw_button(renderer, &run_btn, "RUN", true, run_lamp);
        draw_button(renderer, &stop_btn, "STOP", true, !run_lamp);
        draw_button(renderer, &step_btn, "STEP", true, false);
        draw_button(renderer, &ustep_btn, "uSTEP", true, false);
        draw_button(renderer, &reset_btn, "RESET", true, false);
        draw_button(renderer, &examine_btn, "EXAMINE", true, false);
        draw_button(renderer, &deposit_btn, "DEPOSIT", true, false);

        draw_led(renderer, 735, 101, "RUN", run_lamp, 40, 235, 80);
        draw_led(renderer, 783, 101, "HALT", !run_lamp, 235, 66, 48);

        /* Address LEDs: 16 square lamps, labelled 0-F. */
        text_scaled(renderer, 42, 164, 1.3f, "ADDRESS", 223, 221, 204);
        for (int i = 0; i < 16; i++) {
            char label[2] = { "0123456789ABCDEF"[i], 0 };
            draw_led(renderer,
                     58.0f + i * 61.0f,
                     191.0f,
                     label,
                     switches[i],
                     242, 186, 45);
        }

        /* Data LEDs: 8 square lamps, labelled 0-7. */
        text_scaled(renderer, 42, 258, 1.3f, "DATA", 223, 221, 204);
        for (int i = 0; i < 8; i++) {
            char label[2] = { (char)('0' + i), 0 };
            draw_led(renderer,
                     58.0f + i * 61.0f,
                     286.0f,
                     label,
                     false,
                     64, 215, 236);
        }

        /* Status block. */
        SDL_FRect status = { 575, 274, 530, 115 };
        panel_rect(renderer, status, 35, 36, 34, 102, 102, 94);
        text_scaled(renderer, 592, 287, 1.2f, "SYSTEM STATUS", 218, 217, 201);
        text(renderer, 592, 319, "ROM:", 188, 188, 174);
        text(renderer, 640, 319,
             file_state.rom_path[0] ? file_state.rom_path : "(no image loaded)",
             224, 224, 211);
        text(renderer, 592, 341, "LOG: latest.log", 188, 188, 174);
        text(renderer, 592, 361,
             tty_enabled ? "SERIAL: PTY enabled" : "SERIAL: internal / disabled",
             188, 188, 174);

        /* 16 address/manual-entry switches. */
        text_scaled(renderer, 42, 438, 1.3f, "ADDRESS SWITCH REGISTER", 223, 221, 204);
        for (int i = 0; i < 16; i++) {
            char label[2] = { "0123456789ABCDEF"[i], 0 };
            draw_switch(renderer, 58.0f + i * 61.0f, 472.0f, label, switches[i]);
        }

        /* Bottom utility strip. */
        SDL_FRect utility = { 20, 675, 1140, 65 };
        panel_rect(renderer, utility, 35, 36, 35, 96, 96, 88);
        draw_button(renderer, &open_btn, "OPEN...", true, false);
        draw_button(renderer, &pref_btn, "PREFERENCES", true, tty_enabled);
        draw_button(renderer, &log_btn, "LOG", true, false);
        draw_button(renderer, &start_btn, "LOAD / START", file_state.rom_path[0] != '\0', false);

        text(renderer, 405, 698,
             file_state.rom_path[0] ? "EEPROM IMAGE READY" : "SELECT EEPROM IMAGE",
             file_state.rom_path[0] ? 120 : 212,
             file_state.rom_path[0] ? 220 : 170,
             file_state.rom_path[0] ? 120 : 70);

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (start)
        ppa_log_printf("GUI: starting emulator with ROM: %s\n", options->rom_path);
    else
        ppa_log_printf("GUI: front panel closed without starting emulation.\n");

    return start;
}
