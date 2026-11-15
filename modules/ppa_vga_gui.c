#include "ppa_vga_gui.h"
#include "ppa_module.h"
#include "ppa_module_draw.h"
#include <stdio.h>

void ppa_vga_gui_draw(SDL_Renderer *r, TTF_Font *font,
                      const PPA1ModuleInfo *info, int slot) {
    if (!r || !info) return;
    ppa_mod_face(r, "PPA-VGA/1  |  COLOR VIDEO ADAPTER", font);

    char status[192];
    snprintf(status, sizeof(status), "SLOT %d  /  %s  /  MODE 0  /  320x200x8",
             slot + 1, info->active ? "ACTIVE" : "DISABLED");
    ppa_mod_text(r, font, 30, 82, (SDL_Color){169,213,170,255}, status);

    uint8_t br = 0, bg = 0, bb = 0;
    if (info->vga_palette) {
        unsigned p = (unsigned)info->vga_border * 3u;
        br = info->vga_palette[p + 0u];
        bg = info->vga_palette[p + 1u];
        bb = info->vga_palette[p + 2u];
    }

    SDL_FRect bezel = {22, 110, 496, 334};
    SDL_SetRenderDrawColor(r, 10, 12, 13, 255);
    SDL_RenderFillRect(r, &bezel);
    SDL_SetRenderDrawColor(r, 100, 106, 103, 255);
    SDL_RenderRect(r, &bezel);

    SDL_FRect border = {28, 116, 484, 312};
    SDL_SetRenderDrawColor(r, br, bg, bb, 255);
    SDL_RenderFillRect(r, &border);

    SDL_FRect screen = {30, 122, 480, 300};
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderFillRect(r, &screen);

    if (info->active && info->vga_enabled &&
        info->vga_vram && info->vga_palette) {
        static uint32_t pixels[PPAM_VGA_VRAM_SIZE];
        for (unsigned i = 0; i < PPAM_VGA_VRAM_SIZE; ++i) {
            unsigned p = (unsigned)info->vga_vram[i] * 3u;
            uint32_t rr = info->vga_palette[p + 0u];
            uint32_t gg = info->vga_palette[p + 1u];
            uint32_t bb2 = info->vga_palette[p + 2u];
            pixels[i] = (rr << 24) | (gg << 16) | (bb2 << 8) | 0xFFu;
        }

        SDL_Texture *tex = SDL_CreateTexture(
            r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
            (int)PPAM_VGA_WIDTH, (int)PPAM_VGA_HEIGHT);
        if (tex) {
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
            SDL_UpdateTexture(tex, NULL, pixels,
                              (int)(PPAM_VGA_WIDTH * sizeof(uint32_t)));
            SDL_RenderTexture(r, tex, NULL, &screen);
            SDL_DestroyTexture(tex);
        }
    }

    ppa_mod_text(r, font, 30, 454, (SDL_Color){221,226,218,255},
                 "VRAM +1 BANK / 64,000 B / 256-COLOR PROGRAMMABLE RGB DAC");
    ppa_mod_text(r, font, 30, 477, (SDL_Color){154,160,156,255},
                 "DISPLAY DATA COMES ONLY FROM PPA-BUS/1 VIDEO MEMORY");
}
