#include "ppa_vga_gui.h"
#include "ppa_module.h"
#include "ppa_module_draw.h"
#include "ppa_monitor.h"
#include <stdio.h>
#include <stdlib.h>

/* Full-resolution monitor scanout. All pixels originate in on-card VRAM.
   The emulator never writes graphics to the SDL window on the CPU's behalf. */
static uint32_t rgba(const uint8_t *p,unsigned index) {
    const uint8_t *c=p+index*3u;
    return ((uint32_t)c[0]<<24)|((uint32_t)c[1]<<16)|((uint32_t)c[2]<<8)|255u;
}
static void draw_scanout(SDL_Renderer *r,const PPA1ModuleInfo *v,
                         const SDL_FRect *screen,const PpaMonitor *monitor) {
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
                                              SDL_TEXTUREACCESS_TARGET,(int)w,(int)h);
        if(!target) return;
        SDL_SetTextureBlendMode(target,SDL_BLENDMODE_NONE);
        SDL_Texture *previous=SDL_GetRenderTarget(r);
        if(SDL_SetRenderTarget(r,target)) {
            /* Render text in native text-buffer coordinates, not the
               window's high-resolution logical monitor coordinates. */
            SDL_SetRenderLogicalPresentation(r,(int)w,(int)h,
                                             SDL_LOGICAL_PRESENTATION_DISABLED);
            SDL_SetRenderDrawColor(r,0,0,0,255);
            SDL_RenderClear(r);
            unsigned columns=w/8u,rows=h/16u;
            for(unsigned y=0;y<rows;y++) for(unsigned x=0;x<columns;x++) {
                unsigned cell=(y*columns+x)*2u;
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
        SDL_SetRenderLogicalPresentation(r,1280,800,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetTextureScaleMode(target,
            monitor && (monitor->style==PPA_MONITOR_CRT ||
                        monitor->sharpness<40) ?
                SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        if(monitor) {
            int v=monitor->brightness*255/50;
            if(v>255) v=255;
            SDL_SetTextureColorMod(target,(Uint8)v,(Uint8)v,(Uint8)v);
        }
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
    } else if(v->vga_bpp==4) {
        /* EGA/VGA planar memory: four independent bitplanes. */
        unsigned stride=(unsigned)(count/8u);
        for(uint64_t i=0;i<count;i++) {
            unsigned b=(unsigned)(i>>3),mask=1u<<(7-(i&7));
            unsigned idx=0;
            for(unsigned plane=0;plane<4;plane++) {
                unsigned pos=plane*stride+b;
                if(pos<v->vga_vram_size && (mem[pos]&mask)) idx|=1u<<plane;
            }
            pixels[i]=rgba(palette,idx);
        }
    } else if(v->vga_bpp==1) {
        for(uint64_t i=0;i<count;i++) {
            uint64_t p=i/8u;
            unsigned bit=p<v->vga_vram_size ? (mem[p]>>(7-(i&7)))&1u : 0;
            pixels[i]=rgba(palette,bit?15:0);
        }
    } else if(v->vga_bpp==2) {
        static const uint8_t cga[4]={0,3,5,15};
        for(uint64_t i=0;i<count;i++) {
            uint64_t p=i/4u;
            unsigned bit=p<v->vga_vram_size ? (mem[p]>>(6-2*(i&3)))&3u : 0;
            pixels[i]=rgba(palette,cga[bit]);
        }
    } else {
        for(uint64_t i=0;i<count;i++)
            pixels[i]=rgba(palette,i<v->vga_vram_size?mem[i]:0);
    }
    /* Monitor picture controls never modify hardware VRAM. */
    if(monitor && (monitor->brightness!=50 || monitor->contrast!=50 ||
                   monitor->saturation!=50 || monitor->gamma!=50))
        for(uint64_t i=0;i<count;i++)
            pixels[i]=ppa_monitor_adjust_pixel(monitor,pixels[i]);
    SDL_Texture *tex=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,(int)w,(int)h);
    if(tex) {
        SDL_SetTextureScaleMode(tex,
            monitor && (monitor->style==PPA_MONITOR_CRT ||
                        monitor->sharpness<40) ?
                SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(tex,NULL,pixels,(int)(w*sizeof(uint32_t)));
        SDL_RenderTexture(r,tex,NULL,screen);
        SDL_DestroyTexture(tex);
    }
    free(pixels);
}
void ppa_vga_gui_draw(SDL_Renderer *r,TTF_Font *font,
                      const PPA1ModuleInfo *info,int slot) {
    if(!r||!info) return;
    PpaMonitor *monitor=ppa_monitor_for_slot(slot);
    ppa_monitor_tick(monitor,info);
    SDL_SetRenderDrawColor(r,20,22,24,255);
    SDL_RenderClear(r);
    char s[160];
    SDL_FRect housing={16,16,1248,768};
    SDL_SetRenderDrawColor(r,46,50,51,255);
    SDL_RenderFillRect(r,&housing);
    SDL_SetRenderDrawColor(r,125,131,133,255);
    SDL_RenderRect(r,&housing);
    ppa_mod_text(r,font,36,32,(SDL_Color){239,244,234,255},
                 "PPA-VGA/1  |  HIGH-RESOLUTION COLOR MONITOR");
    snprintf(s,sizeof(s),"SLOT %d  /  %s  /  MODE %02Xh  /  %ux%u  /  %u BPP",
             slot+1,info->active?"ACTIVE":"DISABLED",info->vga_mode,
             info->vga_width,info->vga_height,info->vga_bpp);
    ppa_mod_text(r,font,36,65,(SDL_Color){169,213,170,255},s);
    SDL_FRect bezel={30,105,1220,600};
    SDL_SetRenderDrawColor(r,10,12,14,255);
    SDL_RenderFillRect(r,&bezel);
    SDL_SetRenderDrawColor(r,110,116,119,255);
    SDL_RenderRect(r,&bezel);
    SDL_FRect outer={42,117,1196,576};
    unsigned border=info->vga_border;
    const uint8_t *p=info->vga_palette ? info->vga_palette+border*3u : NULL;
    SDL_SetRenderDrawColor(r,p?p[0]:0,p?p[1]:0,p?p[2]:0,255);
    SDL_RenderFillRect(r,&outer);
    SDL_FRect bounds={48,123,1184,564};
    SDL_SetRenderDrawColor(r,0,0,0,255);
    SDL_RenderFillRect(r,&bounds);
    if(monitor && monitor->state==PPA_MONITOR_ACTIVE &&
       info->active && info->vga_enabled && info->vga_vram &&
       info->vga_width && info->vga_height) {
        float xratio=bounds.w/info->vga_width;
        float yratio=bounds.h/info->vga_height;
        float scale=xratio<yratio?xratio:yratio;
        SDL_FRect display={bounds.x+(bounds.w-info->vga_width*scale)*0.5f,
            bounds.y+(bounds.h-info->vga_height*scale)*0.5f,
            info->vga_width*scale,info->vga_height*scale};
        draw_scanout(r,info,&display,monitor);
    }
    snprintf(s,sizeof(s),"VIDEO MEMORY: %u BYTES  |  64 KiB BANKED APERTURE",
             info->vga_vram_size);
    ppa_mod_text(r,font,36,720,(SDL_Color){211,220,218,255},s);
    ppa_mod_text(r,font,36,749,(SDL_Color){150,160,159,255},
                 "ADAVA DISPLAY  |  PPA-VGA/1 RGB INPUT");
    ppa_monitor_draw(r,font,info,slot,&bounds);
}
