#include "gui.h"
#include "logger.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char rom_path[1024];
    bool dialog_open;
} FileDialogState;

typedef struct {
    TTF_Font *title;
    TTF_Font *brand;
    TTF_Font *section;
    TTF_Font *label;
    TTF_Font *small;
} PanelFonts;

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

static const char *find_regular_font(void) {
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (SDL_GetPathInfo(paths[i], NULL)) return paths[i];
    }
    return NULL;
}

static const char *find_bold_font(void) {
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (SDL_GetPathInfo(paths[i], NULL)) return paths[i];
    }
    return NULL;
}

static bool load_fonts(PanelFonts *f) {
    const char *regular = find_regular_font();
    const char *bold = find_bold_font();

    if (!regular || !bold) {
        ppa_log_printf("GUI: no suitable technical TrueType font found.\n");
        return false;
    }

    f->title   = TTF_OpenFont(regular, 40.0f);
    f->brand   = TTF_OpenFont(bold, 21.0f);
    f->section = TTF_OpenFont(bold, 18.0f);
    f->label   = TTF_OpenFont(bold, 13.5f);
    f->small   = TTF_OpenFont(regular, 11.5f);

    if (!f->title || !f->brand || !f->section || !f->label || !f->small) {
        ppa_log_printf("GUI: TTF_OpenFont failed: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

static void close_fonts(PanelFonts *f) {
    if (f->title)   TTF_CloseFont(f->title);
    if (f->brand)   TTF_CloseFont(f->brand);
    if (f->section) TTF_CloseFont(f->section);
    if (f->label)   TTF_CloseFont(f->label);
    if (f->small)   TTF_CloseFont(f->small);
    memset(f, 0, sizeof(*f));
}

static void draw_text(SDL_Renderer *r, TTF_Font *font,
                      float x, float y, SDL_Color color,
                      const char *text)
{
    if (!font || !text || !*text) return;

    SDL_Surface *surface = TTF_RenderText_Blended(font, text, 0, color);
    if (!surface) return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(r, surface);
    if (!texture) {
        SDL_DestroySurface(surface);
        return;
    }

    SDL_FRect dst = {
        x, y,
        (float)surface->w,
        (float)surface->h
    };

    SDL_RenderTexture(r, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

static void draw_text_center(SDL_Renderer *r, TTF_Font *font,
                             SDL_FRect rect, SDL_Color color,
                             const char *text)
{
    int w = 0, h = 0;
    if (!TTF_GetStringSize(font, text, 0, &w, &h)) return;
    draw_text(r, font,
              rect.x + (rect.w - (float)w) * 0.5f,
              rect.y + (rect.h - (float)h) * 0.5f,
              color, text);
}

static void fill_circle(SDL_Renderer *r, float cx, float cy, float radius) {
    for (int y = -(int)radius; y <= (int)radius; y++) {
        float dx = sqrtf(radius * radius - (float)(y * y));
        SDL_RenderLine(r, cx - dx, cy + (float)y, cx + dx, cy + (float)y);
    }
}

static void draw_bevel_panel(SDL_Renderer *r, SDL_FRect rect,
                             SDL_Color fill, SDL_Color edge_hi, SDL_Color edge_lo)
{
    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, 255);
    SDL_RenderFillRect(r, &rect);

    SDL_SetRenderDrawColor(r, edge_hi.r, edge_hi.g, edge_hi.b, 255);
    SDL_RenderLine(r, rect.x, rect.y, rect.x + rect.w, rect.y);
    SDL_RenderLine(r, rect.x, rect.y, rect.x, rect.y + rect.h);

    SDL_SetRenderDrawColor(r, edge_lo.r, edge_lo.g, edge_lo.b, 255);
    SDL_RenderLine(r, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h);
    SDL_RenderLine(r, rect.x + rect.w, rect.y, rect.x + rect.w, rect.y + rect.h);
}

static void draw_screw(SDL_Renderer *r, float x, float y) {
    SDL_SetRenderDrawColor(r, 164, 166, 160, 255);
    fill_circle(r, x, y, 5.5f);
    SDL_SetRenderDrawColor(r, 47, 48, 47, 255);
    SDL_RenderLine(r, x - 3.5f, y, x + 3.5f, y);
}

static void draw_section(SDL_Renderer *r, PanelFonts *fonts,
                         SDL_FRect rect, const char *title)
{
    draw_bevel_panel(r, rect,
        (SDL_Color){42, 44, 43, 255},
        (SDL_Color){132, 134, 127, 255},
        (SDL_Color){19, 20, 20, 255});

    draw_screw(r, rect.x + 10, rect.y + 10);
    draw_screw(r, rect.x + rect.w - 10, rect.y + 10);
    draw_screw(r, rect.x + 10, rect.y + rect.h - 10);
    draw_screw(r, rect.x + rect.w - 10, rect.y + rect.h - 10);

    draw_text(r, fonts->section, rect.x + 22, rect.y + 10,
              (SDL_Color){226, 220, 196, 255}, title);

    SDL_SetRenderDrawColor(r, 177, 170, 145, 255);
    SDL_RenderLine(r, rect.x + 20, rect.y + 39,
                   rect.x + rect.w - 20, rect.y + 39);
}

static void draw_led(SDL_Renderer *r, PanelFonts *fonts,
                     float x, float y, const char *label,
                     bool on, SDL_Color lamp)
{
    SDL_FRect bezel = {x, y + 22, 28, 26};
    SDL_FRect lens  = {x + 4, y + 26, 20, 18};

    draw_text_center(r, fonts->small,
        (SDL_FRect){x - 2, y, 32, 18},
        (SDL_Color){222, 219, 201, 255}, label);

    SDL_SetRenderDrawColor(r, 12, 13, 13, 255);
    SDL_RenderFillRect(r, &bezel);
    SDL_SetRenderDrawColor(r, 112, 113, 106, 255);
    SDL_RenderRect(r, &bezel);

    if (on) {
        SDL_SetRenderDrawColor(r, lamp.r, lamp.g, lamp.b, 255);
        SDL_RenderFillRect(r, &lens);

        SDL_SetRenderDrawColor(r,
            (Uint8)SDL_min(255, lamp.r + 45),
            (Uint8)SDL_min(255, lamp.g + 45),
            (Uint8)SDL_min(255, lamp.b + 45), 255);
        SDL_FRect glow = {lens.x + 5, lens.y + 4, 8, 6};
        SDL_RenderFillRect(r, &glow);
    } else {
        SDL_SetRenderDrawColor(r,
            (Uint8)(lamp.r / 7),
            (Uint8)(lamp.g / 7),
            (Uint8)(lamp.b / 7), 255);
        SDL_RenderFillRect(r, &lens);
    }
}

static void draw_toggle(SDL_Renderer *r, PanelFonts *fonts,
                        float x, float y, const char *label, bool on)
{
    draw_text_center(r, fonts->small,
        (SDL_FRect){x - 2, y, 34, 18},
        (SDL_Color){224, 221, 205, 255}, label);

    SDL_SetRenderDrawColor(r, 128, 129, 123, 255);
    fill_circle(r, x + 15, y + 44, 9.5f);
    SDL_SetRenderDrawColor(r, 22, 23, 23, 255);
    fill_circle(r, x + 15, y + 44, 5.5f);

    float tip_y = on ? y + 27 : y + 61;
    SDL_SetRenderDrawColor(r, 196, 198, 192, 255);
    SDL_RenderLine(r, x + 15, y + 44, x + 15, tip_y);
    SDL_RenderLine(r, x + 14, y + 44, x + 14, tip_y);

    SDL_SetRenderDrawColor(r, 218, 219, 211, 255);
    fill_circle(r, x + 14.5f, tip_y, 4.0f);
}

static void draw_push_button(SDL_Renderer *r, PanelFonts *fonts,
                             SDL_FRect rect, const char *label,
                             SDL_Color face, bool active)
{
    draw_bevel_panel(r, rect,
        active
            ? (SDL_Color){(Uint8)SDL_min(255, face.r + 25),
                          (Uint8)SDL_min(255, face.g + 25),
                          (Uint8)SDL_min(255, face.b + 25), 255}
            : face,
        (SDL_Color){210, 210, 198, 255},
        (SDL_Color){18, 18, 18, 255});

    draw_text_center(r, fonts->label, rect,
                     (SDL_Color){244, 242, 225, 255}, label);
}

static void draw_knob(SDL_Renderer *r, PanelFonts *fonts,
                      float cx, float cy, float radius,
                      const char *label)
{
    SDL_SetRenderDrawColor(r, 16, 17, 17, 255);
    fill_circle(r, cx, cy, radius + 5);

    SDL_SetRenderDrawColor(r, 103, 104, 100, 255);
    fill_circle(r, cx, cy, radius);

    SDL_SetRenderDrawColor(r, 51, 52, 51, 255);
    fill_circle(r, cx, cy, radius - 7);

    SDL_SetRenderDrawColor(r, 218, 215, 200, 255);
    SDL_RenderLine(r, cx, cy, cx + radius * 0.55f, cy - radius * 0.55f);

    draw_text_center(r, fonts->small,
        (SDL_FRect){cx - 65, cy + radius + 9, 130, 18},
        (SDL_Color){219, 216, 199, 255}, label);
}

static void draw_menu_button(SDL_Renderer *r, PanelFonts *fonts,
                             SDL_FRect rect, const char *label)
{
    SDL_SetRenderDrawColor(r, 47, 49, 48, 255);
    SDL_RenderFillRect(r, &rect);
    draw_text_center(r, fonts->small, rect,
                     (SDL_Color){224, 222, 207, 255}, label);
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

    if (!TTF_Init()) {
        ppa_log_printf("GUI: TTF_Init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    PanelFonts fonts = {0};
    if (!load_fonts(&fonts)) {
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer(
            "PROGRAMBLE PROCESSOR A-1",
            1280, 820,
            SDL_WINDOW_RESIZABLE,
            &window, &renderer)) {
        ppa_log_printf("GUI: SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        close_fonts(&fonts);
        TTF_Quit();
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
    bool run_state = false;
    bool switches[16] = {0};

    SDL_FRect menu_open = {10, 4, 72, 29};
    SDL_FRect menu_pref = {86, 4, 112, 29};
    SDL_FRect menu_view = {202, 4, 72, 29};
    SDL_FRect menu_log  = {278, 4, 64, 29};
    SDL_FRect menu_help = {346, 4, 72, 29};

    SDL_FRect run_btn   = {662, 221, 88, 42};
    SDL_FRect stop_btn  = {760, 221, 88, 42};
    SDL_FRect reset_btn = {858, 221, 88, 42};

    SDL_FRect op_run    = {646, 684, 76, 42};
    SDL_FRect op_stop   = {730, 684, 76, 42};
    SDL_FRect op_step   = {814, 684, 76, 42};
    SDL_FRect op_ustep  = {898, 684, 88, 42};
    SDL_FRect op_exam   = {994, 684, 90, 42};
    SDL_FRect op_dep    = {1092, 684, 90, 42};

    SDL_FRect load_btn  = {1055, 748, 118, 40};
    SDL_FRect power_btn = {1194, 683, 46, 95};

    ppa_log_printf("GUI: hardware front panel opened.\n");

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                       event.button.button == SDL_BUTTON_LEFT) {
                float x = event.button.x;
                float y = event.button.y;

                if (point_in_rect(x, y, &menu_open) && !file_state.dialog_open) {
                    file_state.dialog_open = true;
                    SDL_ShowOpenFileDialog(file_dialog_callback, &file_state, window,
                                           filters, 2, NULL, false);
                } else if (point_in_rect(x, y, &menu_pref)) {
                    tty_enabled = !tty_enabled;
                    ppa_log_printf("GUI: Preferences: PTY serial %s.\n",
                                   tty_enabled ? "enabled" : "disabled");
                } else if (point_in_rect(x, y, &menu_log)) {
                    ppa_log_printf("GUI: log requested: latest.log\n");
                } else if (point_in_rect(x, y, &run_btn) ||
                           point_in_rect(x, y, &op_run)) {
                    run_state = true;
                    ppa_log_printf("GUI: RUN pressed.\n");
                } else if (point_in_rect(x, y, &stop_btn) ||
                           point_in_rect(x, y, &op_stop)) {
                    run_state = false;
                    ppa_log_printf("GUI: STOP pressed.\n");
                } else if (point_in_rect(x, y, &reset_btn)) {
                    memset(switches, 0, sizeof(switches));
                    run_state = false;
                    ppa_log_printf("GUI: RESET pressed.\n");
                } else if (point_in_rect(x, y, &op_step)) {
                    ppa_log_printf("GUI: STEP pressed.\n");
                } else if (point_in_rect(x, y, &op_ustep)) {
                    ppa_log_printf("GUI: MICROSTEP pressed.\n");
                } else if (point_in_rect(x, y, &op_exam)) {
                    ppa_log_printf("GUI: EXAMINE pressed.\n");
                } else if (point_in_rect(x, y, &op_dep)) {
                    ppa_log_printf("GUI: DEPOSIT pressed.\n");
                } else if (point_in_rect(x, y, &power_btn)) {
                    running = false;
                } else if (point_in_rect(x, y, &load_btn) && file_state.rom_path[0]) {
                    snprintf(options->rom_path, sizeof(options->rom_path),
                             "%s", file_state.rom_path);
                    options->tty_enabled = tty_enabled;
                    start = true;
                    running = false;
                } else {
                    for (int i = 0; i < 16; i++) {
                        SDL_FRect hit = {54.0f + i * 32.0f, 629.0f, 26.0f, 64.0f};
                        if (point_in_rect(x, y, &hit)) {
                            switches[i] = !switches[i];
                            ppa_log_printf("GUI: input switch %X -> %d\n",
                                           i, switches[i] ? 1 : 0);
                            break;
                        }
                    }
                }
            }
        }

        /* Main background: warm charcoal metal. */
        SDL_SetRenderDrawColor(renderer, 24, 26, 26, 255);
        SDL_RenderClear(renderer);

        /* Application menu bar. */
        SDL_FRect menu = {0, 0, 1280, 38};
        SDL_SetRenderDrawColor(renderer, 34, 36, 36, 255);
        SDL_RenderFillRect(renderer, &menu);
        draw_menu_button(renderer, &fonts, menu_open, "OPEN");
        draw_menu_button(renderer, &fonts, menu_pref, "PREFERENCES");
        draw_menu_button(renderer, &fonts, menu_view, "VIEW");
        draw_menu_button(renderer, &fonts, menu_log, "LOG");
        draw_menu_button(renderer, &fonts, menu_help, "HELP");

        /* Header block. */
        draw_text(renderer, fonts.title, 42, 51,
                  (SDL_Color){236, 229, 204, 255},
                  "PROGRAMBLE PROCESSOR");

        SDL_SetRenderDrawColor(renderer, 202, 190, 151, 255);
        SDL_RenderLine(renderer, 42, 108, 1030, 108);

        SDL_FRect badge = {1050, 48, 180, 68};
        draw_bevel_panel(renderer, badge,
            (SDL_Color){92, 38, 30, 255},
            (SDL_Color){218, 202, 160, 255},
            (SDL_Color){35, 18, 17, 255});
        draw_text_center(renderer, fonts.title, badge,
                         (SDL_Color){245, 232, 195, 255}, "A-1");

        draw_text_center(renderer, fonts.brand,
            (SDL_FRect){1040, 118, 200, 30},
            (SDL_Color){225, 218, 195, 255},
            "Adava Electronics");

        /* Layout from the user's sketch. */
        SDL_FRect lights  = {30, 155, 565, 265};
        SDL_FRect ctrl    = {620, 155, 625, 175};
        SDL_FRect output  = {620, 345, 625, 190};
        SDL_FRect input   = {30, 445, 565, 300};
        SDL_FRect ops     = {620, 555, 565, 190};
        SDL_FRect pwr     = {1194, 555, 51, 190};

        draw_section(renderer, &fonts, lights, "PANEL SVĚTÝLEK");
        draw_section(renderer, &fonts, ctrl, "DŮLEŽITÉ KONTROLKY");
        draw_section(renderer, &fonts, output, "NASTAVENÍ RYCHLOSTI A \"DATA\" VÝSTUPU");
        draw_section(renderer, &fonts, input, "ZÁKLADNÍ VSTUP");
        draw_section(renderer, &fonts, ops, "NASTAVOVÁNÍ A OPERACE");
        draw_section(renderer, &fonts, pwr, "PWR");

        /* ADDRESS LEDs. */
        draw_text(renderer, fonts.label, 52, 206,
                  (SDL_Color){214, 209, 188, 255}, "ADDRESS  A0-A15");
        for (int i = 0; i < 16; i++) {
            char lab[2] = {"0123456789ABCDEF"[i], 0};
            draw_led(renderer, &fonts,
                     54.0f + i * 32.0f, 235.0f, lab,
                     switches[i],
                     (SDL_Color){236, 155, 38, 255});
        }

        /* DATA LEDs. */
        draw_text(renderer, fonts.label, 52, 310,
                  (SDL_Color){214, 209, 188, 255}, "DATA  D0-D7");
        for (int i = 0; i < 8; i++) {
            char lab[2] = {(char)('0' + i), 0};
            draw_led(renderer, &fonts,
                     54.0f + i * 48.0f, 337.0f, lab,
                     false,
                     (SDL_Color){40, 216, 194, 255});
        }

        /* Small status group in lamp panel. */
        draw_text(renderer, fonts.label, 435, 310,
                  (SDL_Color){214, 209, 188, 255}, "STATUS");
        draw_led(renderer, &fonts, 435, 337, "R", !run_state,
                 (SDL_Color){79, 220, 94, 255});
        draw_led(renderer, &fonts, 475, 337, "B", run_state,
                 (SDL_Color){236, 155, 38, 255});
        draw_led(renderer, &fonts, 515, 337, "I", false,
                 (SDL_Color){219, 58, 45, 255});

        /* Important indicators and controls. */
        draw_led(renderer, &fonts, 650, 198, "RUN", run_state,
                 (SDL_Color){69, 224, 88, 255});
        draw_led(renderer, &fonts, 718, 198, "HALT", !run_state,
                 (SDL_Color){220, 55, 42, 255});
        draw_led(renderer, &fonts, 786, 198, "ERR", false,
                 (SDL_Color){220, 55, 42, 255});
        draw_led(renderer, &fonts, 854, 198, "CLK", true,
                 (SDL_Color){233, 163, 46, 255});
        draw_led(renderer, &fonts, 922, 198, "MEM", true,
                 (SDL_Color){69, 224, 88, 255});
        draw_led(renderer, &fonts, 990, 198, "I/O", false,
                 (SDL_Color){44, 202, 188, 255});

        draw_push_button(renderer, &fonts, run_btn, "RUN",
                         (SDL_Color){42, 115, 57, 255}, run_state);
        draw_push_button(renderer, &fonts, stop_btn, "HALT",
                         (SDL_Color){140, 45, 38, 255}, !run_state);
        draw_push_button(renderer, &fonts, reset_btn, "RESET",
                         (SDL_Color){105, 102, 91, 255}, false);

        draw_toggle(renderer, &fonts, 982, 212, "INT", true);
        draw_toggle(renderer, &fonts, 1042, 212, "CLK", false);

        /* Speed / data output panel. */
        draw_text(renderer, fonts.label, 650, 398,
                  (SDL_Color){214, 209, 188, 255}, "RYCHLOST / CLOCK");
        draw_knob(renderer, &fonts, 735, 457, 34, "100 Hz  -  100 kHz");

        draw_text(renderer, fonts.label, 825, 398,
                  (SDL_Color){214, 209, 188, 255}, "DATA VÝSTUP");
        draw_knob(renderer, &fonts, 900, 457, 30, "PAR / TTL / RS-232");

        draw_text(renderer, fonts.label, 1030, 398,
                  (SDL_Color){214, 209, 188, 255}, "FORMÁT");
        draw_toggle(renderer, &fonts, 1083, 428, "BIN", true);
        draw_text(renderer, fonts.small, 1120, 444,
                  (SDL_Color){212, 207, 187, 255}, "HEX");
        draw_text(renderer, fonts.small, 1120, 468,
                  (SDL_Color){212, 207, 187, 255}, "ASCII");

        /* Manual input switches. */
        draw_text(renderer, fonts.label, 52, 496,
                  (SDL_Color){214, 209, 188, 255},
                  "MANUÁLNÍ VSTUP  A0-A15");
        for (int i = 0; i < 16; i++) {
            char lab[2] = {"0123456789ABCDEF"[i], 0};
            draw_toggle(renderer, &fonts,
                        52.0f + i * 32.0f, 576.0f, lab, switches[i]);
        }

        draw_text(renderer, fonts.small, 54, 704,
                  (SDL_Color){188, 184, 167, 255}, "LOW (0)");
        draw_text(renderer, fonts.small, 510, 704,
                  (SDL_Color){188, 184, 167, 255}, "HIGH (1)");

        /* Operations. */
        draw_push_button(renderer, &fonts, op_run, "RUN",
                         (SDL_Color){42, 115, 57, 255}, run_state);
        draw_push_button(renderer, &fonts, op_stop, "STOP",
                         (SDL_Color){140, 45, 38, 255}, !run_state);
        draw_push_button(renderer, &fonts, op_step, "STEP",
                         (SDL_Color){100, 98, 88, 255}, false);
        draw_push_button(renderer, &fonts, op_ustep, "uSTEP",
                         (SDL_Color){100, 98, 88, 255}, false);
        draw_push_button(renderer, &fonts, op_exam, "EXAMINE",
                         (SDL_Color){100, 98, 88, 255}, false);
        draw_push_button(renderer, &fonts, op_dep, "DEPOSIT",
                         (SDL_Color){100, 98, 88, 255}, false);

        draw_text(renderer, fonts.small, 648, 646,
                  (SDL_Color){187, 184, 168, 255},
                  "CPU CONTROL");
        draw_text(renderer, fonts.small, 995, 646,
                  (SDL_Color){187, 184, 168, 255},
                  file_state.rom_path[0] ? "EEPROM READY" : "NO EEPROM IMAGE");

        draw_push_button(renderer, &fonts, load_btn,
                         file_state.rom_path[0] ? "LOAD / START" : "OPEN ROM",
                         file_state.rom_path[0]
                             ? (SDL_Color){182, 121, 31, 255}
                             : (SDL_Color){87, 87, 80, 255},
                         false);

        if (!file_state.rom_path[0]) {
            /* allow the large load button to act as Open too */
            if (SDL_GetMouseFocus() == window) {
                float mx, my;
                Uint32 buttons = SDL_GetMouseState(&mx, &my);
                (void)buttons;
            }
        }

        /* Power rocker. */
        SDL_SetRenderDrawColor(renderer, 24, 24, 23, 255);
        SDL_RenderFillRect(renderer, &power_btn);
        SDL_SetRenderDrawColor(renderer, 153, 73, 45, 255);
        SDL_RenderRect(renderer, &power_btn);
        SDL_FRect rocker = {1203, 707, 28, 50};
        SDL_SetRenderDrawColor(renderer, 120, 32, 27, 255);
        SDL_RenderFillRect(renderer, &rocker);
        draw_text_center(renderer, fonts.label, rocker,
                         (SDL_Color){245, 232, 205, 255}, "I");

        /* Bottom status line. */
        SDL_SetRenderDrawColor(renderer, 50, 52, 51, 255);
        SDL_FRect statusbar = {30, 764, 1005, 34};
        SDL_RenderFillRect(renderer, &statusbar);
        draw_text(renderer, fonts.small, 42, 773,
                  (SDL_Color){198, 196, 180, 255},
                  file_state.rom_path[0]
                      ? file_state.rom_path
                      : "Open EEPROM image from OPEN menu");
        draw_text(renderer, fonts.small, 835, 773,
                  (SDL_Color){145, 203, 157, 255},
                  "LOG: latest.log");

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    close_fonts(&fonts);
    TTF_Quit();
    SDL_Quit();

    if (start) {
        ppa_log_printf("GUI: starting emulator with ROM: %s\n", options->rom_path);
    } else {
        ppa_log_printf("GUI: front panel closed without starting emulation.\n");
    }

    return start;
}
