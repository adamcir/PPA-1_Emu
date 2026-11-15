#include "ppa_module_draw.h"
#include <math.h>
#include <string.h>

void ppa_mod_text(SDL_Renderer *r, TTF_Font *font, float x, float y,
                  SDL_Color color, const char *text) {
    if (!r || !font || !text || !*text) return;
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, 0, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(r, surface);
    if (texture) {
        SDL_FRect dst = {x, y, (float)surface->w, (float)surface->h};
        SDL_RenderTexture(r, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_DestroySurface(surface);
}
void ppa_mod_circle(SDL_Renderer *r, float x, float y, float radius,
                    SDL_Color color) {
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    for (int dy = -(int)radius; dy <= (int)radius; ++dy) {
        float span = sqrtf(radius * radius - (float)(dy * dy));
        SDL_RenderLine(r, x - span, y + (float)dy, x + span, y + (float)dy);
    }
}
void ppa_mod_face(SDL_Renderer *r, const char *title, TTF_Font *font) {
    SDL_SetRenderDrawColor(r, 20, 23, 24, 255);
    SDL_RenderClear(r);
    SDL_FRect outer = {16, 16, 508, 478};
    SDL_SetRenderDrawColor(r, 47, 51, 50, 255);
    SDL_RenderFillRect(r, &outer);
    SDL_SetRenderDrawColor(r, 131, 137, 132, 255);
    SDL_RenderRect(r, &outer);
    SDL_FRect name = {23, 24, 494, 46};
    SDL_SetRenderDrawColor(r, 27, 30, 30, 255);
    SDL_RenderFillRect(r, &name);
    ppa_mod_text(r, font, 32, 33, (SDL_Color){239,244,234,255}, title);
}
