#include "gui.h"
#include "logger.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_W 1280
#define PANEL_H 820

typedef struct {
    char rom_path[1024];
    bool dialog_open;
} FileDialogState;

typedef struct {
    TTF_Font *title;
    TTF_Font *badge;
    TTF_Font *section;
    TTF_Font *label;
    TTF_Font *small;
} PanelFonts;

typedef enum {
    AUX_NONE = 0,
    AUX_PREFERENCES,
    AUX_VIEW,
    AUX_LOG,
    AUX_HELP
} AuxKind;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    AuxKind kind;
} AuxWindow;

typedef struct {
    bool auto_run;
    bool lamp_test;
    bool high_contrast;
    bool tty_requested;
} Preferences;

static bool point_in_rect(float x, float y, const SDL_FRect *r) {
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static bool point_in_circle(float x, float y, float cx, float cy, float radius) {
    float dx = x - cx;
    float dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
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

    f->title   = TTF_OpenFont(regular, 39.0f);
    f->badge   = TTF_OpenFont(bold, 34.0f);
    f->section = TTF_OpenFont(bold, 17.0f);
    f->label   = TTF_OpenFont(bold, 13.5f);
    f->small   = TTF_OpenFont(regular, 11.5f);

    return f->title && f->badge && f->section && f->label && f->small;
}

static void close_fonts(PanelFonts *f) {
    if (f->title) TTF_CloseFont(f->title);
    if (f->badge) TTF_CloseFont(f->badge);
    if (f->section) TTF_CloseFont(f->section);
    if (f->label) TTF_CloseFont(f->label);
    if (f->small) TTF_CloseFont(f->small);
    memset(f, 0, sizeof(*f));
}

static SDL_Texture *make_text_texture(SDL_Renderer *r, TTF_Font *font,
                                      SDL_Color color, const char *text,
                                      int *w, int *h)
{
    if (!font || !text || !*text) return NULL;

    SDL_Surface *surface = TTF_RenderText_Blended(font, text, 0, color);
    if (!surface) return NULL;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(r, surface);
    if (w) *w = surface->w;
    if (h) *h = surface->h;
    SDL_DestroySurface(surface);
    return texture;
}

static void draw_text(SDL_Renderer *r, TTF_Font *font,
                      float x, float y, SDL_Color color,
                      const char *text)
{
    int w = 0, h = 0;
    SDL_Texture *texture = make_text_texture(r, font, color, text, &w, &h);
    if (!texture) return;

    SDL_FRect dst = {x, y, (float)w, (float)h};
    SDL_RenderTexture(r, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static void draw_text_center(SDL_Renderer *r, TTF_Font *font,
                             SDL_FRect rect, SDL_Color color,
                             const char *text)
{
    int w = 0, h = 0;
    SDL_Texture *texture = make_text_texture(r, font, color, text, &w, &h);
    if (!texture) return;

    SDL_FRect dst = {
        rect.x + (rect.w - (float)w) * 0.5f,
        rect.y + (rect.h - (float)h) * 0.5f,
        (float)w, (float)h
    };
    SDL_RenderTexture(r, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static void draw_text_fit(SDL_Renderer *r, TTF_Font *font,
                          SDL_FRect rect, SDL_Color color,
                          const char *text)
{
    int w = 0, h = 0;
    SDL_Texture *texture = make_text_texture(r, font, color, text, &w, &h);
    if (!texture || w <= 0 || h <= 0) {
        if (texture) SDL_DestroyTexture(texture);
        return;
    }

    SDL_FRect dst = rect;
    SDL_RenderTexture(r, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static void fill_circle(SDL_Renderer *r, float cx, float cy, float radius) {
    for (int y = -(int)radius; y <= (int)radius; y++) {
        float inside = radius * radius - (float)(y * y);
        float dx = inside > 0.0f ? sqrtf(inside) : 0.0f;
        SDL_RenderLine(r, cx - dx, cy + (float)y, cx + dx, cy + (float)y);
    }
}

static void draw_bevel_panel(SDL_Renderer *r, SDL_FRect rect,
                             SDL_Color fill, SDL_Color hi, SDL_Color lo)
{
    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, 255);
    SDL_RenderFillRect(r, &rect);

    SDL_SetRenderDrawColor(r, hi.r, hi.g, hi.b, 255);
    SDL_RenderLine(r, rect.x, rect.y, rect.x + rect.w, rect.y);
    SDL_RenderLine(r, rect.x, rect.y, rect.x, rect.y + rect.h);

    SDL_SetRenderDrawColor(r, lo.r, lo.g, lo.b, 255);
    SDL_RenderLine(r, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h);
    SDL_RenderLine(r, rect.x + rect.w, rect.y, rect.x + rect.w, rect.y + rect.h);
}

static void draw_screw(SDL_Renderer *r, float x, float y) {
    SDL_SetRenderDrawColor(r, 164, 165, 157, 255);
    fill_circle(r, x, y, 5.0f);
    SDL_SetRenderDrawColor(r, 52, 53, 50, 255);
    SDL_RenderLine(r, x - 3.0f, y, x + 3.0f, y);
}

static void draw_section(SDL_Renderer *r, PanelFonts *fonts,
                         SDL_FRect rect, const char *title,
                         bool high_contrast)
{
    SDL_Color fill = high_contrast
        ? (SDL_Color){45, 47, 46, 255}
        : (SDL_Color){39, 41, 40, 255};

    draw_bevel_panel(r, rect, fill,
        (SDL_Color){129, 130, 121, 255},
        (SDL_Color){14, 15, 15, 255});

    draw_screw(r, rect.x + 10, rect.y + 10);
    draw_screw(r, rect.x + rect.w - 10, rect.y + 10);
    draw_screw(r, rect.x + 10, rect.y + rect.h - 10);
    draw_screw(r, rect.x + rect.w - 10, rect.y + rect.h - 10);

    draw_text(r, fonts->section, rect.x + 22, rect.y + 10,
              (SDL_Color){229, 221, 194, 255}, title);

    SDL_SetRenderDrawColor(r, 171, 162, 130, 255);
    SDL_RenderLine(r, rect.x + 20, rect.y + 39,
                   rect.x + rect.w - 20, rect.y + 39);
}

static void draw_led(SDL_Renderer *r, PanelFonts *fonts,
                     float x, float y, const char *label,
                     bool on, SDL_Color lamp)
{
    SDL_FRect bezel = {x, y + 19, 26, 24};
    SDL_FRect lens = {x + 4, y + 23, 18, 16};

    draw_text_center(r, fonts->small,
        (SDL_FRect){x - 4, y, 34, 17},
        (SDL_Color){220, 216, 198, 255}, label);

    SDL_SetRenderDrawColor(r, 10, 11, 11, 255);
    SDL_RenderFillRect(r, &bezel);
    SDL_SetRenderDrawColor(r, 105, 107, 100, 255);
    SDL_RenderRect(r, &bezel);

    if (on) {
        SDL_SetRenderDrawColor(r, lamp.r, lamp.g, lamp.b, 255);
    } else {
        SDL_SetRenderDrawColor(r,
            (Uint8)(lamp.r / 7),
            (Uint8)(lamp.g / 7),
            (Uint8)(lamp.b / 7), 255);
    }
    SDL_RenderFillRect(r, &lens);

    if (on) {
        SDL_SetRenderDrawColor(r, 245, 244, 210, 210);
        SDL_FRect glint = {lens.x + 5, lens.y + 3, 6, 5};
        SDL_RenderFillRect(r, &glint);
    }
}

static void draw_toggle(SDL_Renderer *r, PanelFonts *fonts,
                        float x, float y, const char *label, bool on)
{
    draw_text_center(r, fonts->small,
        (SDL_FRect){x - 4, y, 34, 17},
        (SDL_Color){221, 217, 199, 255}, label);

    SDL_SetRenderDrawColor(r, 124, 125, 119, 255);
    fill_circle(r, x + 13, y + 42, 8.5f);
    SDL_SetRenderDrawColor(r, 20, 21, 21, 255);
    fill_circle(r, x + 13, y + 42, 4.5f);

    float tip_y = on ? y + 26 : y + 57;
    SDL_SetRenderDrawColor(r, 200, 201, 194, 255);
    SDL_RenderLine(r, x + 13, y + 42, x + 13, tip_y);
    SDL_RenderLine(r, x + 12, y + 42, x + 12, tip_y);

    SDL_SetRenderDrawColor(r, 220, 220, 211, 255);
    fill_circle(r, x + 12.5f, tip_y, 3.8f);
}

static void draw_push_button(SDL_Renderer *r, PanelFonts *fonts,
                             SDL_FRect rect, const char *label,
                             SDL_Color face, bool active)
{
    SDL_Color actual = face;
    if (active) {
        actual.r = (Uint8)SDL_min(255, actual.r + 24);
        actual.g = (Uint8)SDL_min(255, actual.g + 24);
        actual.b = (Uint8)SDL_min(255, actual.b + 24);
    }

    draw_bevel_panel(r, rect, actual,
        (SDL_Color){208, 206, 190, 255},
        (SDL_Color){16, 16, 16, 255});

    draw_text_center(r, fonts->label, rect,
                     (SDL_Color){246, 242, 221, 255}, label);
}

static void draw_knob(SDL_Renderer *r, PanelFonts *fonts,
                      float cx, float cy, float radius,
                      int index, int count,
                      const char *value)
{
    SDL_SetRenderDrawColor(r, 13, 14, 14, 255);
    fill_circle(r, cx, cy, radius + 5);

    SDL_SetRenderDrawColor(r, 103, 104, 100, 255);
    fill_circle(r, cx, cy, radius);

    SDL_SetRenderDrawColor(r, 48, 49, 48, 255);
    fill_circle(r, cx, cy, radius - 7);

    float t = count > 1 ? (float)index / (float)(count - 1) : 0.0f;
    float angle = (225.0f - t * 270.0f) * 3.14159265358979323846f / 180.0f;
    float px = cx + cosf(angle) * radius * 0.62f;
    float py = cy - sinf(angle) * radius * 0.62f;

    SDL_SetRenderDrawColor(r, 225, 217, 192, 255);
    SDL_RenderLine(r, cx, cy, px, py);

    draw_text_center(r, fonts->small,
        (SDL_FRect){cx - 70, cy + radius + 8, 140, 18},
        (SDL_Color){215, 210, 191, 255}, value);
}

static void draw_menu_button(SDL_Renderer *r, PanelFonts *fonts,
                             SDL_FRect rect, const char *label)
{
    SDL_SetRenderDrawColor(r, 47, 49, 48, 255);
    SDL_RenderFillRect(r, &rect);
    SDL_SetRenderDrawColor(r, 66, 68, 65, 255);
    SDL_RenderRect(r, &rect);
    draw_text_center(r, fonts->small, rect,
                     (SDL_Color){225, 221, 204, 255}, label);
}

static void read_log_tail(char lines[][160], int *line_count, int max_lines) {
    *line_count = 0;
    FILE *f = fopen("latest.log", "r");
    if (!f) return;

    char ring[32][160] = {{0}};
    int total = 0;
    char buf[160];

    while (fgets(buf, sizeof(buf), f)) {
        snprintf(ring[total % max_lines], 160, "%s", buf);
        total++;
    }
    fclose(f);

    int count = total < max_lines ? total : max_lines;
    int start = total > max_lines ? total - max_lines : 0;

    for (int i = 0; i < count; i++) {
        snprintf(lines[i], 160, "%s", ring[(start + i) % max_lines]);
        size_t n = strlen(lines[i]);
        while (n && (lines[i][n - 1] == '\n' || lines[i][n - 1] == '\r')) {
            lines[i][--n] = '\0';
        }
    }
    *line_count = count;
}

static void destroy_aux(AuxWindow *aux) {
    if (!aux) return;
    if (aux->renderer) SDL_DestroyRenderer(aux->renderer);
    if (aux->window) SDL_DestroyWindow(aux->window);
    memset(aux, 0, sizeof(*aux));
}

static void open_aux(AuxWindow *aux, AuxKind kind, const char *title, int w, int h) {
    if (aux->window) {
        SDL_RaiseWindow(aux->window);
        return;
    }

    aux->kind = kind;
    aux->window = SDL_CreateWindow(title, w, h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!aux->window) return;

    aux->renderer = SDL_CreateRenderer(aux->window, NULL);
    if (!aux->renderer) {
        SDL_DestroyWindow(aux->window);
        memset(aux, 0, sizeof(*aux));
        return;
    }

    SDL_SetRenderVSync(aux->renderer, 1);
}

static void draw_aux_window(AuxWindow *aux, PanelFonts *fonts,
                            Preferences *prefs,
                            const PPA1GuiBackend *backend)
{
    if (!aux || !aux->renderer) return;

    SDL_Renderer *r = aux->renderer;
    SDL_SetRenderDrawColor(r, 28, 30, 30, 255);
    SDL_RenderClear(r);

    SDL_Color cream = {228, 222, 201, 255};
    SDL_Color muted = {180, 181, 171, 255};

    if (aux->kind == AUX_PREFERENCES) {
        draw_text(r, fonts->section, 24, 20, cream, "PREFERENCES");
        draw_text(r, fonts->label, 24, 70, cream, "Front panel");
        draw_text(r, fonts->small, 24, 100, muted,
                  prefs->high_contrast ? "High contrast panel: ON" : "High contrast panel: OFF");
        draw_text(r, fonts->small, 24, 128, muted,
                  prefs->lamp_test ? "Lamp test: ON" : "Lamp test: OFF");
        draw_text(r, fonts->small, 24, 156, muted,
                  prefs->auto_run ? "Auto RUN after LOAD: ON" : "Auto RUN after LOAD: OFF");
        draw_text(r, fonts->small, 24, 184, muted,
                  prefs->tty_requested ? "External PTY request: ON" : "External PTY request: OFF");
        draw_text(r, fonts->small, 24, 230, muted,
                  "Click each line to toggle the setting.");
    } else if (aux->kind == AUX_VIEW) {
        draw_text(r, fonts->section, 24, 20, cream, "PROCESSOR VIEW");
        bool loaded = backend->is_loaded(backend->ctx);
        bool power = backend->is_powered(backend->ctx);
        bool running = backend->is_running(backend->ctx);
        bool halted = backend->is_halted(backend->ctx);
        char line[128];

        snprintf(line, sizeof(line), "Image loaded: %s", loaded ? "YES" : "NO");
        draw_text(r, fonts->small, 24, 76, muted, line);
        snprintf(line, sizeof(line), "Power: %s", power ? "ON" : "OFF");
        draw_text(r, fonts->small, 24, 102, muted, line);
        snprintf(line, sizeof(line), "Run state: %s", running ? "RUNNING" : "STOPPED");
        draw_text(r, fonts->small, 24, 128, muted, line);
        snprintf(line, sizeof(line), "HALT: %s", halted ? "ACTIVE" : "CLEAR");
        draw_text(r, fonts->small, 24, 154, muted, line);
        snprintf(line, sizeof(line), "ADDRESS BUS: %04X", backend->address_bus(backend->ctx));
        draw_text(r, fonts->small, 24, 196, cream, line);
        snprintf(line, sizeof(line), "DATA BUS: %02X", backend->data_bus(backend->ctx));
        draw_text(r, fonts->small, 24, 222, cream, line);
    } else if (aux->kind == AUX_LOG) {
        draw_text(r, fonts->section, 24, 20, cream, "LATEST.LOG");
        char lines[16][160];
        int count = 0;
        read_log_tail(lines, &count, 16);
        float y = 60.0f;
        for (int i = 0; i < count; i++, y += 20.0f) {
            draw_text(r, fonts->small, 24, y, muted, lines[i]);
        }
    } else if (aux->kind == AUX_HELP) {
        draw_text(r, fonts->section, 24, 20, cream, "PPA-1 FRONT PANEL HELP");
        draw_text(r, fonts->small, 24, 70, muted, "OPEN         Select a 4 MiB EEPROM image.");
        draw_text(r, fonts->small, 24, 96, muted, "LOAD         Load/reset the selected image into the emulator.");
        draw_text(r, fonts->small, 24, 122, muted, "RUN/STOP     Start or stop continuous CPU execution.");
        draw_text(r, fonts->small, 24, 148, muted, "STEP         Execute one complete instruction.");
        draw_text(r, fonts->small, 24, 174, muted, "MICROSTEP    Execute one controller microstep.");
        draw_text(r, fonts->small, 24, 200, muted, "RESET        Reset CPU registers; EEPROM remains loaded.");
        draw_text(r, fonts->small, 24, 226, muted, "PWR          Power-gates the processor only; GUI stays open.");
        draw_text(r, fonts->small, 24, 252, muted, "Rotary controls: click left/right side to rotate.");
    }

    SDL_RenderPresent(r);
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

int ppa_gui_run_panel(const char *initial_rom,
                      bool initial_tty_enabled,
                      const PPA1GuiBackend *backend)
{
    if (!backend || !backend->ctx) return 1;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        ppa_log_printf("GUI: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (!TTF_Init()) {
        ppa_log_printf("GUI: TTF_Init failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    PanelFonts fonts = {0};
    if (!load_fonts(&fonts)) {
        ppa_log_printf("GUI: font initialization failed.\n");
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "PROGRAMBLE PROCESSOR A-1",
        PANEL_W, PANEL_H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        close_fonts(&fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_DestroyWindow(window);
        close_fonts(&fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderLogicalPresentation(
        renderer, PANEL_W, PANEL_H,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);

    Uint32 main_window_id = SDL_GetWindowID(window);

    FileDialogState file_state = {0};
    if (initial_rom && *initial_rom) {
        snprintf(file_state.rom_path, sizeof(file_state.rom_path), "%s", initial_rom);
    }

    const SDL_DialogFileFilter filters[] = {
        {"PPA-1 EEPROM image", "bin"},
        {"All files", "*"}
    };

    Preferences prefs = {
        .auto_run = false,
        .lamp_test = false,
        .high_contrast = false,
        .tty_requested = initial_tty_enabled
    };

    AuxWindow prefs_window = {0};
    AuxWindow view_window = {0};
    AuxWindow log_window = {0};
    AuxWindow help_window = {0};

    bool switches[16] = {0};
    bool panel_running = true;
    bool power_on = false;

    const char *clock_values[] = {
        "1 Hz", "10 Hz", "100 Hz", "1 kHz",
        "10 kHz", "50 kHz", "100 kHz"
    };
    const double clock_rates[] = {
        1.0, 10.0, 100.0, 1000.0,
        10000.0, 50000.0, 100000.0
    };
    int clock_index = 3;

    const char *output_values[] = {
        "PARALLEL", "SERIAL TTL", "SERIAL RS-232", "INTERNAL"
    };
    int output_index = 3;

    const char *format_values[] = {"BIN", "HEX", "ASCII"};
    int format_index = 1;

    SDL_FRect menu_open = {10, 4, 72, 29};
    SDL_FRect menu_pref = {86, 4, 112, 29};
    SDL_FRect menu_view = {202, 4, 72, 29};
    SDL_FRect menu_log  = {278, 4, 64, 29};
    SDL_FRect menu_help = {346, 4, 72, 29};

    SDL_FRect op_run   = {626, 686, 72, 42};
    SDL_FRect op_stop  = {706, 686, 72, 42};
    SDL_FRect op_step  = {786, 686, 76, 42};
    SDL_FRect op_ustep = {870, 686, 92, 42};
    SDL_FRect op_reset = {970, 686, 78, 42};
    SDL_FRect op_exam  = {1056, 686, 86, 42};
    SDL_FRect op_dep   = {1150, 686, 86, 42};

    SDL_FRect load_btn = {1022, 750, 154, 40};
    SDL_FRect power_btn = {1195, 665, 46, 112};

    const float clock_cx = 738.0f, clock_cy = 472.0f, clock_radius = 36.0f;
    const float output_cx = 912.0f, output_cy = 472.0f, output_radius = 32.0f;
    SDL_FRect format_hit = {1050, 427, 130, 90};

    Uint64 last_ticks = SDL_GetTicks();
    double instruction_accum = 0.0;

    ppa_log_printf("GUI: persistent PPA-1 front panel opened.\n");

    while (panel_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                panel_running = false;
                continue;
            }

            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                Uint32 id = event.window.windowID;
                if (id == main_window_id) {
                    panel_running = false;
                } else if (prefs_window.window && id == SDL_GetWindowID(prefs_window.window)) {
                    destroy_aux(&prefs_window);
                } else if (view_window.window && id == SDL_GetWindowID(view_window.window)) {
                    destroy_aux(&view_window);
                } else if (log_window.window && id == SDL_GetWindowID(log_window.window)) {
                    destroy_aux(&log_window);
                } else if (help_window.window && id == SDL_GetWindowID(help_window.window)) {
                    destroy_aux(&help_window);
                }
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                Uint32 id = event.button.windowID;

                if (id == main_window_id) {
                    SDL_ConvertEventToRenderCoordinates(renderer, &event);
                    float x = event.button.x;
                    float y = event.button.y;

                    if (event.button.button == SDL_BUTTON_LEFT) {
                        if (point_in_rect(x, y, &menu_open) && !file_state.dialog_open) {
                            file_state.dialog_open = true;
                            SDL_ShowOpenFileDialog(
                                file_dialog_callback, &file_state, window,
                                filters, 2, NULL, false);
                        } else if (point_in_rect(x, y, &menu_pref)) {
                            open_aux(&prefs_window, AUX_PREFERENCES, "PPA-1 Preferences", 520, 330);
                        } else if (point_in_rect(x, y, &menu_view)) {
                            open_aux(&view_window, AUX_VIEW, "PPA-1 Processor View", 520, 330);
                        } else if (point_in_rect(x, y, &menu_log)) {
                            open_aux(&log_window, AUX_LOG, "PPA-1 Log", 760, 430);
                        } else if (point_in_rect(x, y, &menu_help)) {
                            open_aux(&help_window, AUX_HELP, "PPA-1 Help", 620, 360);
                        } else if (point_in_rect(x, y, &op_run)) {
                            backend->set_running(backend->ctx, true);
                        } else if (point_in_rect(x, y, &op_stop)) {
                            backend->set_running(backend->ctx, false);
                        } else if (point_in_rect(x, y, &op_step)) {
                            backend->step_instruction(backend->ctx);
                        } else if (point_in_rect(x, y, &op_ustep)) {
                            backend->step_micro(backend->ctx);
                        } else if (point_in_rect(x, y, &op_reset)) {
                            backend->reset(backend->ctx);
                        } else if (point_in_rect(x, y, &op_exam)) {
                            ppa_log_printf("GUI: EXAMINE requested, switch register=%04X\n",
                                (unsigned)(
                                    (switches[0] ? 1u : 0u) |
                                    (switches[1] ? 2u : 0u) |
                                    (switches[2] ? 4u : 0u) |
                                    (switches[3] ? 8u : 0u)));
                        } else if (point_in_rect(x, y, &op_dep)) {
                            ppa_log_printf("GUI: DEPOSIT requested. Memory write control will be wired next.\n");
                        } else if (point_in_rect(x, y, &power_btn)) {
                            power_on = !backend->is_powered(backend->ctx);
                            backend->set_power(backend->ctx, power_on);
                        } else if (point_in_rect(x, y, &load_btn)) {
                            if (!file_state.rom_path[0]) {
                                if (!file_state.dialog_open) {
                                    file_state.dialog_open = true;
                                    SDL_ShowOpenFileDialog(
                                        file_dialog_callback, &file_state, window,
                                        filters, 2, NULL, false);
                                }
                            } else if (backend->load_rom(backend->ctx, file_state.rom_path)) {
                                power_on = true;
                                backend->set_power(backend->ctx, true);
                                if (prefs.auto_run) backend->set_running(backend->ctx, true);
                            }
                        } else if (point_in_circle(x, y, clock_cx, clock_cy, clock_radius + 10)) {
                            if (x < clock_cx) {
                                if (clock_index > 0) clock_index--;
                            } else {
                                if (clock_index < 6) clock_index++;
                            }
                            ppa_log_printf("GUI: clock selector -> %s\n", clock_values[clock_index]);
                        } else if (point_in_circle(x, y, output_cx, output_cy, output_radius + 10)) {
                            if (x < output_cx) {
                                output_index = (output_index + 3) % 4;
                            } else {
                                output_index = (output_index + 1) % 4;
                            }
                            ppa_log_printf("GUI: data output -> %s\n", output_values[output_index]);
                        } else if (point_in_rect(x, y, &format_hit)) {
                            format_index = (format_index + 1) % 3;
                            ppa_log_printf("GUI: data format -> %s\n", format_values[format_index]);
                        } else {
                            for (int i = 0; i < 16; i++) {
                                SDL_FRect hit = {54.0f + i * 32.0f, 621.0f, 28.0f, 70.0f};
                                if (point_in_rect(x, y, &hit)) {
                                    switches[i] = !switches[i];
                                    break;
                                }
                            }
                        }
                    }
                } else if (prefs_window.window &&
                           id == SDL_GetWindowID(prefs_window.window) &&
                           event.button.button == SDL_BUTTON_LEFT) {
                    float x = event.button.x;
                    float y = event.button.y;
                    (void)x;
                    if (y >= 90 && y < 118) prefs.high_contrast = !prefs.high_contrast;
                    else if (y >= 118 && y < 146) prefs.lamp_test = !prefs.lamp_test;
                    else if (y >= 146 && y < 174) prefs.auto_run = !prefs.auto_run;
                    else if (y >= 174 && y < 206) prefs.tty_requested = !prefs.tty_requested;
                }
            }
        }

        Uint64 now = SDL_GetTicks();
        double dt = (double)(now - last_ticks) / 1000.0;
        last_ticks = now;

        bool loaded = backend->is_loaded(backend->ctx);
        bool powered = backend->is_powered(backend->ctx);
        bool running = backend->is_running(backend->ctx);
        bool halted = backend->is_halted(backend->ctx);

        if (loaded && powered && running && !halted) {
            instruction_accum += dt * clock_rates[clock_index];
            int budget = (int)instruction_accum;
            if (budget > 0) {
                if (budget > 5000) budget = 5000;
                backend->tick(backend->ctx, budget);
                instruction_accum -= (double)budget;
            }
        } else {
            instruction_accum = 0.0;
        }

        loaded = backend->is_loaded(backend->ctx);
        powered = backend->is_powered(backend->ctx);
        running = backend->is_running(backend->ctx);
        halted = backend->is_halted(backend->ctx);

        Uint16 address = backend->address_bus(backend->ctx);
        Uint8 data = backend->data_bus(backend->ctx);

        SDL_SetRenderDrawColor(renderer,
            prefs.high_contrast ? 18 : 25,
            prefs.high_contrast ? 19 : 27,
            prefs.high_contrast ? 19 : 27, 255);
        SDL_RenderClear(renderer);

        SDL_FRect menu_bar = {0, 0, PANEL_W, 38};
        SDL_SetRenderDrawColor(renderer, 34, 36, 36, 255);
        SDL_RenderFillRect(renderer, &menu_bar);
        draw_menu_button(renderer, &fonts, menu_open, "OPEN");
        draw_menu_button(renderer, &fonts, menu_pref, "PREFERENCES");
        draw_menu_button(renderer, &fonts, menu_view, "VIEW");
        draw_menu_button(renderer, &fonts, menu_log, "LOG");
        draw_menu_button(renderer, &fonts, menu_help, "HELP");

        /* Header deliberately stretches to the A-1 badge, like a real engraved panel. */
        SDL_FRect title_rect = {42, 53, 980, 48};
        draw_text_fit(renderer, fonts.title, title_rect,
                      (SDL_Color){238, 230, 202, 255},
                      "PROGRAMBLE PROCESSOR");

        SDL_SetRenderDrawColor(renderer, 202, 190, 151, 255);
        SDL_RenderLine(renderer, 42, 108, 1022, 108);

        SDL_FRect badge = {1040, 49, 190, 66};
        draw_bevel_panel(renderer, badge,
            (SDL_Color){91, 38, 30, 255},
            (SDL_Color){218, 202, 160, 255},
            (SDL_Color){34, 17, 16, 255});
        draw_text_center(renderer, fonts.badge, badge,
                         (SDL_Color){245, 232, 195, 255}, "A-1");
        draw_text_center(renderer, fonts.section,
            (SDL_FRect){1040, 117, 190, 27},
            (SDL_Color){224, 216, 191, 255}, "Adava Electronics");

        SDL_FRect indicators = {30, 155, 565, 274};
        SDL_FRect status = {620, 155, 625, 178};
        SDL_FRect io = {620, 350, 625, 191};
        SDL_FRect manual = {30, 448, 565, 297};
        SDL_FRect ops = {620, 560, 565, 185};
        SDL_FRect pwr = {1194, 560, 51, 185};

        draw_section(renderer, &fonts, indicators, "INDICATOR PANEL", prefs.high_contrast);
        draw_section(renderer, &fonts, status, "SYSTEM INDICATORS", prefs.high_contrast);
        draw_section(renderer, &fonts, io, "CLOCK & DATA OUTPUT", prefs.high_contrast);
        draw_section(renderer, &fonts, manual, "MANUAL INPUT", prefs.high_contrast);
        draw_section(renderer, &fonts, ops, "CONTROL & OPERATIONS", prefs.high_contrast);
        draw_section(renderer, &fonts, pwr, "PWR", prefs.high_contrast);

        draw_text(renderer, fonts.label, 52, 207,
                  (SDL_Color){214, 208, 186, 255}, "ADDRESS BUS  A0-A15");

        for (int i = 0; i < 16; i++) {
            char label[2] = {"0123456789ABCDEF"[i], 0};
            bool bit = prefs.lamp_test ||
                (powered && loaded && ((address >> i) & 1u));
            draw_led(renderer, &fonts,
                     54.0f + i * 32.0f, 236.0f, label, bit,
                     (SDL_Color){235, 154, 37, 255});
        }

        draw_text(renderer, fonts.label, 52, 315,
                  (SDL_Color){214, 208, 186, 255}, "DATA BUS  D0-D7");
        for (int i = 0; i < 8; i++) {
            char label[2] = {(char)('0' + i), 0};
            bool bit = prefs.lamp_test ||
                (powered && loaded && ((data >> i) & 1u));
            draw_led(renderer, &fonts,
                     54.0f + i * 48.0f, 343.0f, label, bit,
                     (SDL_Color){38, 207, 190, 255});
        }

        draw_text(renderer, fonts.label, 438, 315,
                  (SDL_Color){214, 208, 186, 255}, "STATUS");
        draw_led(renderer, &fonts, 438, 343, "RDY",
                 prefs.lamp_test || (loaded && powered && !halted),
                 (SDL_Color){72, 220, 89, 255});
        draw_led(renderer, &fonts, 482, 343, "RUN",
                 prefs.lamp_test || (powered && running),
                 (SDL_Color){72, 220, 89, 255});
        draw_led(renderer, &fonts, 526, 343, "HLT",
                 prefs.lamp_test || (loaded && halted),
                 (SDL_Color){222, 58, 43, 255});

        /* Top-right is indicators only: operational buttons live at the bottom. */
        const char *sys_labels[] = {"POWER", "RUN", "HALT", "ERROR", "CLOCK", "MEMORY", "I/O"};
        SDL_Color sys_colors[] = {
            {72,220,89,255}, {72,220,89,255}, {222,58,43,255},
            {222,58,43,255}, {235,154,37,255}, {72,220,89,255},
            {38,207,190,255}
        };
        bool sys_on[] = {
            powered,
            powered && running,
            loaded && halted,
            false,
            powered && running && ((now / 250) & 1u),
            loaded,
            false
        };
        for (int i = 0; i < 7; i++) {
            draw_led(renderer, &fonts, 650.0f + i * 78.0f, 211.0f,
                     sys_labels[i],
                     prefs.lamp_test || sys_on[i],
                     sys_colors[i]);
        }

        draw_text(renderer, fonts.small, 651, 286,
                  (SDL_Color){180, 177, 161, 255},
                  "Front-panel controls are grouped below, PDP-style.");

        draw_text(renderer, fonts.label, 651, 401,
                  (SDL_Color){214, 208, 186, 255}, "CLOCK RATE");
        draw_knob(renderer, &fonts, clock_cx, clock_cy, clock_radius,
                  clock_index, 7, clock_values[clock_index]);

        draw_text(renderer, fonts.label, 829, 401,
                  (SDL_Color){214, 208, 186, 255}, "DATA OUTPUT");
        draw_knob(renderer, &fonts, output_cx, output_cy, output_radius,
                  output_index, 4, output_values[output_index]);

        draw_text(renderer, fonts.label, 1042, 401,
                  (SDL_Color){214, 208, 186, 255}, "DISPLAY FORMAT");
        draw_text_center(renderer, fonts.label, format_hit,
                         (SDL_Color){235, 225, 198, 255},
                         format_values[format_index]);
        SDL_SetRenderDrawColor(renderer, 100, 102, 96, 255);
        SDL_RenderRect(renderer, &format_hit);

        draw_text(renderer, fonts.label, 52, 500,
                  (SDL_Color){214, 208, 186, 255},
                  "SWITCH REGISTER  SR0-SR15");
        draw_text(renderer, fonts.small, 52, 527,
                  (SDL_Color){178, 176, 162, 255},
                  "Manual address/data entry");
        for (int i = 0; i < 16; i++) {
            char label[2] = {"0123456789ABCDEF"[i], 0};
            draw_toggle(renderer, &fonts,
                        54.0f + i * 32.0f, 576.0f, label, switches[i]);
        }
        draw_text(renderer, fonts.small, 54, 705,
                  (SDL_Color){179, 177, 162, 255}, "DOWN = 0");
        draw_text(renderer, fonts.small, 500, 705,
                  (SDL_Color){179, 177, 162, 255}, "UP = 1");

        draw_text(renderer, fonts.small, 646, 614,
                  (SDL_Color){180, 177, 161, 255},
                  loaded ? "CPU IMAGE LOADED" : "NO EEPROM IMAGE LOADED");

        draw_push_button(renderer, &fonts, op_run, "RUN",
                         (SDL_Color){41, 112, 55, 255}, powered && running);
        draw_push_button(renderer, &fonts, op_stop, "STOP",
                         (SDL_Color){135, 45, 39, 255}, powered && !running);
        draw_push_button(renderer, &fonts, op_step, "STEP",
                         (SDL_Color){99, 96, 86, 255}, false);
        draw_push_button(renderer, &fonts, op_ustep, "MICRO",
                         (SDL_Color){99, 96, 86, 255}, false);
        draw_push_button(renderer, &fonts, op_reset, "RESET",
                         (SDL_Color){99, 96, 86, 255}, false);
        draw_push_button(renderer, &fonts, op_exam, "EXAMINE",
                         (SDL_Color){99, 96, 86, 255}, false);
        draw_push_button(renderer, &fonts, op_dep, "DEPOSIT",
                         (SDL_Color){99, 96, 86, 255}, false);

        draw_push_button(renderer, &fonts, load_btn,
                         loaded ? "RELOAD IMAGE" :
                         (file_state.rom_path[0] ? "LOAD IMAGE" : "OPEN IMAGE"),
                         file_state.rom_path[0]
                             ? (SDL_Color){170, 112, 30, 255}
                             : (SDL_Color){84, 84, 78, 255},
                         false);

        /* PWR is a processor power gate, not an application quit control. */
        SDL_SetRenderDrawColor(renderer, 19, 19, 18, 255);
        SDL_RenderFillRect(renderer, &power_btn);
        SDL_SetRenderDrawColor(renderer, 136, 91, 53, 255);
        SDL_RenderRect(renderer, &power_btn);

        SDL_FRect rocker = {1203, powered ? 681.0f : 715.0f, 28, 48};
        SDL_SetRenderDrawColor(renderer,
            powered ? 116 : 73,
            powered ? 43 : 30,
            powered ? 31 : 27, 255);
        SDL_RenderFillRect(renderer, &rocker);
        draw_text_center(renderer, fonts.label, rocker,
                         (SDL_Color){244, 231, 205, 255},
                         powered ? "I" : "O");

        SDL_FRect statusbar = {30, 764, 1155, 34};
        SDL_SetRenderDrawColor(renderer, 46, 48, 47, 255);
        SDL_RenderFillRect(renderer, &statusbar);

        char status_text[1160];
        snprintf(status_text, sizeof(status_text),
                 "%s   |   CLOCK %s   |   OUTPUT %s / %s   |   LOG latest.log",
                 file_state.rom_path[0] ? file_state.rom_path : "No EEPROM image selected",
                 clock_values[clock_index],
                 output_values[output_index],
                 format_values[format_index]);
        draw_text(renderer, fonts.small, 42, 773,
                  (SDL_Color){193, 190, 174, 255}, status_text);

        SDL_RenderPresent(renderer);

        draw_aux_window(&prefs_window, &fonts, &prefs, backend);
        draw_aux_window(&view_window, &fonts, &prefs, backend);
        draw_aux_window(&log_window, &fonts, &prefs, backend);
        draw_aux_window(&help_window, &fonts, &prefs, backend);
    }

    backend->set_running(backend->ctx, false);

    destroy_aux(&prefs_window);
    destroy_aux(&view_window);
    destroy_aux(&log_window);
    destroy_aux(&help_window);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    close_fonts(&fonts);
    TTF_Quit();
    SDL_Quit();

    ppa_log_printf("GUI: front panel closed.\n");
    return 0;
}
