#include "ppa_ps2_gui.h"
#include "ppa_module_draw.h"
#include <stdio.h>

/* Visual MF2-style US QWERTY arrangement. Values are PS/2 Set-2 scan codes.
   A key is illuminated while the corresponding hardware key is held. */
typedef struct { const char *name; uint8_t code; float width; bool ext; } Key;
static const Key row0[] = {
    {"ESC",0x76,1,0},{"F1",0x05,1,0},{"F2",0x06,1,0},{"F3",0x04,1,0},
    {"F4",0x0C,1,0},{"F5",0x03,1,0},{"F6",0x0B,1,0},{"F7",0x83,1,0},
    {"F8",0x0A,1,0},{"F9",0x01,1,0},{"F10",0x09,1,0},{"F11",0x78,1,0},
    {"F12",0x07,1,0}
};
static const Key row1[] = {
    {"~",0x0E,1,0},{"1",0x16,1,0},{"2",0x1E,1,0},{"3",0x26,1,0},
    {"4",0x25,1,0},{"5",0x2E,1,0},{"6",0x36,1,0},{"7",0x3D,1,0},
    {"8",0x3E,1,0},{"9",0x46,1,0},{"0",0x45,1,0},{"-",0x4E,1,0},
    {"=",0x55,1,0},{"BACK",0x66,2,0}
};
static const Key row2[] = {
    {"TAB",0x0D,1.5f,0},{"Q",0x15,1,0},{"W",0x1D,1,0},{"E",0x24,1,0},
    {"R",0x2D,1,0},{"T",0x2C,1,0},{"Y",0x35,1,0},{"U",0x3C,1,0},
    {"I",0x43,1,0},{"O",0x44,1,0},{"P",0x4D,1,0},{"[",0x54,1,0},
    {"]",0x5B,1,0},{"\\",0x5D,1.5f,0}
};
static const Key row3[] = {
    {"CAPS",0x58,1.8f,0},{"A",0x1C,1,0},{"S",0x1B,1,0},{"D",0x23,1,0},
    {"F",0x2B,1,0},{"G",0x34,1,0},{"H",0x33,1,0},{"J",0x3B,1,0},
    {"K",0x42,1,0},{"L",0x4B,1,0},{";",0x4C,1,0},{"'",0x52,1,0},
    {"ENTER",0x5A,2.2f,0}
};
static const Key row4[] = {
    {"SHIFT",0x12,2.3f,0},{"Z",0x1A,1,0},{"X",0x22,1,0},{"C",0x21,1,0},
    {"V",0x2A,1,0},{"B",0x32,1,0},{"N",0x31,1,0},{"M",0x3A,1,0},
    {",",0x41,1,0},{".",0x49,1,0},{"/",0x4A,1,0},{"SHIFT",0x59,2.7f,0}
};
static const Key row5[] = {
    {"CTRL",0x14,1.4f,0},{"WIN",0x1F,1.3f,1},{"ALT",0x11,1.3f,0},
    {"SPACE",0x29,6.4f,0},{"ALT GR",0x11,1.4f,1},
    {"WIN",0x27,1.3f,1},{"CTRL",0x14,1.5f,1}
};
static void draw_row(SDL_Renderer *r, TTF_Font *f, float y,
                     const Key *keys, size_t n,const PPA1ModuleInfo *info) {
    const float unit=28.0f,gap=2.5f;
    float x=23;
    for(size_t i=0;i<n;i++) {
        float width=unit*keys[i].width;
        unsigned idx=(unsigned)keys[i].code|(keys[i].ext?0x80u:0u);
        bool held=info->ps2_pressed[idx]!=0;
        SDL_FRect rect={x,y,width-2,34};
        SDL_SetRenderDrawColor(r,held?84:55,held?166:62,held?99:68,255);
        SDL_RenderFillRect(r,&rect);
        SDL_SetRenderDrawColor(r,held?184:121,held?249:132,held?180:127,255);
        SDL_RenderRect(r,&rect);
        ppa_mod_text(r,f,x+4,y+10,held?(SDL_Color){255,255,255,255}:
                                  (SDL_Color){207,213,211,255},keys[i].name);
        x+=width+gap;
    }
}
void ppa_ps2_gui_draw(SDL_Renderer *r,TTF_Font *font,
                      const PPA1ModuleInfo *info,int slot) {
    if(!r || !info) return;
    ppa_mod_face(r,"PPA-PS/2/1 | QWERTY KEYBOARD",font);
    char text[160];
    snprintf(text,sizeof(text),"SLOT %d / %s / SET 2 / %s",
             slot+1,info->active?"ACTIVE":"DISABLED",
             info->ps2_scanning?"SCANNING":"STOPPED");
    ppa_mod_text(r,font,28,83,(SDL_Color){147,226,161,255},text);
    SDL_FRect base={16,116,508,271};
    SDL_SetRenderDrawColor(r,27,31,31,255);
    SDL_RenderFillRect(r,&base);
    draw_row(r,font,128,row0,sizeof(row0)/sizeof(row0[0]),info);
    draw_row(r,font,169,row1,sizeof(row1)/sizeof(row1[0]),info);
    draw_row(r,font,210,row2,sizeof(row2)/sizeof(row2[0]),info);
    draw_row(r,font,251,row3,sizeof(row3)/sizeof(row3[0]),info);
    draw_row(r,font,292,row4,sizeof(row4)/sizeof(row4[0]),info);
    draw_row(r,font,333,row5,sizeof(row5)/sizeof(row5[0]),info);
    SDL_FRect led={33,402,13,13};
    SDL_SetRenderDrawColor(r,info->ps2_scanning?40:68,info->ps2_scanning?215:53,56,255);
    SDL_RenderFillRect(r,&led);
    snprintf(text,sizeof(text),"FIFO: %u  |  LAST SET2: %02Xh  |  %s",
             info->ps2_count,info->ps2_last,info->ps2_overflow?"OVERFLOW":"READY");
    ppa_mod_text(r,font,54,400,(SDL_Color){213,226,217,255},text);
    ppa_mod_text(r,font,29,438,(SDL_Color){198,215,208,255},
                 "CLICK HERE, THEN TYPE ON YOUR PHYSICAL KEYBOARD");
    ppa_mod_text(r,font,29,459,(SDL_Color){169,182,176,255},
                 "MAKE/BREAK SCANCODES -> PPA-BUS/1 FIFO");
    ppa_mod_text(r,font,29,480,(SDL_Color){132,151,140,255},
                 "0010 DATA  0011 STATUS  0012 CTRL  0013 CMD");
}
