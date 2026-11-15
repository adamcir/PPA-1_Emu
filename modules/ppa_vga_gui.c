#include "ppa_vga_gui.h"
#include "ppa_module.h"
#include "ppa_module_draw.h"
#include <stdio.h>
#include <stdlib.h>

/* Full-resolution monitor scanout. All pixels originate in on-card VRAM.
   The emulator never writes graphics to the SDL window on the CPU's behalf. */
static uint32_t rgba(const uint8_t *p,unsigned index) {
    const uint8_t *c=p+index*3u;
    return ((uint32_t)c[0]<<24)|((uint32_t)c[1]<<16)|((uint32_t)c[2]<<8)|255u;
}
static void draw_scanout(SDL_Renderer *r,const PPA1ModuleInfo *v,
                         const SDL_FRect *screen) {
    if(!v->vga_vram || !v->vga_palette || !v->vga_width || !v->vga_height) return;
    unsigned w=v->vga_width,h=v->vga_height;
    uint64_t count=(uint64_t)w*h;
    if(count>1920u*1080u) return;
    uint32_t *pixels=malloc((size_t)count*sizeof(*pixels));
    if(!pixels) return;
    const uint8_t *mem=v->vga_vram;
    const uint8_t *palette=v->vga_palette;
    if(v->vga_text) {
        /* Text is drawn into a target texture below using the embedded
           SDL debug font. Character/attribute pairs are real VRAM bytes. */
        free(pixels);
        SDL_Texture *target=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_TARGET,640,400);
        if(!target) return;
        SDL_SetTextureBlendMode(target,SDL_BLENDMODE_NONE);
        SDL_Texture *previous=SDL_GetRenderTarget(r);
        if(SDL_SetRenderTarget(r,target)) {
            SDL_SetRenderDrawColor(r,0,0,0,255);
            SDL_RenderClear(r);
            for(unsigned y=0;y<25;y++) for(unsigned x=0;x<80;x++) {
                unsigned cell=(y*80u+x)*2u;
                if(cell+1>=v->vga_vram_size) continue;
                unsigned ch=mem[cell],attr=mem[cell+1];
                unsigned bg=(attr>>4)&15u,fg=attr&15u;
                const uint8_t *b=palette+3u*bg;
                SDL_FRect rc={(float)(x*8),(float)(y*16),8,16};
                SDL_SetRenderDrawColor(r,b[0],b[1],b[2],255);
                SDL_RenderFillRect(r,&rc);
                if(ch>=32&&ch<127) {
                    const uint8_t *c=palette+3u*fg;
                    char letter[2]={(char)ch,0};
                    SDL_SetRenderDrawColor(r,c[0],c[1],c[2],255);
                    SDL_RenderDebugText(r,(float)(x*8),(float)(y*16+4),letter);
                }
            }
        }
        SDL_SetRenderTarget(r,previous);
        SDL_SetTextureScaleMode(target,SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(r,target,NULL,screen);
        SDL_DestroyTexture(target);
        return;
    }
    if(v->vga_bpp==24) {
        for(uint64_t i=0;i<count;i++) {
            uint64_t p=i*3u;
            pixels[i]=p+2<v->vga_vram_size ?
                ((uint32_t)mem[p]<<24)|((uint32_t)mem[p+1]<<16)|
                ((uint32_t)mem[p+2]<<8)|255u : 255u;
        }
    } else if(v->vga_mode==0x12) {
        /* Four 38,400-byte bitplanes, leftmost pixel is bit 7. */
        for(uint64_t i=0;i<count;i++) {
            unsigned b=(unsigned)(i>>3),mask=1u<<(7-(i&7));
            unsigned idx=0;
            for(unsigned plane=0;plane<4;plane++) {
                unsigned pos=plane*38400u+b;
                if(pos<v->vga_vram_size && (mem[pos]&mask)) idx|=1u<<plane;
            }
            pixels[i]=rgba(palette,idx);
        }
    } else {
        for(uint64_t i=0;i<count;i++)
            pixels[i]=rgba(palette,i<v->vga_vram_size?mem[i]:0);
    }
    SDL_Texture *tex=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,(int)w,(int)h);
    if(tex) {
        SDL_SetTextureScaleMode(tex,SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(tex,NULL,pixels,(int)(w*sizeof(uint32_t)));
        SDL_RenderTexture(r,tex,NULL,screen);
        SDL_DestroyTexture(tex);
    }
    free(pixels);
}
void ppa_vga_gui_draw(SDL_Renderer *r,TTF_Font *font,
                      const PPA1ModuleInfo *info,int slot) {
    if(!r||!info) return;
    SDL_SetRenderDrawColor(r,20,22,24,255);
    SDL_RenderClear(r);
    char s[160];
    ppa_mod_face(r,"PPA-VGA/1  |  VGA DISPLAY",font);
    snprintf(s,sizeof(s),"SLOT %d  /  %s  /  MODE %02Xh  /  %ux%u  /  %u BPP",
             slot+1,info->active?"ACTIVE":"DISABLED",info->vga_mode,
             info->vga_width,info->vga_height,info->vga_bpp);
    ppa_mod_text(r,font,22,80,(SDL_Color){169,213,170,255},s);
    SDL_FRect bezel={15,105,510,345};
    SDL_SetRenderDrawColor(r,10,12,14,255);
    SDL_RenderFillRect(r,&bezel);
    SDL_SetRenderDrawColor(r,110,116,119,255);
    SDL_RenderRect(r,&bezel);
    SDL_FRect outer={21,111,498,333};
    unsigned border=info->vga_border;
    const uint8_t *p=info->vga_palette ? info->vga_palette+border*3u : NULL;
    SDL_SetRenderDrawColor(r,p?p[0]:0,p?p[1]:0,p?p[2]:0,255);
    SDL_RenderFillRect(r,&outer);
    SDL_FRect bounds={25,115,490,325};
    SDL_SetRenderDrawColor(r,0,0,0,255);
    SDL_RenderFillRect(r,&bounds);
    if(info->active && info->vga_enabled && info->vga_vram &&
       info->vga_width && info->vga_height) {
        float xratio=bounds.w/info->vga_width;
        float yratio=bounds.h/info->vga_height;
        float scale=xratio<yratio?xratio:yratio;
        SDL_FRect display={bounds.x+(bounds.w-info->vga_width*scale)*0.5f,
            bounds.y+(bounds.h-info->vga_height*scale)*0.5f,
            info->vga_width*scale,info->vga_height*scale};
        draw_scanout(r,info,&display);
    } else {
        ppa_mod_text(r,font,175,255,(SDL_Color){80,85,85,255},
                     "NO VIDEO SIGNAL");
    }
    snprintf(s,sizeof(s),"VIDEO MEMORY: %u BYTES  |  64 KiB BANKED APERTURE",
             info->vga_vram_size);
    ppa_mod_text(r,font,22,464,(SDL_Color){211,220,218,255},s);
    ppa_mod_text(r,font,22,486,(SDL_Color){150,160,159,255},
                 "PPA-BUS/1 VIDEO OUTPUT  /  DIGITAL RGB MONITOR");
}
