#include "gui.h"
#include "logger.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define PANEL_W 1280
#define PANEL_H 760

typedef struct {
    char rom_path[1024];
    bool dialog_open;
    bool load_requested;
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
    AUX_LOG
} AuxKind;

#define TERM_LINES 128
#define TERM_COLS 96

typedef enum {
    BTN_NONE = 0,
    BTN_LOAD_ADDR,
    BTN_ADDR_DEC,
    BTN_ADDR_INC,
    BTN_EXAM,
    BTN_DEPOSIT,
    BTN_START,
    BTN_CONT,
    BTN_STOP,
    BTN_STEP,
    BTN_MICRO,
    BTN_RESET
} ButtonId;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    AuxKind kind;
} AuxWindow;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    char lines[TERM_LINES][TERM_COLS + 1];
    int line;
    int col;
} TerminalWindow;

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
    state->load_requested = true;
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
                             SDL_Color face, bool active, bool pressed)
{
    SDL_Color actual = face;
    if (active) {
        actual.r = (Uint8)SDL_min(255, actual.r + 24);
        actual.g = (Uint8)SDL_min(255, actual.g + 24);
        actual.b = (Uint8)SDL_min(255, actual.b + 24);
    }

    if (pressed) {
        rect.x += 2.0f;
        rect.y += 2.0f;
        actual.r = (Uint8)(actual.r * 0.84f);
        actual.g = (Uint8)(actual.g * 0.84f);
        actual.b = (Uint8)(actual.b * 0.84f);
        draw_bevel_panel(r, rect, actual,
            (SDL_Color){24, 24, 23, 255},
            (SDL_Color){150, 148, 136, 255});
    } else {
        draw_bevel_panel(r, rect, actual,
            (SDL_Color){208, 206, 190, 255},
            (SDL_Color){16, 16, 16, 255});
    }

    draw_text_center(r, fonts->label, rect,
                     (SDL_Color){246, 242, 221, 255}, label);
}

static bool button_pressed(ButtonId current, ButtonId id, Uint64 until) {
    return current == id && SDL_GetTicks() < until;
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

static void terminal_new_line(TerminalWindow *term) {
    if (!term) return;

    term->line++;
    term->col = 0;

    if (term->line >= TERM_LINES) {
        memmove(term->lines[0], term->lines[1],
                sizeof(term->lines[0]) * (TERM_LINES - 1));
        term->line = TERM_LINES - 1;
    }

    memset(term->lines[term->line], 0, sizeof(term->lines[term->line]));
}

static void terminal_put_byte(TerminalWindow *term, uint8_t value) {
    if (!term) return;

    if (value == '\r') {
        term->col = 0;
        return;
    }
    if (value == '\n') {
        terminal_new_line(term);
        return;
    }
    if (value == '\b' || value == 0x7F) {
        if (term->col > 0) {
            term->col--;
            term->lines[term->line][term->col] = '\0';
        }
        return;
    }
    if (value == '\t') {
        int spaces = 4 - (term->col % 4);
        while (spaces-- > 0) terminal_put_byte(term, ' ');
        return;
    }
    if (value < 0x20) return;

    if (term->col >= TERM_COLS) terminal_new_line(term);

    term->lines[term->line][term->col++] = (char)value;
    term->lines[term->line][term->col] = '\0';
}

static bool open_terminal(TerminalWindow *term,
                          const PPA1GuiBackend *backend)
{
    if (!term || !backend) return false;

    if (term->window) {
        SDL_RaiseWindow(term->window);
        return true;
    }

    term->window = SDL_CreateWindow(
        "PPA-1 UART Terminal",
        680, 380,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!term->window) return false;

    term->renderer = SDL_CreateRenderer(term->window, NULL);
    if (!term->renderer) {
        SDL_DestroyWindow(term->window);
        memset(term, 0, sizeof(*term));
        return false;
    }

    SDL_SetRenderVSync(term->renderer, 1);
    SDL_SetWindowMinimumSize(term->window, 360, 220);
    SDL_StartTextInput(term->window);

    if (backend->set_terminal_enabled) {
        backend->set_terminal_enabled(backend->ctx, true);
    }
    return true;
}

static void close_terminal(TerminalWindow *term,
                           const PPA1GuiBackend *backend)
{
    if (!term) return;

    if (term->window) SDL_StopTextInput(term->window);

    if (backend && backend->set_terminal_enabled) {
        backend->set_terminal_enabled(backend->ctx, false);
    }

    if (term->renderer) SDL_DestroyRenderer(term->renderer);
    if (term->window) SDL_DestroyWindow(term->window);
    memset(term, 0, sizeof(*term));
}

static void update_terminal(TerminalWindow *term,
                            const PPA1GuiBackend *backend)
{
    if (!term || !term->window || !backend || !backend->terminal_read_tx) return;

    uint8_t buf[256];
    size_t n;
    do {
        n = backend->terminal_read_tx(backend->ctx, buf, sizeof(buf));
        for (size_t i = 0; i < n; i++) {
            terminal_put_byte(term, buf[i]);
        }
    } while (n == sizeof(buf));
}

static void draw_terminal(TerminalWindow *term, PanelFonts *fonts,
                          const PPA1GuiBackend *backend)
{
    if (!term || !term->renderer || !fonts) return;

    update_terminal(term, backend);

    int w = 680, h = 380;
    SDL_GetCurrentRenderOutputSize(term->renderer, &w, &h);

    SDL_SetRenderDrawColor(term->renderer, 10, 14, 13, 255);
    SDL_RenderClear(term->renderer);

    draw_text(term->renderer, fonts->label, 14, 10,
              (SDL_Color){140, 224, 171, 255},
              "PPA-1 UART TERMINAL  FF00 TX / FF01 RX");

    SDL_SetRenderDrawColor(term->renderer, 45, 72, 58, 255);
    SDL_RenderLine(term->renderer, 14, 34, (float)w - 14.0f, 34);

    int visible = (h - 58) / 18;
    if (visible < 1) visible = 1;
    if (visible > TERM_LINES) visible = TERM_LINES;

    int start = term->line - visible + 1;
    if (start < 0) start = 0;

    float y = 44.0f;
    for (int line = start; line <= term->line && line < TERM_LINES; line++) {
        draw_text(term->renderer, fonts->small, 14, y,
                  (SDL_Color){201, 230, 208, 255},
                  term->lines[line]);
        y += 18.0f;
    }

    SDL_RenderPresent(term->renderer);
}

static bool launch_uart_terminal(const PPA1GuiBackend *backend) {
    if (!backend || !backend->set_tty_enabled ||
        !backend->is_tty_enabled || !backend->tty_name) {
        return false;
    }

    if (!backend->is_tty_enabled(backend->ctx)) {
        if (!backend->set_tty_enabled(backend->ctx, true)) {
            ppa_log_printf("GUI: failed to enable UART PTY.\n");
            return false;
        }
    }

    const char *tty = backend->tty_name(backend->ctx);
    if (!tty || !*tty) {
        ppa_log_printf("GUI: UART PTY has no device name.\n");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        ppa_log_printf("GUI: fork() failed while opening UART terminal.\n");
        return false;
    }

    if (pid == 0) {
        (void)setsid();

        const char *script =
            "printf '\\033]0;PPA-1 UART TERMINAL\\007'; "
            "exec screen \"$1\" 9600";

        execlp("x-terminal-emulator",
               "x-terminal-emulator",
               "-e", "sh", "-lc", script, "sh", tty,
               (char *)NULL);

        char xfce_command[256];
        snprintf(xfce_command, sizeof(xfce_command),
                 "screen %s 9600", tty);

        execlp("xfce4-terminal",
               "xfce4-terminal",
               "--title=PPA-1 UART TERMINAL",
               "-e", xfce_command,
               (char *)NULL);

        execlp("gnome-terminal",
               "gnome-terminal",
               "--title=PPA-1 UART TERMINAL",
               "--", "screen", tty, "9600",
               (char *)NULL);

        execlp("konsole",
               "konsole",
               "--title", "PPA-1 UART TERMINAL",
               "-e", "screen", tty, "9600",
               (char *)NULL);

        execlp("xterm",
               "xterm",
               "-T", "PPA-1 UART TERMINAL",
               "-e", "screen", tty, "9600",
               (char *)NULL);

        _exit(127);
    }

    ppa_log_printf("GUI: opened default UART terminal on %s at 9600 baud.\n",
                   tty);
    return true;
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
        char pty_line[256];
        if (backend->is_tty_enabled && backend->is_tty_enabled(backend->ctx)) {
            snprintf(pty_line, sizeof(pty_line), "External PTY: ON   %s",
                     backend->tty_name ? backend->tty_name(backend->ctx) : "");
        } else {
            snprintf(pty_line, sizeof(pty_line), "External PTY: OFF");
        }
        draw_text(r, fonts->small, 24, 184, muted, pty_line);
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
        1280, 820,
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
    SDL_SetWindowMinimumSize(window, 480, 320);

    SDL_Texture *panel_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        PANEL_W, PANEL_H);
    if (!panel_texture) {
        ppa_log_printf("GUI: failed to create panel render target: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        close_fonts(&fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(panel_texture, SDL_SCALEMODE_LINEAR);

    Uint32 main_window_id = SDL_GetWindowID(window);

    FileDialogState file_state = {0};
    if (initial_rom && *initial_rom) {
        snprintf(file_state.rom_path, sizeof(file_state.rom_path), "%s", initial_rom);
        file_state.load_requested = true;
    }

    const SDL_DialogFileFilter filters[] = {
        {"PPA-1 EEPROM image", "bin"},
        {"All files", "*"}
    };

    Preferences prefs = {
        .auto_run = false,
        .lamp_test = false,
        .high_contrast = false,
        .tty_requested = false
    };

    if (initial_tty_enabled && backend->set_tty_enabled) {
        backend->set_tty_enabled(backend->ctx, true);
    }
    if (backend->is_tty_enabled) {
        prefs.tty_requested = backend->is_tty_enabled(backend->ctx);
    }

    AuxWindow prefs_window = {0};
    AuxWindow view_window = {0};
    AuxWindow log_window = {0};

    bool switches[16] = {0};
    bool panel_running = true;
    uint16_t manual_address = 0x0000;

    const char *clock_values[] = {
        "1 Hz", "2 Hz", "5 Hz", "10 Hz",
        "20 Hz", "50 Hz", "100 Hz", "200 Hz",
        "500 Hz", "1 kHz", "2 kHz", "5 kHz",
        "10 kHz", "20 kHz", "50 kHz", "100 kHz"
    };
    const double clock_rates[] = {
        1.0, 2.0, 5.0, 10.0,
        20.0, 50.0, 100.0, 200.0,
        500.0, 1000.0, 2000.0, 5000.0,
        10000.0, 20000.0, 50000.0, 100000.0
    };
    const int clock_count = (int)(sizeof(clock_rates) / sizeof(clock_rates[0]));
    int clock_index = 9;

    /* Menu lives in real window coordinates and always spans the whole window. */
    SDL_FRect menu_open = {10, 5, 72, 28};
    SDL_FRect menu_pref = {86, 5, 112, 28};
    SDL_FRect menu_view = {202, 5, 72, 28};
    SDL_FRect menu_log  = {278, 5, 64, 28};
    SDL_FRect menu_terminal = {346, 5, 118, 28};

    /* All panel controls below use the fixed PANEL_W x PANEL_H design space. */
    /* Real front-panel programming row. */
    SDL_FRect fp_load_addr = {650, 548, 96, 38};
    SDL_FRect fp_addr_dec  = {754, 548, 76, 38};
    SDL_FRect fp_addr_inc  = {838, 548, 76, 38};
    SDL_FRect fp_exam      = {922, 548, 76, 38};
    SDL_FRect fp_dep       = {1006, 548, 86, 38};
    SDL_FRect fp_start     = {1100, 548, 70, 38};

    /* Processor execution row. */
    SDL_FRect op_cont  = {650, 596, 76, 40};
    SDL_FRect op_stop  = {734, 596, 76, 40};
    SDL_FRect op_step  = {818, 596, 76, 40};
    SDL_FRect op_ustep = {902, 596, 84, 40};
    SDL_FRect op_reset = {994, 596, 76, 40};

    SDL_FRect power_btn = {1184, 548, 48, 108};

    const float clock_cx = 838.0f, clock_cy = 406.0f, clock_radius = 38.0f;

    Uint64 last_ticks = SDL_GetTicks();
    double clock_phase = 0.0;
    ButtonId pressed_button = BTN_NONE;
    Uint64 pressed_until = 0;

    ppa_log_printf("GUI: fixed-layout PPA-1 front panel opened.\n");

    while (panel_running) {
        int rw = 1280, rh = 820;
        SDL_GetCurrentRenderOutputSize(renderer, &rw, &rh);

        const float menu_h = 38.0f;
        float available_h = (float)rh - menu_h;
        if (available_h < 1.0f) available_h = 1.0f;

        float scale_x = (float)rw / (float)PANEL_W;
        float scale_y = available_h / (float)PANEL_H;
        float panel_scale = scale_x < scale_y ? scale_x : scale_y;
        if (panel_scale < 0.01f) panel_scale = 0.01f;

        float panel_screen_w = (float)PANEL_W * panel_scale;
        float panel_screen_h = (float)PANEL_H * panel_scale;
        float panel_origin_x = ((float)rw - panel_screen_w) * 0.5f;
        float panel_origin_y = menu_h + (available_h - panel_screen_h) * 0.5f;

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
                }
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL &&
                event.wheel.windowID == main_window_id) {
                float mx = 0.0f, my = 0.0f;
                SDL_GetMouseState(&mx, &my);

                if (mx >= panel_origin_x && mx <= panel_origin_x + panel_screen_w &&
                    my >= panel_origin_y && my <= panel_origin_y + panel_screen_h) {
                    float px = (mx - panel_origin_x) / panel_scale;
                    float py = (my - panel_origin_y) / panel_scale;

                    if (point_in_circle(px, py, clock_cx, clock_cy, clock_radius + 18)) {
                        int old_clock_index = clock_index;

                        if (event.wheel.y > 0.0f) {
                            if (clock_index + 1 < clock_count) clock_index++;
                        } else if (event.wheel.y < 0.0f) {
                            if (clock_index > 0) clock_index--;
                        }

                        if (clock_index != old_clock_index) {
                            clock_phase = 0.0;
                            last_ticks = SDL_GetTicks();
                            ppa_log_printf("GUI: live CPU clock -> %s\n",
                                           clock_values[clock_index]);
                        }
                    }
                }
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                Uint32 id = event.button.windowID;

                if (id == main_window_id) {
                    SDL_ConvertEventToRenderCoordinates(renderer, &event);
                    float wx = event.button.x;
                    float wy = event.button.y;

                    if (event.button.button == SDL_BUTTON_LEFT) {
                        /* Window-level dynamic menu. */
                        if (point_in_rect(wx, wy, &menu_open) && !file_state.dialog_open) {
                            file_state.dialog_open = true;
                            SDL_ShowOpenFileDialog(
                                file_dialog_callback, &file_state, window,
                                filters, 2, NULL, false);
                            continue;
                        } else if (point_in_rect(wx, wy, &menu_pref)) {
                            open_aux(&prefs_window, AUX_PREFERENCES, "PPA-1 Preferences", 520, 330);
                            continue;
                        } else if (point_in_rect(wx, wy, &menu_view)) {
                            open_aux(&view_window, AUX_VIEW, "PPA-1 Processor View", 520, 330);
                            continue;
                        } else if (point_in_rect(wx, wy, &menu_log)) {
                            open_aux(&log_window, AUX_LOG, "PPA-1 Log", 760, 430);
                            continue;
                        } else if (point_in_rect(wx, wy, &menu_terminal)) {
                            (void)launch_uart_terminal(backend);
                            continue;
                        }

                        /* Convert window coordinates into the immutable front-panel design space. */
                        if (wx >= panel_origin_x && wx <= panel_origin_x + panel_screen_w &&
                            wy >= panel_origin_y && wy <= panel_origin_y + panel_screen_h) {
                            float x = (wx - panel_origin_x) / panel_scale;
                            float y = (wy - panel_origin_y) / panel_scale;

                            if (point_in_rect(x, y, &fp_load_addr)) {
                                pressed_button = BTN_LOAD_ADDR;
                                pressed_until = SDL_GetTicks() + 140;

                                uint16_t address_value = 0;
                                for (int i = 0; i < 16; i++) {
                                    if (switches[i]) address_value |= (uint16_t)(1u << i);
                                }
                                manual_address = address_value;
                                backend->load_address(backend->ctx, manual_address);

                                uint8_t data_value = 0;
                                for (int i = 0; i < 8; i++) {
                                    if (switches[i]) data_value |= (uint8_t)(1u << i);
                                }
                                backend->set_data_input(backend->ctx, data_value);
                            } else if (point_in_rect(x, y, &fp_addr_dec)) {
                                pressed_button = BTN_ADDR_DEC;
                                pressed_until = SDL_GetTicks() + 140;
                                manual_address--;
                                backend->load_address(backend->ctx, manual_address);
                            } else if (point_in_rect(x, y, &fp_addr_inc)) {
                                pressed_button = BTN_ADDR_INC;
                                pressed_until = SDL_GetTicks() + 140;
                                manual_address++;
                                backend->load_address(backend->ctx, manual_address);
                            } else if (point_in_rect(x, y, &fp_exam)) {
                                pressed_button = BTN_EXAM;
                                pressed_until = SDL_GetTicks() + 140;
                                (void)backend->examine(backend->ctx, manual_address);
                            } else if (point_in_rect(x, y, &fp_dep)) {
                                pressed_button = BTN_DEPOSIT;
                                pressed_until = SDL_GetTicks() + 140;

                                uint8_t data_value = 0;
                                for (int i = 0; i < 8; i++) {
                                    if (switches[i]) data_value |= (uint8_t)(1u << i);
                                }
                                backend->deposit(backend->ctx, manual_address, data_value);
                            } else if (point_in_rect(x, y, &fp_start)) {
                                pressed_button = BTN_START;
                                pressed_until = SDL_GetTicks() + 140;
                                backend->start_at(backend->ctx, manual_address);
                            } else if (point_in_rect(x, y, &op_cont)) {
                                pressed_button = BTN_CONT;
                                pressed_until = SDL_GetTicks() + 140;
                                backend->set_running(backend->ctx, true);
                            } else if (point_in_rect(x, y, &op_stop)) {
                                pressed_button = BTN_STOP;
                                pressed_until = SDL_GetTicks() + 140;
                                backend->set_running(backend->ctx, false);
                            } else if (point_in_rect(x, y, &op_step)) {
                                pressed_button = BTN_STEP;
                                pressed_until = SDL_GetTicks() + 140;
                                backend->step_instruction(backend->ctx);
                            } else if (point_in_rect(x, y, &op_ustep)) {
                                pressed_button = BTN_MICRO;
                                pressed_until = SDL_GetTicks() + 140;
                                backend->step_micro(backend->ctx);
                            } else if (point_in_rect(x, y, &op_reset)) {
                                pressed_button = BTN_RESET;
                                pressed_until = SDL_GetTicks() + 140;
                                backend->reset(backend->ctx);
                                manual_address = 0x0000;
                            } else if (point_in_rect(x, y, &power_btn)) {
                                backend->set_power(
                                    backend->ctx,
                                    !backend->is_powered(backend->ctx));
                            } else if (point_in_circle(x, y, clock_cx, clock_cy, clock_radius + 12)) {
                                int old_clock_index = clock_index;
                                if (x < clock_cx) {
                                    if (clock_index > 0) clock_index--;
                                } else {
                                    if (clock_index + 1 < clock_count) clock_index++;
                                }

                                if (clock_index != old_clock_index) {
                                    clock_phase = 0.0;
                                    last_ticks = SDL_GetTicks();
                                    ppa_log_printf("GUI: live CPU clock -> %s\n",
                                                   clock_values[clock_index]);
                                }
                            } else {
                                for (int i = 0; i < 16; i++) {
                                    SDL_FRect hit = {62.0f + i * 31.0f, 507.0f, 28.0f, 72.0f};
                                    if (point_in_rect(x, y, &hit)) {
                                        switches[i] = !switches[i];

                                        uint8_t data_value = 0;
                                        for (int bit = 0; bit < 8; bit++) {
                                            if (switches[bit]) data_value |= (uint8_t)(1u << bit);
                                        }
                                        backend->set_data_input(backend->ctx, data_value);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } else if (prefs_window.window &&
                           id == SDL_GetWindowID(prefs_window.window) &&
                           event.button.button == SDL_BUTTON_LEFT) {
                    float y = event.button.y;
                    if (y >= 90 && y < 118) prefs.high_contrast = !prefs.high_contrast;
                    else if (y >= 118 && y < 146) prefs.lamp_test = !prefs.lamp_test;
                    else if (y >= 146 && y < 174) prefs.auto_run = !prefs.auto_run;
                    else if (y >= 174 && y < 206) {
                        bool desired = !(backend->is_tty_enabled &&
                                         backend->is_tty_enabled(backend->ctx));
                        if (backend->set_tty_enabled &&
                            backend->set_tty_enabled(backend->ctx, desired)) {
                            prefs.tty_requested = backend->is_tty_enabled
                                ? backend->is_tty_enabled(backend->ctx)
                                : desired;
                        }
                    }
                }
            }
        }

        if (file_state.load_requested && file_state.rom_path[0]) {
            file_state.load_requested = false;
            if (backend->load_rom(backend->ctx, file_state.rom_path)) {
                manual_address = 0x0000;

                if (prefs.auto_run) {
                    backend->set_power(backend->ctx, true);
                    backend->set_running(backend->ctx, true);
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

        bool clock_level = false;

        if (loaded && powered && running && !halted) {
            double total_cycles = clock_phase + dt * clock_rates[clock_index];
            double whole_cycles = floor(total_cycles);
            int rising_edges = (int)whole_cycles;
            clock_phase = total_cycles - whole_cycles;

            if (rising_edges > 5000) rising_edges = 5000;
            if (rising_edges > 0) {
                backend->tick(backend->ctx, rising_edges);
            }

            clock_level = clock_phase < 0.5;
        } else {
            clock_phase = 0.0;
            clock_level = false;
        }

        loaded = backend->is_loaded(backend->ctx);
        powered = backend->is_powered(backend->ctx);
        running = backend->is_running(backend->ctx);
        halted = backend->is_halted(backend->ctx);

        if (!powered || !running || halted) {
            clock_level = false;
        }

        Uint16 address = backend->address_bus(backend->ctx);
        Uint8 data = backend->data_bus(backend->ctx);

        /* Render the complete hardware panel at its immutable design size. */
        SDL_SetRenderTarget(renderer, panel_texture);
        SDL_SetRenderViewport(renderer, NULL);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        SDL_SetRenderDrawColor(renderer, 17, 19, 19, 255);
        SDL_RenderClear(renderer);

        SDL_FRect face = {10, 8, 1260, 742};
        draw_bevel_panel(renderer, face,
            prefs.high_contrast
                ? (SDL_Color){45, 47, 46, 255}
                : (SDL_Color){37, 39, 38, 255},
            (SDL_Color){122, 123, 115, 255},
            (SDL_Color){10, 11, 11, 255});

        draw_screw(renderer, 24, 22);
        draw_screw(renderer, 1256, 22);
        draw_screw(renderer, 24, 736);
        draw_screw(renderer, 1256, 736);

        /* One continuous metal panel: only engraved separator lines, no modular cards. */
        SDL_FRect title_rect = {46, 26, 972, 52};
        draw_text_fit(renderer, fonts.title, title_rect,
                      (SDL_Color){239, 231, 204, 255},
                      "PROGRAMBLE PROCESSOR");

        SDL_FRect badge = {1040, 24, 186, 62};
        draw_bevel_panel(renderer, badge,
            (SDL_Color){91, 38, 30, 255},
            (SDL_Color){218, 202, 160, 255},
            (SDL_Color){34, 17, 16, 255});
        draw_text_center(renderer, fonts.badge, badge,
                         (SDL_Color){245, 232, 195, 255}, "A-1");
        draw_text_center(renderer, fonts.section,
            (SDL_FRect){1035, 88, 196, 26},
            (SDL_Color){224, 216, 191, 255}, "Adava Electronics");

        SDL_SetRenderDrawColor(renderer, 178, 168, 134, 255);
        SDL_RenderLine(renderer, 46, 104, 1228, 104);
        SDL_RenderLine(renderer, 620, 132, 620, 565);
        SDL_RenderLine(renderer, 46, 350, 592, 350);
        SDL_RenderLine(renderer, 646, 300, 1228, 300);
        SDL_RenderLine(renderer, 646, 500, 1228, 500);

        /* Left upper: bus lamps. */
        draw_text(renderer, fonts.section, 52, 126,
                  (SDL_Color){229, 221, 194, 255}, "BUS INDICATORS");
        draw_text(renderer, fonts.label, 54, 163,
                  (SDL_Color){212, 207, 185, 255}, "ADDRESS BUS  A0-A15");

        for (int i = 0; i < 16; i++) {
            char label[2] = {"0123456789ABCDEF"[i], 0};
            bool bit = prefs.lamp_test ||
                (powered && ((address >> i) & 1u));
            draw_led(renderer, &fonts,
                     58.0f + i * 31.0f, 190.0f, label, bit,
                     (SDL_Color){235, 154, 37, 255});
        }

        draw_text(renderer, fonts.label, 54, 257,
                  (SDL_Color){212, 207, 185, 255}, "DATA BUS  D0-D7");
        for (int i = 0; i < 8; i++) {
            char label[2] = {(char)('0' + i), 0};
            bool bit = prefs.lamp_test ||
                (powered && loaded && ((data >> i) & 1u));
            draw_led(renderer, &fonts,
                     58.0f + i * 49.0f, 284.0f, label, bit,
                     (SDL_Color){38, 207, 190, 255});
        }

        draw_text(renderer, fonts.label, 452, 257,
                  (SDL_Color){212, 207, 185, 255}, "CPU");
        draw_led(renderer, &fonts, 448, 284, "RDY",
                 prefs.lamp_test || (loaded && powered && !halted),
                 (SDL_Color){72, 220, 89, 255});
        draw_led(renderer, &fonts, 493, 284, "RUN",
                 prefs.lamp_test || (powered && running),
                 (SDL_Color){72, 220, 89, 255});
        draw_led(renderer, &fonts, 538, 284, "HLT",
                 prefs.lamp_test || (loaded && halted),
                 (SDL_Color){222, 58, 43, 255});

        /* Right upper: system status, no separate box. */
        draw_text(renderer, fonts.section, 646, 126,
                  (SDL_Color){229, 221, 194, 255}, "SYSTEM STATUS");

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
            clock_level,
            loaded,
            false
        };
        for (int i = 0; i < 7; i++) {
            draw_led(renderer, &fonts,
                     650.0f + i * 78.0f, 175.0f,
                     sys_labels[i],
                     prefs.lamp_test || sys_on[i],
                     sys_colors[i]);
        }

        /* Lower left: manual switch register. */
        draw_text(renderer, fonts.section, 52, 371,
                  (SDL_Color){229, 221, 194, 255}, "FRONT PANEL INPUT");
        draw_text(renderer, fonts.label, 54, 407,
                  (SDL_Color){212, 207, 185, 255},
                  "DATA D0-D7 / ADDRESS A0-A15");
        for (int i = 0; i < 16; i++) {
            char label[2] = {"0123456789ABCDEF"[i], 0};
            draw_toggle(renderer, &fonts,
                        62.0f + i * 31.0f, 475.0f, label, switches[i]);
        }
        /* Hardware clock control. */
        draw_text(renderer, fonts.section, 646, 321,
                  (SDL_Color){229, 221, 194, 255}, "CLOCK CONTROL");

        draw_text(renderer, fonts.label, 704, 360,
                  (SDL_Color){212, 207, 185, 255}, "CPU CLOCK");
        draw_knob(renderer, &fonts, clock_cx, clock_cy, clock_radius,
                  clock_index, clock_count, clock_values[clock_index]);

        /* Hardware programming and execution controls. */
        draw_text(renderer, fonts.section, 646, 520,
                  (SDL_Color){229, 221, 194, 255}, "PROGRAMMING & CONTROL");

        draw_push_button(renderer, &fonts, fp_load_addr, "LOAD ADDR",
                         (SDL_Color){126, 91, 38, 255}, false,
                         button_pressed(pressed_button, BTN_LOAD_ADDR, pressed_until));
        draw_push_button(renderer, &fonts, fp_addr_dec, "ADDR -",
                         (SDL_Color){99, 96, 86, 255}, false,
                         button_pressed(pressed_button, BTN_ADDR_DEC, pressed_until));
        draw_push_button(renderer, &fonts, fp_addr_inc, "ADDR +",
                         (SDL_Color){99, 96, 86, 255}, false,
                         button_pressed(pressed_button, BTN_ADDR_INC, pressed_until));
        draw_push_button(renderer, &fonts, fp_exam, "EXAM",
                         (SDL_Color){99, 96, 86, 255}, false,
                         button_pressed(pressed_button, BTN_EXAM, pressed_until));
        draw_push_button(renderer, &fonts, fp_dep, "DEPOSIT",
                         (SDL_Color){99, 96, 86, 255}, false,
                         button_pressed(pressed_button, BTN_DEPOSIT, pressed_until));
        draw_push_button(renderer, &fonts, fp_start, "START",
                         (SDL_Color){41, 112, 55, 255}, powered && running,
                         button_pressed(pressed_button, BTN_START, pressed_until));

        draw_push_button(renderer, &fonts, op_cont, "CONT",
                         (SDL_Color){41, 112, 55, 255}, powered && running,
                         button_pressed(pressed_button, BTN_CONT, pressed_until));
        draw_push_button(renderer, &fonts, op_stop, "STOP",
                         (SDL_Color){135, 45, 39, 255}, powered && !running,
                         button_pressed(pressed_button, BTN_STOP, pressed_until));
        draw_push_button(renderer, &fonts, op_step, "STEP",
                         (SDL_Color){99, 96, 86, 255}, false,
                         button_pressed(pressed_button, BTN_STEP, pressed_until));
        draw_push_button(renderer, &fonts, op_ustep, "MICRO",
                         (SDL_Color){99, 96, 86, 255}, false,
                         button_pressed(pressed_button, BTN_MICRO, pressed_until));
        draw_push_button(renderer, &fonts, op_reset, "RESET",
                         (SDL_Color){99, 96, 86, 255}, false,
                         button_pressed(pressed_button, BTN_RESET, pressed_until));

        /* PWR remains part of the same physical plate. */
        draw_text(renderer, fonts.label, 1188, 520,
                  (SDL_Color){219, 212, 190, 255}, "PWR");
        SDL_SetRenderDrawColor(renderer, 18, 18, 17, 255);
        SDL_RenderFillRect(renderer, &power_btn);
        SDL_SetRenderDrawColor(renderer, 136, 91, 53, 255);
        SDL_RenderRect(renderer, &power_btn);

        SDL_FRect rocker = {
            1195,
            powered ? 558.0f : 600.0f,
            28, 42
        };
        SDL_SetRenderDrawColor(renderer,
            powered ? 116 : 73,
            powered ? 43 : 30,
            powered ? 31 : 27, 255);
        SDL_RenderFillRect(renderer, &rocker);
        draw_text_center(renderer, fonts.label, rocker,
                         (SDL_Color){244, 231, 205, 255},
                         powered ? "I" : "O");

        /* Composite the fixed panel into the actual resizable window. */
        SDL_SetRenderTarget(renderer, NULL);
        SDL_SetRenderViewport(renderer, NULL);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);

        SDL_SetRenderDrawColor(renderer, 17, 19, 19, 255);
        SDL_RenderClear(renderer);

        SDL_FRect menu_bar = {0, 0, (float)rw, menu_h};
        SDL_SetRenderDrawColor(renderer, 34, 36, 36, 255);
        SDL_RenderFillRect(renderer, &menu_bar);
        draw_menu_button(renderer, &fonts, menu_open, "OPEN");
        draw_menu_button(renderer, &fonts, menu_pref, "PREFERENCES");
        draw_menu_button(renderer, &fonts, menu_view, "VIEW");
        draw_menu_button(renderer, &fonts, menu_log, "LOG");
        draw_menu_button(renderer, &fonts, menu_terminal, "UART MONITOR");

        SDL_FRect panel_dst = {
            panel_origin_x, panel_origin_y,
            panel_screen_w, panel_screen_h
        };
        SDL_RenderTexture(renderer, panel_texture, NULL, &panel_dst);
        SDL_RenderPresent(renderer);

        draw_aux_window(&prefs_window, &fonts, &prefs, backend);
        draw_aux_window(&view_window, &fonts, &prefs, backend);
        draw_aux_window(&log_window, &fonts, &prefs, backend);
    }

    backend->set_running(backend->ctx, false);

    destroy_aux(&prefs_window);
    destroy_aux(&view_window);
    destroy_aux(&log_window);
    SDL_DestroyTexture(panel_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    close_fonts(&fonts);
    TTF_Quit();
    SDL_Quit();

    ppa_log_printf("GUI: front panel closed.\n");
    return 0;
}
