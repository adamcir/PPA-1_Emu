#include "ppa_monitor.h"
#include "ppa_module_draw.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MON_BUTTON_Y 721.0f
#define MON_BUTTON_X 720.0f
#define MON_BUTTON_W 81.0f
#define MON_BUTTON_STEP 90.0f
#define MON_BUTTON_H 30.0f
#define MON_NO_SIGNAL_MS 3000u
#define MON_SLEEP_MS 8000u
#define MON_POPUP_MS 2600u

enum { OSD_BRIGHTNESS, OSD_CONTRAST, OSD_SATURATION, OSD_SHARPNESS,
       OSD_GAMMA, OSD_STYLE, OSD_AUTO_SLEEP, OSD_INFORMATION,
       OSD_FACTORY_RESET, OSD_ITEM_COUNT };
static const char * const osd_items[] = {
    "BRIGHTNESS", "CONTRAST", "SATURATION", "SHARPNESS", "GAMMA",
    "DISPLAY TYPE", "AUTO SLEEP", "INFORMATION", "FACTORY RESET"
};
static const char * const button_labels[] = {
    "MENU", "-", "+", "AUTO", "LCD/CRT", "POWER"
};
static PpaMonitor monitors[8];

static void factory_defaults(PpaMonitor *m) {
    m->power_on=true;
    m->auto_sleep=true;
    m->osd_open=false;
    m->info_open=false;
    m->brightness=50;
    m->contrast=50;
    m->saturation=50;
    m->sharpness=50;
    m->gamma=50;
    m->style=PPA_MONITOR_LCD;
    m->selected=OSD_BRIGHTNESS;
    m->state=PPA_MONITOR_NO_SIGNAL;
    m->no_signal_since=SDL_GetTicks();
    m->popup_until=0;
    m->last_signal=false;
}
PpaMonitor *ppa_monitor_for_slot(int slot) {
    if(slot<0 || slot>=8) return NULL;
    PpaMonitor *m=&monitors[slot];
    if(!m->initialized) {
        factory_defaults(m);
        m->initialized=true;
    }
    return m;
}
static bool has_signal(const PPA1ModuleInfo *v) {
    return v && v->installed && v->active && v->type==3 &&
           v->vga_enabled && v->vga_vram && v->vga_width && v->vga_height;
}
void ppa_monitor_tick(PpaMonitor *m,const PPA1ModuleInfo *v) {
    if(!m) return;
    Uint64 now=SDL_GetTicks();
    bool signal=has_signal(v);
    if(!m->power_on) {
        m->state=PPA_MONITOR_OFF;
        m->last_signal=signal;
        return;
    }
    if(signal) {
        if(!m->last_signal || m->state!=PPA_MONITOR_ACTIVE)
            m->popup_until=now+MON_POPUP_MS;
        m->state=PPA_MONITOR_ACTIVE;
        m->no_signal_since=0;
    } else {
        if(m->last_signal || !m->no_signal_since)
            m->no_signal_since=now;
        Uint64 elapsed=now-m->no_signal_since;
        if(!m->auto_sleep || elapsed<MON_NO_SIGNAL_MS)
            m->state=PPA_MONITOR_NO_SIGNAL;
        else if(elapsed<MON_SLEEP_MS)
            m->state=PPA_MONITOR_ENTERING_SLEEP;
        else
            m->state=PPA_MONITOR_SLEEP;
    }
    m->last_signal=signal;
}
static bool inside(float x,float y,SDL_FRect r) {
    return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;
}
static int *selected_value(PpaMonitor *m) {
    switch(m->selected) {
        case OSD_BRIGHTNESS: return &m->brightness;
        case OSD_CONTRAST: return &m->contrast;
        case OSD_SATURATION: return &m->saturation;
        case OSD_SHARPNESS: return &m->sharpness;
        case OSD_GAMMA: return &m->gamma;
        default: return NULL;
    }
}
static void adjust(PpaMonitor *m,int delta) {
    int *value=selected_value(m);
    if(value) {
        *value+=delta*5;
        if(*value<0) *value=0;
        if(*value>100) *value=100;
    } else if(m->selected==OSD_STYLE) {
        m->style=m->style==PPA_MONITOR_LCD?PPA_MONITOR_CRT:PPA_MONITOR_LCD;
    } else if(m->selected==OSD_AUTO_SLEEP) {
        m->auto_sleep=!m->auto_sleep;
        m->no_signal_since=SDL_GetTicks();
    } else if(m->selected==OSD_INFORMATION) {
        m->info_open=!m->info_open;
    } else if(m->selected==OSD_FACTORY_RESET) {
        factory_defaults(m);
        m->osd_open=true;
    }
}
void ppa_monitor_button(int slot,float x,float y) {
    PpaMonitor *m=ppa_monitor_for_slot(slot);
    if(!m) return;
    for(int i=0;i<6;i++) {
        SDL_FRect rect={MON_BUTTON_X+i*MON_BUTTON_STEP,MON_BUTTON_Y,
                        MON_BUTTON_W,MON_BUTTON_H};
        if(!inside(x,y,rect)) continue;
        switch(i) {
            case 0: /* MENU cycles OSD rows, +/- adjusts selected item */
                if(!m->power_on) break;
                if(!m->osd_open) { m->osd_open=true; m->selected=0; }
                else if(m->info_open) m->info_open=false;
                else if(m->selected==OSD_ITEM_COUNT-1) m->osd_open=false;
                else m->selected++;
                break;
            case 1: if(m->power_on && m->osd_open) adjust(m,-1); break;
            case 2: if(m->power_on && m->osd_open) adjust(m,+1); break;
            case 3: /* AUTO restores calibrated picture levels */
                if(m->power_on) {
                    m->brightness=50; m->contrast=50;
                    m->saturation=50; m->gamma=50; m->sharpness=50;
                    m->popup_until=SDL_GetTicks()+MON_POPUP_MS;
                }
                break;
            case 4:
                if(m->power_on) m->style=m->style==PPA_MONITOR_LCD?
                                                   PPA_MONITOR_CRT:PPA_MONITOR_LCD;
                break;
            case 5:
                m->power_on=!m->power_on;
                m->osd_open=false; m->info_open=false;
                m->no_signal_since=SDL_GetTicks();
                m->popup_until=SDL_GetTicks()+MON_POPUP_MS;
                break;
        }
        return;
    }
}
static uint8_t clamp8(float v) {
    if(v<0) return 0;
    if(v>255) return 255;
    return (uint8_t)(v+0.5f);
}
uint32_t ppa_monitor_adjust_pixel(const PpaMonitor *m,uint32_t pixel) {
    if(!m) return pixel;
    float r=(float)((pixel>>24)&255),g=(float)((pixel>>16)&255),
          b=(float)((pixel>>8)&255);
    float sat=(float)m->saturation/50.0f;
    float gray=0.2126f*r+0.7152f*g+0.0722f*b;
    float contrast=0.25f+(float)m->contrast/50.0f*0.75f;
    float light=((float)m->brightness-50.0f)*2.55f;
    r=(gray+(r-gray)*sat-127.5f)*contrast+127.5f+light;
    g=(gray+(g-gray)*sat-127.5f)*contrast+127.5f+light;
    b=(gray+(b-gray)*sat-127.5f)*contrast+127.5f+light;
    float gamma=powf(2.0f,(50.0f-(float)m->gamma)/50.0f);
    r=255.0f*powf((float)clamp8(r)/255.0f,gamma);
    g=255.0f*powf((float)clamp8(g)/255.0f,gamma);
    b=255.0f*powf((float)clamp8(b)/255.0f,gamma);
    return ((uint32_t)clamp8(r)<<24)|((uint32_t)clamp8(g)<<16)|
           ((uint32_t)clamp8(b)<<8)|255u;
}
static void block(SDL_Renderer *r, SDL_FRect rc,SDL_Color c) {
    SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
    SDL_RenderFillRect(r,&rc);
}
static void text(SDL_Renderer *r,TTF_Font *font,float x,float y,
                 const char *s,SDL_Color color) {
    ppa_mod_text(r,font,x,y,color,s);
}
static void popup(SDL_Renderer *r,TTF_Font *f,SDL_FRect area,
                  const char *heading,const char *caption) {
    SDL_FRect rc={area.x+(area.w-440)/2,area.y+(area.h-120)/2,440,120};
    block(r,rc,(SDL_Color){22,45,90,255});
    SDL_SetRenderDrawColor(r,87,167,240,255);
    SDL_RenderRect(r,&rc);
    text(r,f,rc.x+35,rc.y+28,heading,(SDL_Color){255,255,255,255});
    text(r,f,rc.x+35,rc.y+70,caption,(SDL_Color){183,223,255,255});
}
void ppa_monitor_draw(SDL_Renderer *r,TTF_Font *f,const PPA1ModuleInfo *info,
                      int slot,const SDL_FRect *screen) {
    PpaMonitor *m=ppa_monitor_for_slot(slot);
    if(!m || !r || !f || !screen) return;
    Uint64 now=SDL_GetTicks();
    if(m->style==PPA_MONITOR_CRT && m->state==PPA_MONITOR_ACTIVE) {
        /* Screen-space scanlines, separate from the video card framebuffer. */
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
        float spacing=4.0f+(float)m->sharpness/35.0f;
        for(float y=screen->y+2;y<screen->y+screen->h;y+=spacing)
            block(r,(SDL_FRect){screen->x,y,screen->w,1.5f},
                  (SDL_Color){0,0,0,65});
        SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_NONE);
    }
    if(m->state==PPA_MONITOR_NO_SIGNAL) {
        float span=screen->w-480;
        float movement=span>0?(float)((now/18u)%(unsigned)span):0;
        SDL_FRect area=*screen;
        area.x+=movement*0.25f-movement*0.125f;
        popup(r,f,area,"NO VIDEO SIGNAL","CHECK PPA-VGA/1 RGB INPUT");
    } else if(m->state==PPA_MONITOR_ENTERING_SLEEP) {
        popup(r,f,*screen,"ENTERING SLEEP MODE...","NO VIDEO SIGNAL DETECTED");
    } else if(m->state==PPA_MONITOR_ACTIVE &&
              now<m->popup_until && !m->osd_open) {
        char line[128];
        snprintf(line,sizeof(line),"MODE %02Xh  %ux%u  %u BPP",
                 info->vga_mode,info->vga_width,info->vga_height,info->vga_bpp);
        popup(r,f,*screen,"PPA-VGA/1 RGB SIGNAL",line);
    }
    /* The front-panel buttons work in OFF and SLEEP too. */
    for(int i=0;i<6;i++) {
        SDL_FRect btn={MON_BUTTON_X+i*MON_BUTTON_STEP,MON_BUTTON_Y,
                      MON_BUTTON_W,MON_BUTTON_H};
        block(r,btn,(SDL_Color){61,66,68,255});
        SDL_SetRenderDrawColor(r,149,153,154,255);
        SDL_RenderRect(r,&btn);
        text(r,f,btn.x+9,btn.y+8,button_labels[i],
             (SDL_Color){236,240,238,255});
    }
    SDL_Color led={30,35,35,255};
    switch(m->state) {
        case PPA_MONITOR_ACTIVE: led=(SDL_Color){46,226,88,255}; break;
        case PPA_MONITOR_NO_SIGNAL: led=(SDL_Color){50,166,250,255}; break;
        case PPA_MONITOR_ENTERING_SLEEP:
        case PPA_MONITOR_SLEEP: led=(SDL_Color){240,168,39,255}; break;
        default: break;
    }
    block(r,(SDL_FRect){1234,756,14,12},led);
    text(r,f,1157,754,"PWR",(SDL_Color){194,199,195,255});
    if(!m->power_on || !m->osd_open) return;
    SDL_FRect panel={835,145,365,495};
    block(r,panel,(SDL_Color){24,37,56,255});
    SDL_SetRenderDrawColor(r,109,183,240,255);
    SDL_RenderRect(r,&panel);
    text(r,f,857,163,"ADAVA DISPLAY SETTINGS",
         (SDL_Color){239,247,255,255});
    if(m->info_open) {
        char line[128];
        text(r,f,857,209,"INPUT: PPA-VGA/1 RGB",
             (SDL_Color){199,222,243,255});
        snprintf(line,sizeof(line),"VIDEO MODE: %02Xh",info->vga_mode);
        text(r,f,857,244,line,(SDL_Color){199,222,243,255});
        snprintf(line,sizeof(line),"RESOLUTION: %ux%u",
                 info->vga_width,info->vga_height);
        text(r,f,857,279,line,(SDL_Color){199,222,243,255});
        snprintf(line,sizeof(line),"COLOR DEPTH: %u BPP",info->vga_bpp);
        text(r,f,857,314,line,(SDL_Color){199,222,243,255});
        snprintf(line,sizeof(line),"VIDEO MEMORY: %u B",info->vga_vram_size);
        text(r,f,857,349,line,(SDL_Color){199,222,243,255});
        text(r,f,857,404,has_signal(info)?"SIGNAL: PRESENT":"SIGNAL: MISSING",
             (SDL_Color){129,232,163,255});
        text(r,f,857,573,"MENU: BACK",(SDL_Color){182,197,210,255});
        return;
    }
    for(int i=0;i<OSD_ITEM_COUNT;i++) {
        int y=207+i*43;
        if(i==m->selected)
            block(r,(SDL_FRect){850,(float)y-6,334,37},
                  (SDL_Color){42,90,145,255});
        text(r,f,861,(float)y,osd_items[i],
             i==m->selected?(SDL_Color){255,255,255,255}:
                             (SDL_Color){195,209,219,255});
        char value[36]="";
        int n=-1;
        switch(i) {
            case OSD_BRIGHTNESS:n=m->brightness;break;
            case OSD_CONTRAST:n=m->contrast;break;
            case OSD_SATURATION:n=m->saturation;break;
            case OSD_SHARPNESS:n=m->sharpness;break;
            case OSD_GAMMA:n=m->gamma;break;
            case OSD_STYLE:snprintf(value,sizeof(value),"%s",
                     m->style==PPA_MONITOR_LCD?"LCD":"CRT");break;
            case OSD_AUTO_SLEEP:snprintf(value,sizeof(value),"%s",
                     m->auto_sleep?"ON":"OFF");break;
            default: break;
        }
        if(n>=0) snprintf(value,sizeof(value),"%d",n);
        text(r,f,1129,(float)y,value,(SDL_Color){129,238,179,255});
    }
    text(r,f,861,603,"MENU: NEXT   - / +: ADJUST",
         (SDL_Color){173,190,203,255});
}
