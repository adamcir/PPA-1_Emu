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

static const SDL_Color COL_BLACK      = {10, 11, 11, 255};
static const SDL_Color COL_DARK_GRAY  = {39, 41, 40, 255};
static const SDL_Color COL_GRAY       = {101, 103, 100, 255};
static const SDL_Color COL_LIGHT_GRAY = {174, 177, 171, 255};
static const SDL_Color COL_WHITE      = {238, 240, 234, 255};
static const SDL_Color COL_ORANGE     = {229, 145, 34, 255};
static const SDL_Color COL_GREEN      = {126, 226, 112, 255};
static const SDL_Color COL_RED        = {205, 67, 58, 255};
static const SDL_Color COL_BLUE       = {99, 199, 221, 255};

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
    TTF_Font *mono;
} PanelFonts;

static float g_text_pixel_scale = 1.0f;

typedef enum {
    AUX_NONE = 0,
    AUX_PREFERENCES,
    AUX_VIEW,
    AUX_LOG,
    AUX_CREDITS
} AuxKind;

#define TERM_LINES 128
#define TERM_COLS 96

typedef enum {
    BTN_NONE = 0,
    BTN_LOAD_BANK,
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
    bool tty_requested;
} Preferences;

static const SDL_FRect PREF_LAMP_BUTTON = {24, 78, 512, 62};
static const SDL_FRect PREF_AUTORUN_BUTTON = {24, 150, 512, 62};
static const SDL_FRect PREF_PTY_BUTTON = {24, 222, 512, 62};

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

static const char *find_mono_font(void) {
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        if (SDL_GetPathInfo(paths[i], NULL)) return paths[i];
    }
    return NULL;
}

static bool load_fonts_scaled(PanelFonts *f, float scale) {
    const char *regular = find_regular_font();
    const char *bold = find_bold_font();
    const char *mono = find_mono_font();

    if (!regular || !bold || !mono) {
        ppa_log_printf("GUI: no suitable technical TrueType font found.\n");
        return false;
    }

    if (scale < 1.0f) scale = 1.0f;

    /* The front-panel model name is intentionally large and heavy.
       Re-open it at the panel supersampling scale so it is never just
       a low-resolution bitmap stretched with the window. */
    f->title   = TTF_OpenFont(bold, 46.0f * scale);
    f->badge   = TTF_OpenFont(bold, 46.0f * scale);
    f->section = TTF_OpenFont(bold, 17.0f * scale);
    f->label   = TTF_OpenFont(bold, 13.5f * scale);
    f->small   = TTF_OpenFont(regular, 11.5f * scale);
    f->mono    = TTF_OpenFont(mono, 12.0f * scale);

    return f->title && f->badge && f->section && f->label &&
           f->small && f->mono;
}

static bool load_fonts(PanelFonts *f) {
    return load_fonts_scaled(f, 1.0f);
}

static void close_fonts(PanelFonts *f) {
    if (f->title) TTF_CloseFont(f->title);
    if (f->badge) TTF_CloseFont(f->badge);
    if (f->section) TTF_CloseFont(f->section);
    if (f->label) TTF_CloseFont(f->label);
    if (f->small) TTF_CloseFont(f->small);
    if (f->mono) TTF_CloseFont(f->mono);
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
    if (texture) {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    }
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

    float logical_w = (float)w / g_text_pixel_scale;
    float logical_h = (float)h / g_text_pixel_scale;
    SDL_FRect dst = {x, y, logical_w, logical_h};
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

    float logical_w = (float)w / g_text_pixel_scale;
    float logical_h = (float)h / g_text_pixel_scale;
    SDL_FRect dst = {
        rect.x + (rect.w - logical_w) * 0.5f,
        rect.y + (rect.h - logical_h) * 0.5f,
        logical_w, logical_h
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

    float logical_w = (float)w / g_text_pixel_scale;
    float logical_h = (float)h / g_text_pixel_scale;
    float scale_x = rect.w / logical_w;
    float scale_y = rect.h / logical_h;
    float fit = scale_x < scale_y ? scale_x : scale_y;
    if (fit > 1.0f) fit = 1.0f;

    SDL_FRect dst = {
        rect.x + (rect.w - logical_w * fit) * 0.5f,
        rect.y + (rect.h - logical_h * fit) * 0.5f,
        logical_w * fit,
        logical_h * fit
    };
    SDL_RenderTexture(r, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static void draw_text_stretched(SDL_Renderer *r, TTF_Font *font,
                                SDL_FRect rect, SDL_Color color,
                                const char *text)
{
    int w = 0, h = 0;
    SDL_Texture *texture = make_text_texture(r, font, color, text, &w, &h);
    if (!texture || w <= 0 || h <= 0) {
        if (texture) SDL_DestroyTexture(texture);
        return;
    }

    /*
     * The glyphs were rasterized at the panel supersampling scale, so this
     * horizontal expansion still comes from a high-resolution source rather
     * than from the final low-resolution panel image.
     */
    float logical_h = (float)h / g_text_pixel_scale;
    float dst_h = rect.h;
    if (dst_h > logical_h) dst_h = logical_h;

    SDL_FRect dst = {
        rect.x,
        rect.y + (rect.h - dst_h) * 0.5f,
        rect.w,
        dst_h
    };

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
                         SDL_FRect rect, const char *title)
{
    SDL_Color fill = {39, 41, 40, 255};

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

static void draw_labeled_knob(SDL_Renderer *r, PanelFonts *fonts,
                              float cx, float cy, float radius,
                              int index, int count,
                              const char * const *labels,
                              bool enabled)
{
    SDL_Color tick_color = enabled ? COL_WHITE : COL_GRAY;
    SDL_Color label_color = enabled ? COL_WHITE : COL_GRAY;

    for (int i = 0; i < count; i++) {
        float t = count > 1 ? (float)i / (float)(count - 1) : 0.0f;
        float angle = (225.0f - t * 270.0f) *
                      3.14159265358979323846f / 180.0f;

        float ix = cx + cosf(angle) * (radius + 6.0f);
        float iy = cy - sinf(angle) * (radius + 6.0f);
        float ox = cx + cosf(angle) * (radius + 13.0f);
        float oy = cy - sinf(angle) * (radius + 13.0f);

        SDL_SetRenderDrawColor(r,
            tick_color.r, tick_color.g, tick_color.b, tick_color.a);
        SDL_RenderLine(r, ix, iy, ox, oy);

        if (labels && labels[i]) {
            float lx = cx + cosf(angle) * (radius + 27.0f);
            float ly = cy - sinf(angle) * (radius + 27.0f);

            SDL_FRect lr = {lx - 22.0f, ly - 8.0f, 44.0f, 16.0f};
            draw_text_center(r, fonts->small, lr, label_color, labels[i]);
        }
    }

    SDL_SetRenderDrawColor(
        r, COL_BLACK.r, COL_BLACK.g, COL_BLACK.b, 255);
    fill_circle(r, cx, cy, radius + 4.0f);

    SDL_Color body = enabled ? COL_GRAY : COL_DARK_GRAY;
    SDL_SetRenderDrawColor(r, body.r, body.g, body.b, 255);
    fill_circle(r, cx, cy, radius);

    SDL_SetRenderDrawColor(
        r, COL_DARK_GRAY.r, COL_DARK_GRAY.g, COL_DARK_GRAY.b, 255);
    fill_circle(r, cx, cy, radius - 7.0f);

    float t = count > 1 ? (float)index / (float)(count - 1) : 0.0f;
    float angle = (225.0f - t * 270.0f) *
                  3.14159265358979323846f / 180.0f;
    float px = cx + cosf(angle) * radius * 0.68f;
    float py = cy - sinf(angle) * radius * 0.68f;

    SDL_SetRenderDrawColor(r,
        tick_color.r, tick_color.g, tick_color.b, 255);
    SDL_RenderLine(r, cx, cy, px, py);
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

static void draw_aux_header(SDL_Renderer *r, PanelFonts *fonts,
                            const char *title, const char *subtitle)
{
    int w = 560, h = 380;
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    (void)h;

    draw_text(r, fonts->section, 24, 18,
              (SDL_Color){238, 240, 234, 255}, title);

    if (subtitle && *subtitle) {
        draw_text(r, fonts->small, 24, 42,
                  (SDL_Color){151, 156, 151, 255}, subtitle);
    }

    SDL_SetRenderDrawColor(r, 73, 77, 74, 255);
    SDL_RenderLine(r, 24, 64, (float)w - 24.0f, 64);
}

static void draw_aux_toggle_button(SDL_Renderer *r, PanelFonts *fonts,
                                   SDL_FRect rect,
                                   const char *title,
                                   const char *description,
                                   bool enabled,
                                   SDL_Color accent)
{
    SDL_Color fill = enabled
        ? (SDL_Color){42, 47, 44, 255}
        : (SDL_Color){34, 36, 36, 255};
    SDL_Color border = enabled
        ? accent
        : (SDL_Color){78, 81, 78, 255};

    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, 255);
    SDL_RenderFillRect(r, &rect);

    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, 255);
    SDL_RenderRect(r, &rect);

    SDL_FRect inner = {
        rect.x + 1.0f, rect.y + 1.0f,
        rect.w - 2.0f, rect.h - 2.0f
    };
    SDL_RenderRect(r, &inner);

    draw_text(r, fonts->label,
              rect.x + 16.0f, rect.y + 9.0f,
              (SDL_Color){238, 240, 234, 255}, title);

    draw_text(r, fonts->small,
              rect.x + 16.0f, rect.y + 34.0f,
              (SDL_Color){164, 169, 164, 255}, description);

    SDL_FRect state = {
        rect.x + rect.w - 70.0f,
        rect.y + 16.0f,
        52.0f, 30.0f
    };

    SDL_Color state_fill = enabled
        ? accent
        : (SDL_Color){52, 54, 53, 255};

    SDL_SetRenderDrawColor(r,
        state_fill.r, state_fill.g, state_fill.b, 255);
    SDL_RenderFillRect(r, &state);

    SDL_SetRenderDrawColor(r,
        enabled ? 225 : 91,
        enabled ? 226 : 94,
        enabled ? 218 : 91, 255);
    SDL_RenderRect(r, &state);

    draw_text_center(r, fonts->label, state,
                     (SDL_Color){247, 247, 240, 255},
                     enabled ? "ON" : "OFF");
}

static void read_log_tail(char lines[][160], int *line_count, int max_lines) {
    *line_count = 0;
    if (max_lines < 1) return;
    if (max_lines > 64) max_lines = 64;

    FILE *f = fopen("latest.log", "r");
    if (!f) return;

    char ring[64][160] = {{0}};
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

    SDL_SetWindowMinimumSize(term->window, 480, 280);

    term->renderer = SDL_CreateRenderer(term->window, NULL);
    if (!term->renderer) {
        SDL_DestroyWindow(term->window);
        memset(term, 0, sizeof(*term));
        return false;
    }

    SDL_SetRenderVSync(term->renderer, 1);
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

    SDL_SetRenderDrawColor(term->renderer, 28, 30, 30, 255);
    SDL_RenderClear(term->renderer);

    draw_aux_header(term->renderer, fonts,
                    "UART MONITOR",
                    "FF00 TX / FF01 RX");

    /*
     * Terminal body stays strictly monospaced so character columns align,
     * while the surrounding window uses the same visual language as LOG,
     * VIEW, PREFERENCES and CREDITS.
     */
    int visible = (h - 94) / 18;
    if (visible < 1) visible = 1;
    if (visible > TERM_LINES) visible = TERM_LINES;

    int start = term->line - visible + 1;
    if (start < 0) start = 0;

    SDL_FRect body = {
        24.0f, 78.0f,
        (float)w - 48.0f,
        (float)h - 96.0f
    };
    if (body.w < 1.0f) body.w = 1.0f;
    if (body.h < 1.0f) body.h = 1.0f;

    SDL_SetRenderDrawColor(term->renderer, 22, 24, 24, 255);
    SDL_RenderFillRect(term->renderer, &body);
    SDL_SetRenderDrawColor(term->renderer, 63, 67, 64, 255);
    SDL_RenderRect(term->renderer, &body);

    float y = 88.0f;
    for (int line = start; line <= term->line && line < TERM_LINES; line++) {
        draw_text(term->renderer, fonts->mono, 36, y,
                  (SDL_Color){201, 230, 208, 255},
                  term->lines[line]);
        y += 18.0f;
    }

    SDL_RenderPresent(term->renderer);
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

    Uint64 flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (kind == AUX_LOG) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    aux->window = SDL_CreateWindow(title, w, h, flags);
    if (!aux->window) return;

    if (kind == AUX_LOG) {
        SDL_SetWindowMinimumSize(aux->window, 560, 300);
    }

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
        draw_aux_header(r, fonts, "PREFERENCES",
                        "EMULATOR & FRONT PANEL");

        bool pty_on = backend->is_tty_enabled &&
                      backend->is_tty_enabled(backend->ctx);

        draw_aux_toggle_button(
            r, fonts, PREF_LAMP_BUTTON,
            "LAMP TEST",
            "Light every front-panel indicator for a quick hardware check.",
            prefs->lamp_test, COL_GREEN);

        draw_aux_toggle_button(
            r, fonts, PREF_AUTORUN_BUTTON,
            "AUTO RUN AFTER LOAD",
            "Power the processor and start execution after loading EEPROM.",
            prefs->auto_run, COL_ORANGE);

        draw_aux_toggle_button(
            r, fonts, PREF_PTY_BUTTON,
            "EXTERNAL PTY",
            "Expose FF00-FF03 UART through a Linux pseudo-terminal.",
            pty_on, COL_BLUE);

        char pty_line[256];
        if (pty_on && backend->tty_name) {
            snprintf(pty_line, sizeof(pty_line),
                     "ACTIVE DEVICE: %s",
                     backend->tty_name(backend->ctx));
        } else {
            snprintf(pty_line, sizeof(pty_line),
                     "No external serial device active.");
        }

        draw_text(r, fonts->mono, 24, 302,
                  pty_on ? COL_BLUE : muted, pty_line);
        draw_text(r, fonts->small, 24, 329, muted,
                  "Changes apply immediately.");

    } else if (aux->kind == AUX_VIEW) {
        draw_aux_header(r, fonts, "PROCESSOR VIEW",
                        "LIVE FRONT-PANEL TELEMETRY");
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
        draw_aux_header(r, fonts, "LATEST.LOG",
                        "LIVE EMULATOR TRACE");

        int rw = 760, rh = 430;
        SDL_GetCurrentRenderOutputSize(r, &rw, &rh);
        (void)rw;

        int visible_lines = (rh - 88) / 20;
        if (visible_lines < 1) visible_lines = 1;
        if (visible_lines > 64) visible_lines = 64;

        char lines[64][160];
        int count = 0;
        read_log_tail(lines, &count, visible_lines);

        float y = 80.0f;
        for (int i = 0; i < count; i++, y += 20.0f) {
            draw_text(r, fonts->small, 24, y, muted, lines[i]);
        }
    } else if (aux->kind == AUX_CREDITS) {
        draw_aux_header(r, fonts, "CREDITS",
                        "PROGRAMBLE PROCESSOR A-1");

        SDL_FRect badge = {24, 88, 184, 76};
        SDL_SetRenderDrawColor(r, 28, 30, 30, 255);
        SDL_RenderFillRect(r, &badge);
        SDL_SetRenderDrawColor(r, 238, 240, 234, 255);
        SDL_RenderRect(r, &badge);
        SDL_FRect badge_inner = {
            badge.x + 2.0f, badge.y + 2.0f,
            badge.w - 4.0f, badge.h - 4.0f
        };
        SDL_RenderRect(r, &badge_inner);
        draw_text_stretched(r, fonts->badge,
                            (SDL_FRect){34, 94, 164, 62},
                            COL_WHITE, "A-1");

        draw_text(r, fonts->small, 236, 88, muted,
                  "DESIGNED & DEVELOPED BY");
        draw_text(r, fonts->section, 236, 112, cream,
                  "Adam Cír (Adava)");
        draw_text(r, fonts->small, 236, 142, muted,
                  "Adava Electronics / Adava Development");

        SDL_SetRenderDrawColor(r, 73, 77, 74, 255);
        SDL_RenderLine(r, 24, 188, 536, 188);

        draw_text(r, fonts->label, 24, 210, cream,
                  "PPA-1 EMULATOR");
        draw_text(r, fonts->small, 24, 238, muted,
                  "Original 8-bit processor architecture and emulator.");
        draw_text(r, fonts->small, 24, 264, muted,
                  "Frontend: SDL3 + SDL3_ttf");
        draw_text(r, fonts->small, 24, 290, muted,
                  "(c) 2026 Adam Cír (Adava)");

        SDL_FRect footer = {24, 326, 512, 1};
        SDL_SetRenderDrawColor(r, 73, 77, 74, 255);
        SDL_RenderFillRect(r, &footer);
        draw_text_center(r, fonts->small,
                         (SDL_FRect){24, 338, 512, 20},
                         (SDL_Color){126, 226, 112, 255},
                         "PROGRAMBLE PROCESSOR A-1");
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

    PanelFonts panel_fonts = {0};
    if (!load_fonts_scaled(&panel_fonts, 1.0f)) {
        ppa_log_printf("GUI: panel font initialization failed.\n");
        close_fonts(&fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "PROGRAMBLE PROCESSOR A-1",
        1280, 820,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        close_fonts(&panel_fonts);
        close_fonts(&fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        SDL_DestroyWindow(window);
        close_fonts(&panel_fonts);
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
        close_fonts(&panel_fonts);
        close_fonts(&fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(panel_texture, SDL_SCALEMODE_LINEAR);
    float panel_render_scale = 1.0f;

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
    AuxWindow credits_window = {0};
    TerminalWindow uart_terminal = {0};

    bool switches[16] = {0};
    bool panel_running = true;
    uint16_t manual_address = 0x0000;
    uint8_t front_data_latch = 0x00;
    bool exam_held = false;
    uint8_t exam_data = 0x00;

    const char *clock_values[] = {
        "1 Hz", "2 Hz", "5 Hz", "10 Hz",
        "20 Hz", "50 Hz", "100 Hz", "200 Hz",
        "500 Hz", "1 kHz", "2 kHz", "5 kHz",
        "10 kHz", "20 kHz", "50 kHz", "100 kHz"
    };
    const char *clock_tick_labels[] = {
        "1", NULL, NULL, "10",
        NULL, NULL, "100", NULL,
        NULL, "1k", NULL, NULL,
        "10k", NULL, NULL, "100k"
    };
    const double clock_rates[] = {
        1.0, 2.0, 5.0, 10.0,
        20.0, 50.0, 100.0, 200.0,
        500.0, 1000.0, 2000.0, 5000.0,
        10000.0, 20000.0, 50000.0, 100000.0
    };
    const int clock_count = (int)(sizeof(clock_rates) / sizeof(clock_rates[0]));
    int clock_index = 9;

    const char *data_source_labels[] = {"DATA BUS", "REGISTER OUTPUT"};
    const char *data_source_tick_labels[] = {"BUS", "REG OUT"};
    int data_source_index = 0;

    const char *register_labels[] = {
        "A", "B", "C", "D", "XH", "XL", "YH", "YL"
    };
    const int register_count =
        (int)(sizeof(register_labels) / sizeof(register_labels[0]));
    int register_index = 0;

    /* Menu lives in real window coordinates and always spans the whole window. */
    SDL_FRect menu_open = {10, 5, 72, 28};
    SDL_FRect menu_pref = {86, 5, 112, 28};
    SDL_FRect menu_view = {202, 5, 72, 28};
    SDL_FRect menu_log  = {278, 5, 64, 28};
    SDL_FRect menu_terminal = {346, 5, 118, 28};
    SDL_FRect menu_credits = {468, 5, 86, 28};

    /* All panel controls below use the fixed PANEL_W x PANEL_H design space. */
    /* Front-panel bank/address/memory programming row. */
    SDL_FRect fp_load_bank = {650, 548, 96, 38};
    SDL_FRect fp_load_addr = {754, 548, 96, 38};
    SDL_FRect fp_addr_dec  = {858, 548, 68, 38};
    SDL_FRect fp_addr_inc  = {934, 548, 68, 38};
    SDL_FRect fp_exam      = {1010, 548, 68, 38};
    SDL_FRect fp_dep       = {1086, 548, 78, 38};

    /* Processor execution row. */
    SDL_FRect fp_start = {650, 596, 76, 40};
    SDL_FRect op_cont  = {734, 596, 76, 40};
    SDL_FRect op_stop  = {818, 596, 76, 40};
    SDL_FRect op_step  = {902, 596, 76, 40};
    SDL_FRect op_ustep = {986, 596, 84, 40};
    SDL_FRect op_reset = {1078, 596, 86, 40};

    SDL_FRect power_btn = {1184, 548, 48, 108};

    const float clock_cx = 720.0f, clock_cy = 428.0f, clock_radius = 29.0f;
    const float data_source_cx = 905.0f, data_source_cy = 428.0f;
    const float data_source_radius = 29.0f;
    const float register_cx = 1095.0f, register_cy = 428.0f;
    const float register_radius = 29.0f;

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

        /*
         * Render the physical panel at or above its visible pixel density.
         * The design coordinate system remains 1280x760, but the target and
         * fonts are supersampled when the window is enlarged.
         */
        float desired_render_scale = panel_scale > 1.0f
            ? ceilf(panel_scale * 4.0f) / 4.0f
            : 1.0f;
        if (desired_render_scale > 4.0f) desired_render_scale = 4.0f;

        if (fabsf(desired_render_scale - panel_render_scale) > 0.01f) {
            int target_w = (int)ceilf((float)PANEL_W * desired_render_scale);
            int target_h = (int)ceilf((float)PANEL_H * desired_render_scale);

            SDL_Texture *new_panel_texture = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_RGBA8888,
                SDL_TEXTUREACCESS_TARGET,
                target_w, target_h);

            PanelFonts new_panel_fonts = {0};
            bool fonts_ok = load_fonts_scaled(
                &new_panel_fonts, desired_render_scale);

            if (new_panel_texture && fonts_ok) {
                SDL_SetTextureScaleMode(
                    new_panel_texture, SDL_SCALEMODE_LINEAR);

                SDL_DestroyTexture(panel_texture);
                panel_texture = new_panel_texture;

                close_fonts(&panel_fonts);
                panel_fonts = new_panel_fonts;
                panel_render_scale = desired_render_scale;
            } else {
                if (new_panel_texture) SDL_DestroyTexture(new_panel_texture);
                close_fonts(&new_panel_fonts);
            }
        }

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
                } else if (credits_window.window &&
                           id == SDL_GetWindowID(credits_window.window)) {
                    destroy_aux(&credits_window);
                } else if (uart_terminal.window &&
                           id == SDL_GetWindowID(uart_terminal.window)) {
                    close_terminal(&uart_terminal, backend);
                    ppa_log_printf("GUI: built-in UART terminal window closed.\n");
                }
                continue;
            }

            if (uart_terminal.window &&
                event.type == SDL_EVENT_TEXT_INPUT &&
                event.text.windowID == SDL_GetWindowID(uart_terminal.window)) {
                if (backend->terminal_write_rx) {
                    const char *p = event.text.text;
                    while (p && *p) {
                        backend->terminal_write_rx(
                            backend->ctx, (uint8_t)(unsigned char)*p);
                        p++;
                    }
                }
                continue;
            }

            if (uart_terminal.window &&
                event.type == SDL_EVENT_KEY_DOWN &&
                event.key.windowID == SDL_GetWindowID(uart_terminal.window)) {
                if (backend->terminal_write_rx) {
                    uint8_t ch = 0;
                    bool send = true;

                    switch (event.key.key) {
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            ch = '\r';
                            break;
                        case SDLK_BACKSPACE:
                            ch = 0x08;
                            break;
                        case SDLK_TAB:
                            ch = '\t';
                            break;
                        case SDLK_ESCAPE:
                            ch = 0x1B;
                            break;
                        default:
                            send = false;
                            break;
                    }

                    if (send) {
                        backend->terminal_write_rx(backend->ctx, ch);
                    }
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
                    int dir = event.wheel.y > 0.0f ? 1 :
                              event.wheel.y < 0.0f ? -1 : 0;

                    if (dir != 0 &&
                        point_in_circle(px, py, clock_cx, clock_cy,
                                        clock_radius + 22.0f)) {
                        int old = clock_index;
                        clock_index += dir;
                        if (clock_index < 0) clock_index = 0;
                        if (clock_index >= clock_count) clock_index = clock_count - 1;

                        if (clock_index != old) {
                            clock_phase = 0.0;
                            last_ticks = SDL_GetTicks();
                            ppa_log_printf("GUI: live CPU clock -> %s\n",
                                           clock_values[clock_index]);
                        }
                    } else if (dir != 0 &&
                               point_in_circle(px, py,
                                               data_source_cx, data_source_cy,
                                               data_source_radius + 22.0f)) {
                        data_source_index += dir;
                        if (data_source_index < 0) data_source_index = 0;
                        if (data_source_index > 1) data_source_index = 1;

                        ppa_log_printf("GUI: DATA display source -> %s\n",
                                       data_source_labels[data_source_index]);
                    } else if (dir != 0 &&
                               point_in_circle(px, py,
                                               register_cx, register_cy,
                                               register_radius + 26.0f)) {
                        register_index += dir;
                        if (register_index < 0) register_index = 0;
                        if (register_index >= register_count)
                            register_index = register_count - 1;

                        ppa_log_printf("GUI: register selector -> %s%s\n",
                                       register_labels[register_index],
                                       data_source_index == 1
                                           ? ""
                                           : " (not routed to DATA)");
                    }
                }
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                event.button.windowID == main_window_id &&
                event.button.button == SDL_BUTTON_LEFT) {
                if (exam_held) {
                    exam_held = false;

                    uint8_t data_value = 0;
                    for (int bit = 0; bit < 8; bit++) {
                        if (switches[bit])
                            data_value |= (uint8_t)(1u << bit);
                    }

                    front_data_latch = data_value;
                    backend->set_data_input(backend->ctx, data_value);

                    if (pressed_button == BTN_EXAM)
                        pressed_button = BTN_NONE;
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
                            open_aux(&prefs_window, AUX_PREFERENCES,
                                     "PPA-1 Preferences", 560, 380);
                            continue;
                        } else if (point_in_rect(wx, wy, &menu_view)) {
                            open_aux(&view_window, AUX_VIEW, "PPA-1 Processor View", 520, 330);
                            continue;
                        } else if (point_in_rect(wx, wy, &menu_log)) {
                            open_aux(&log_window, AUX_LOG, "PPA-1 Log", 760, 430);
                            continue;
                        } else if (point_in_rect(wx, wy, &menu_credits)) {
                            open_aux(&credits_window, AUX_CREDITS,
                                     "PPA-1 Credits", 560, 390);
                            continue;
                        } else if (point_in_rect(wx, wy, &menu_terminal)) {
                            if (uart_terminal.window) {
                                close_terminal(&uart_terminal, backend);
                                ppa_log_printf(
                                    "GUI: built-in UART terminal closed by UART MONITOR toggle.\n");
                            } else if (open_terminal(&uart_terminal, backend)) {
                                ppa_log_printf(
                                    "GUI: built-in UART terminal opened.\n");
                            } else {
                                ppa_log_printf(
                                    "GUI: failed to open built-in UART terminal.\n");
                            }
                            continue;
                        }

                        /* Convert window coordinates into the immutable front-panel design space. */
                        if (wx >= panel_origin_x && wx <= panel_origin_x + panel_screen_w &&
                            wy >= panel_origin_y && wy <= panel_origin_y + panel_screen_h) {
                            float x = (wx - panel_origin_x) / panel_scale;
                            float y = (wy - panel_origin_y) / panel_scale;

                            if (point_in_rect(x, y, &fp_load_bank)) {
                                pressed_button = BTN_LOAD_BANK;
                                pressed_until = SDL_GetTicks() + 140;

                                uint16_t bank_value = 0;
                                for (int bit = 0; bit < 16; bit++) {
                                    if (switches[bit])
                                        bank_value |= (uint16_t)(1u << bit);
                                }
                                if (backend->load_bank) {
                                    backend->load_bank(
                                        backend->ctx, bank_value);
                                }
                            } else if (point_in_rect(x, y, &fp_load_addr)) {
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
                                exam_data =
                                    backend->examine(backend->ctx, manual_address);
                                exam_held = true;
                            } else if (point_in_rect(x, y, &fp_dep)) {
                                pressed_button = BTN_DEPOSIT;
                                pressed_until = SDL_GetTicks() + 140;

                                uint8_t data_value = 0;
                                for (int i = 0; i < 8; i++) {
                                    if (switches[i]) data_value |= (uint8_t)(1u << i);
                                }
                                backend->deposit(
                                    backend->ctx, manual_address, data_value);
                                front_data_latch = data_value;
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
                                front_data_latch = 0x00;
                                exam_held = false;
                                exam_data = 0x00;
                            } else if (point_in_rect(x, y, &power_btn)) {
                                bool new_power =
                                    !backend->is_powered(backend->ctx);
                                backend->set_power(backend->ctx, new_power);

                                if (!new_power) {
                                    manual_address = 0x0000;
                                    front_data_latch = 0x00;
                                    exam_held = false;
                                    exam_data = 0x00;
                                    clock_phase = 0.0;
                                } else {
                                    uint8_t data_value = 0;
                                    for (int bit = 0; bit < 8; bit++) {
                                        if (switches[bit])
                                            data_value |= (uint8_t)(1u << bit);
                                    }
                                    backend->set_data_input(
                                        backend->ctx, data_value);
                                    front_data_latch = data_value;
                                }
                            } else if (point_in_circle(
                                           x, y,
                                           clock_cx, clock_cy,
                                           clock_radius + 18.0f)) {
                                int old = clock_index;
                                if (x < clock_cx) clock_index--;
                                else clock_index++;

                                if (clock_index < 0) clock_index = 0;
                                if (clock_index >= clock_count)
                                    clock_index = clock_count - 1;

                                if (clock_index != old) {
                                    clock_phase = 0.0;
                                    last_ticks = SDL_GetTicks();
                                    ppa_log_printf(
                                        "GUI: live CPU clock -> %s\n",
                                        clock_values[clock_index]);
                                }
                            } else if (point_in_circle(
                                           x, y,
                                           data_source_cx, data_source_cy,
                                           data_source_radius + 18.0f)) {
                                if (x < data_source_cx)
                                    data_source_index--;
                                else
                                    data_source_index++;

                                if (data_source_index < 0)
                                    data_source_index = 0;
                                if (data_source_index > 1)
                                    data_source_index = 1;

                                ppa_log_printf(
                                    "GUI: DATA display source -> %s\n",
                                    data_source_labels[data_source_index]);
                            } else if (point_in_circle(
                                           x, y,
                                           register_cx, register_cy,
                                           register_radius + 22.0f)) {
                                if (x < register_cx)
                                    register_index--;
                                else
                                    register_index++;

                                if (register_index < 0)
                                    register_index = 0;
                                if (register_index >= register_count)
                                    register_index = register_count - 1;

                                ppa_log_printf(
                                    "GUI: register selector -> %s%s\n",
                                    register_labels[register_index],
                                    data_source_index == 1
                                        ? ""
                                        : " (not routed to DATA)");
                            } else {
                                for (int pos = 0; pos < 16; pos++) {
                                    float nibble_gap =
                                        (float)(pos / 4) * 10.0f;
                                    SDL_FRect hit = {
                                        62.0f + pos * 31.0f + nibble_gap,
                                        507.0f, 28.0f, 72.0f
                                    };
                                    if (point_in_rect(x, y, &hit)) {
                                        int bit_index = 15 - pos;
                                        switches[bit_index] =
                                            !switches[bit_index];

                                        uint8_t data_value = 0;
                                        for (int bit = 0; bit < 8; bit++) {
                                            if (switches[bit])
                                                data_value |=
                                                    (uint8_t)(1u << bit);
                                        }
                                        backend->set_data_input(
                                            backend->ctx, data_value);
                                        front_data_latch = data_value;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } else if (prefs_window.window &&
                           id == SDL_GetWindowID(prefs_window.window) &&
                           event.button.button == SDL_BUTTON_LEFT) {
                    float x = event.button.x;
                    float y = event.button.y;

                    if (point_in_rect(x, y, &PREF_LAMP_BUTTON)) {
                        prefs.lamp_test = !prefs.lamp_test;
                        ppa_log_printf("GUI: Lamp test -> %s\n",
                                       prefs.lamp_test ? "ON" : "OFF");
                    } else if (point_in_rect(
                                   x, y, &PREF_AUTORUN_BUTTON)) {
                        prefs.auto_run = !prefs.auto_run;
                        ppa_log_printf("GUI: Auto RUN after LOAD -> %s\n",
                                       prefs.auto_run ? "ON" : "OFF");
                    } else if (point_in_rect(
                                   x, y, &PREF_PTY_BUTTON)) {
                        bool desired = !(backend->is_tty_enabled &&
                                         backend->is_tty_enabled(
                                             backend->ctx));
                        if (backend->set_tty_enabled &&
                            backend->set_tty_enabled(
                                backend->ctx, desired)) {
                            prefs.tty_requested =
                                backend->is_tty_enabled
                                    ? backend->is_tty_enabled(
                                          backend->ctx)
                                    : desired;
                            ppa_log_printf(
                                "GUI: External PTY -> %s\n",
                                prefs.tty_requested ? "ON" : "OFF");
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
        Uint16 bank = 0;
        if (backend->register_value) {
            bank = (Uint16)(
                ((Uint16)backend->register_value(backend->ctx, 8) << 8) |
                (Uint16)backend->register_value(backend->ctx, 9));
        }

        Uint8 bus_data = running
            ? backend->data_bus(backend->ctx)
            : (exam_held ? exam_data : front_data_latch);
        Uint8 data = bus_data;
        Uint8 status_register = backend->status_register
            ? backend->status_register(backend->ctx)
            : 0;

        if (data_source_index == 1 && backend->register_value) {
            data = backend->register_value(backend->ctx, register_index);
        }

        /* Render the complete hardware panel at its immutable design size. */
        SDL_SetRenderTarget(renderer, panel_texture);
        SDL_SetRenderViewport(renderer, NULL);
        SDL_SetRenderScale(
            renderer, panel_render_scale, panel_render_scale);
        g_text_pixel_scale = panel_render_scale;
        SDL_SetRenderDrawColor(
            renderer, COL_BLACK.r, COL_BLACK.g, COL_BLACK.b, 255);
        SDL_RenderClear(renderer);

        SDL_FRect face = {10, 8, 1260, 742};
        draw_bevel_panel(renderer, face,
            COL_DARK_GRAY,
            COL_LIGHT_GRAY,
            COL_BLACK);

        draw_screw(renderer, 24, 22);
        draw_screw(renderer, 1256, 22);
        draw_screw(renderer, 24, 736);
        draw_screw(renderer, 1256, 736);

        /* One continuous metal panel: only engraved separator lines, no modular cards. */
        SDL_FRect title_rect = {36, 18, 990, 68};
        draw_text_stretched(renderer, panel_fonts.title, title_rect,
                            COL_WHITE,
                            "PROGRAMBLE PROCESSOR");

        /*
         * A-1 uses the same 46 px bold type scale as the processor title.
         * Its box is an independent white-outline nameplate; the manufacturer
         * label sits immediately below it, outside the rectangle.
         */
        SDL_FRect a1_box = {1038, 18, 190, 68};
        SDL_SetRenderDrawColor(renderer,
                               COL_DARK_GRAY.r,
                               COL_DARK_GRAY.g,
                               COL_DARK_GRAY.b, 255);
        SDL_RenderFillRect(renderer, &a1_box);

        SDL_SetRenderDrawColor(renderer,
                               COL_WHITE.r,
                               COL_WHITE.g,
                               COL_WHITE.b, 255);
        SDL_RenderRect(renderer, &a1_box);

        /* Second inset line keeps the outline readable after downsampling. */
        SDL_FRect a1_inner = {
            a1_box.x + 1.0f, a1_box.y + 1.0f,
            a1_box.w - 2.0f, a1_box.h - 2.0f
        };
        SDL_RenderRect(renderer, &a1_inner);

        draw_text_stretched(renderer, panel_fonts.badge,
            (SDL_FRect){1046, 18, 174, 68},
            COL_WHITE, "A-1");

        draw_text_stretched(renderer, panel_fonts.label,
            (SDL_FRect){1038, 87, 190, 20},
            COL_WHITE, "Adava Electronics");

        SDL_SetRenderDrawColor(
            renderer, COL_LIGHT_GRAY.r, COL_LIGHT_GRAY.g,
            COL_LIGHT_GRAY.b, 255);
        SDL_RenderLine(renderer, 46, 112, 1228, 112);
        SDL_RenderLine(renderer, 620, 132, 620, 565);
        SDL_RenderLine(renderer, 46, 350, 592, 350);
        SDL_RenderLine(renderer, 646, 300, 1228, 300);
        SDL_RenderLine(renderer, 646, 500, 1228, 500);

        /* Left upper: address, bank and data indicators. */
        draw_text(renderer, panel_fonts.section, 52, 122,
                  COL_WHITE, "BUS INDICATORS");

        draw_text(renderer, panel_fonts.label, 54, 148,
                  COL_LIGHT_GRAY,
                  "ADDRESS BUS");
        for (int pos = 0; pos < 16; pos++) {
            int bit_index = 15 - pos;
            char label[2] = {"0123456789ABCDEF"[bit_index], 0};
            bool bit = prefs.lamp_test ||
                (powered && ((address >> bit_index) & 1u));
            float nibble_gap = (float)(pos / 4) * 10.0f;
            draw_led(renderer, &panel_fonts,
                     58.0f + pos * 31.0f + nibble_gap,
                     166.0f, label, bit,
                     COL_ORANGE);
        }

        draw_text(renderer, panel_fonts.label, 54, 216,
                  COL_LIGHT_GRAY,
                  "BANK BUS");
        for (int pos = 0; pos < 16; pos++) {
            int bit_index = 15 - pos;
            char label[2] = {"0123456789ABCDEF"[bit_index], 0};
            bool bit = prefs.lamp_test ||
                (powered && ((bank >> bit_index) & 1u));
            float nibble_gap = (float)(pos / 4) * 10.0f;
            draw_led(renderer, &panel_fonts,
                     58.0f + pos * 31.0f + nibble_gap,
                     234.0f, label, bit,
                     COL_BLUE);
        }

        draw_text(renderer, panel_fonts.label, 54, 284,
                  COL_LIGHT_GRAY, "DATA BUS");
        for (int pos = 0; pos < 8; pos++) {
            int bit_index = 7 - pos;
            char label[2] = {(char)('0' + bit_index), 0};
            bool bit = prefs.lamp_test ||
                (powered && ((data >> bit_index) & 1u));

            /* Same 31 px pitch as ADDRESS/BANK, with one nibble gap. */
            float nibble_gap = pos >= 4 ? 10.0f : 0.0f;
            draw_led(renderer, &panel_fonts,
                     58.0f + pos * 31.0f + nibble_gap,
                     302.0f, label, bit,
                     COL_GREEN);
        }

        /* Right upper: system status, no separate box. */
        draw_text(renderer, panel_fonts.section, 646, 126,
                  COL_WHITE, "SYSTEM STATUS");

        const char *sys_labels[] = {"POWER", "RUN", "HALT", "ERROR", "CLOCK", "MEMORY", "I/O"};
        SDL_Color sys_colors[] = {
            COL_GREEN, COL_GREEN, COL_RED,
            COL_RED, COL_ORANGE, COL_GREEN,
            COL_BLUE
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
            float group_gap = i >= 4 ? 18.0f : 0.0f;
            draw_led(renderer, &panel_fonts,
                     650.0f + i * 75.0f + group_gap,
                     175.0f,
                     sys_labels[i],
                     prefs.lamp_test || sys_on[i],
                     sys_colors[i]);
        }

        /*
         * STATUS REGISTER is shown semantically, not as raw S7-S0 bits.
         * PPA-1 currently defines only ZERO (bit 0) and CARRY (bit 1).
         */
        draw_text(renderer, panel_fonts.label, 646, 226,
                  COL_LIGHT_GRAY, "STATUS REGISTER");
        draw_led(renderer, &panel_fonts, 650, 245, "ZERO",
                 prefs.lamp_test ||
                     (powered && ((status_register & 0x01u) != 0)),
                 COL_GREEN);
        draw_led(renderer, &panel_fonts, 728, 245, "CARRY",
                 prefs.lamp_test ||
                     (powered && ((status_register & 0x02u) != 0)),
                 COL_ORANGE);

        /* Lower left: manual switch register. */
        draw_text(renderer, panel_fonts.section, 52, 371,
                  COL_WHITE, "FRONT PANEL INPUT");
        draw_text(renderer, panel_fonts.label, 54, 407,
                  COL_LIGHT_GRAY,
                  "DATA / ADDRESS");
        for (int pos = 0; pos < 16; pos++) {
            int bit_index = 15 - pos;
            char label[2] = {"0123456789ABCDEF"[bit_index], 0};
            /*
             * Front-panel switches are grouped in 4-bit nibbles.
             * For DATA, the rightmost two groups are D7-D4 | D3-D0;
             * for ADDRESS, all four groups form A15-A0.
             */
            float nibble_gap = (float)(pos / 4) * 10.0f;
            draw_toggle(renderer, &panel_fonts,
                        62.0f + pos * 31.0f + nibble_gap,
                        475.0f, label, switches[bit_index]);
        }
        /* Physical clock and DATA display selectors. */
        draw_text(renderer, panel_fonts.section, 646, 321,
                  COL_WHITE, "DISPLAY & CLOCK");

        draw_text_center(renderer, panel_fonts.label,
                         (SDL_FRect){660, 346, 120, 18},
                         COL_WHITE, "CLOCK");
        draw_labeled_knob(renderer, &panel_fonts,
                          clock_cx, clock_cy, clock_radius,
                          clock_index, clock_count,
                          clock_tick_labels, true);

        draw_text_center(renderer, panel_fonts.label,
                         (SDL_FRect){838, 346, 134, 18},
                         COL_WHITE, "DATA OUTPUT");
        draw_labeled_knob(renderer, &panel_fonts,
                          data_source_cx, data_source_cy,
                          data_source_radius,
                          data_source_index, 2,
                          data_source_tick_labels, true);

        draw_text_center(renderer, panel_fonts.label,
                         (SDL_FRect){1015, 346, 160, 18},
                         COL_WHITE,
                         "REGISTER SELECT");
        draw_labeled_knob(renderer, &panel_fonts,
                          register_cx, register_cy,
                          register_radius,
                          register_index, register_count,
                          register_labels,
                          true);

        /* Hardware programming and execution controls. */
        draw_text(renderer, panel_fonts.section, 646, 520,
                  COL_WHITE, "PROGRAMMING & CONTROL");

        draw_push_button(renderer, &panel_fonts, fp_load_bank, "LOAD BANK",
                         COL_BLUE, false,
                         button_pressed(pressed_button, BTN_LOAD_BANK, pressed_until));
        draw_push_button(renderer, &panel_fonts, fp_load_addr, "LOAD ADDR",
                         COL_ORANGE, false,
                         button_pressed(pressed_button, BTN_LOAD_ADDR, pressed_until));
        draw_push_button(renderer, &panel_fonts, fp_addr_dec, "ADDR -",
                         COL_GRAY, false,
                         button_pressed(pressed_button, BTN_ADDR_DEC, pressed_until));
        draw_push_button(renderer, &panel_fonts, fp_addr_inc, "ADDR +",
                         COL_GRAY, false,
                         button_pressed(pressed_button, BTN_ADDR_INC, pressed_until));
        draw_push_button(renderer, &panel_fonts, fp_exam, "EXAM",
                         COL_GRAY, false,
                         exam_held ||
                         button_pressed(pressed_button, BTN_EXAM, pressed_until));
        draw_push_button(renderer, &panel_fonts, fp_dep, "DEPOSIT",
                         COL_GRAY, false,
                         button_pressed(pressed_button, BTN_DEPOSIT, pressed_until));
        draw_push_button(renderer, &panel_fonts, fp_start, "START",
                         COL_GREEN, powered && running,
                         button_pressed(pressed_button, BTN_START, pressed_until));

        draw_push_button(renderer, &panel_fonts, op_cont, "CONT",
                         COL_GREEN, powered && running,
                         button_pressed(pressed_button, BTN_CONT, pressed_until));
        draw_push_button(renderer, &panel_fonts, op_stop, "STOP",
                         COL_RED, powered && !running,
                         button_pressed(pressed_button, BTN_STOP, pressed_until));
        draw_push_button(renderer, &panel_fonts, op_step, "STEP",
                         COL_GRAY, false,
                         button_pressed(pressed_button, BTN_STEP, pressed_until));
        draw_push_button(renderer, &panel_fonts, op_ustep, "MICRO",
                         COL_GRAY, false,
                         button_pressed(pressed_button, BTN_MICRO, pressed_until));
        draw_push_button(renderer, &panel_fonts, op_reset, "RESET",
                         COL_GRAY, false,
                         button_pressed(pressed_button, BTN_RESET, pressed_until));

        /* PWR remains part of the same physical plate. */
        draw_text(renderer, panel_fonts.label, 1188, 520,
                  COL_WHITE, "PWR");
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
        draw_text_center(renderer, panel_fonts.label, rocker,
                         COL_WHITE,
                         powered ? "I" : "O");

        /* Composite the fixed panel into the actual resizable window. */
        SDL_SetRenderTarget(renderer, NULL);
        SDL_SetRenderViewport(renderer, NULL);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
        g_text_pixel_scale = 1.0f;

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
        draw_menu_button(renderer, &fonts, menu_credits, "CREDITS");

        SDL_FRect panel_dst = {
            panel_origin_x, panel_origin_y,
            panel_screen_w, panel_screen_h
        };
        SDL_RenderTexture(renderer, panel_texture, NULL, &panel_dst);
        SDL_RenderPresent(renderer);

        draw_aux_window(&prefs_window, &fonts, &prefs, backend);
        draw_aux_window(&view_window, &fonts, &prefs, backend);
        draw_aux_window(&log_window, &fonts, &prefs, backend);
        draw_aux_window(&credits_window, &fonts, &prefs, backend);
        draw_terminal(&uart_terminal, &fonts, backend);
    }

    backend->set_running(backend->ctx, false);
    close_terminal(&uart_terminal, backend);

    destroy_aux(&prefs_window);
    destroy_aux(&view_window);
    destroy_aux(&log_window);
    destroy_aux(&credits_window);
    SDL_DestroyTexture(panel_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    close_fonts(&panel_fonts);
    close_fonts(&fonts);
    TTF_Quit();
    SDL_Quit();

    ppa_log_printf("GUI: front panel closed.\n");
    return 0;
}
