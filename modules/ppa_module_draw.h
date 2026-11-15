#ifndef PPA_MODULE_DRAW_H
#define PPA_MODULE_DRAW_H
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
void ppa_mod_text(SDL_Renderer *r, TTF_Font *font, float x, float y,
                  SDL_Color color, const char *text);
void ppa_mod_circle(SDL_Renderer *r, float x, float y, float radius,
                    SDL_Color color);
void ppa_mod_face(SDL_Renderer *r, const char *title, TTF_Font *font);
#endif
