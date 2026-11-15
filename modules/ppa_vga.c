#include "ppa_vga.h"
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t id, bpp, text; uint16_t w,h; uint32_t bytes; } Mode;
static const Mode modes[] = {
    {0x00,8,0,320,200,64000}, /* retained PPA-VGA/1 legacy mode */
    {0x01,4,1,320,400,2000}, {0x02,4,1,640,400,4000},
    {0x03,4,1,640,400,4000}, {0x04,2,0,320,200,16000},
    {0x05,2,0,320,200,16000}, {0x06,1,0,640,200,16000},
    {0x07,4,1,640,400,4000}, {0x0D,4,0,320,200,32000},
    {0x0E,4,0,640,200,64000}, {0x0F,1,0,640,350,28000},
    {0x10,4,0,640,350,112000}, {0x11,1,0,640,480,38400},
    {0x12,4,0,640,480,153600},
    {0x13,8,0,320,200,64000}, {0x80,24,0,800,600,1440000},
    {0x81,24,0,1024,768,2359296}, {0x82,24,0,1280,720,2764800},
    {0x83,24,0,1920,1080,6220800}
};
static bool mode_set(PpaModule *m, uint8_t id) {
    for (unsigned k=0;k<sizeof(modes)/sizeof(modes[0]);k++) {
        const Mode *v=&modes[k];
        if(v->id!=id) continue;
        uint8_t *next=calloc(v->bytes,1);
        if(!next) return false;
        free(m->vga_vram);
        m->vga_vram=next; m->vga_vram_size=v->bytes;
        m->vga_width=v->w; m->vga_height=v->h;
        m->vga_mode=id; m->vga_bpp=v->bpp; m->vga_text=v->text;
        m->vga_page=0; m->vga_plane_read=0; m->vga_plane_write=0;
        if(v->text) for(unsigned i=0;i<(unsigned)(v->w/8)*(v->h/16);i++) {
            next[i*2]=' '; next[i*2+1]=7;
        }
        return true;
    }
    return false;
}
static void palette_default(PpaModule *m) {
    static const uint8_t ega[16][3]={
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},
        {170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},
        {255,85,85},{255,85,255},{255,255,85},{255,255,255}};
    for(unsigned i=0;i<256;i++) {
        m->vga_palette[3*i]=(uint8_t)(((i>>5)&7)*255/7);
        m->vga_palette[3*i+1]=(uint8_t)(((i>>2)&7)*255/7);
        m->vga_palette[3*i+2]=(uint8_t)((i&3)*255/3);
    }
    for(unsigned i=0;i<16;i++) memcpy(m->vga_palette+3*i,ega[i],3);
}
void ppa_vga_init(PpaModule *m) { ppa_vga_reset(m); }
void ppa_vga_reset(PpaModule *m) {
    if(!m || m->type!=PPAM_TYPE_VGA) return;
    m->vga_enabled=false; m->vga_border=0; m->vga_palette_index=0;
    palette_default(m);
    (void)mode_set(m,0);
}
static uint32_t vram_pos(const PpaModule *m,uint32_t offset,bool write) {
    uint32_t address=offset-PPA_VGA_VRAM_OFFSET;
    if(m->vga_bpp==4 && !m->vga_text) return
        (uint32_t)(write?m->vga_plane_write:m->vga_plane_read)*
        ((uint32_t)m->vga_width*m->vga_height/8u)+address;
    return (uint32_t)m->vga_page*65536u+address;
}
uint8_t ppa_vga_read(const PpaModule *m,uint32_t offset) {
    if(!m || m->type!=PPAM_TYPE_VGA) return 0xFF;
    if(offset>=PPA_VGA_VRAM_OFFSET && offset<PPA_VGA_VRAM_OFFSET+65536u) {
        uint32_t pos=vram_pos(m,offset,false);
        return m->vga_vram && pos<m->vga_vram_size ? m->vga_vram[pos] : 0xFF;
    }
    switch(offset) {
        case PPA_VGA_REG_CONTROL: return m->vga_enabled?1:0;
        case PPA_VGA_REG_STATUS: return (uint8_t)(1|(m->vga_enabled?2:0));
        case PPA_VGA_REG_MODE: return m->vga_mode;
        case PPA_VGA_REG_BORDER: return m->vga_border;
        case PPA_VGA_REG_DAC_INDEX: return m->vga_palette_index;
        case PPA_VGA_REG_DAC_R: return m->vga_palette[m->vga_palette_index*3u];
        case PPA_VGA_REG_DAC_G: return m->vga_palette[m->vga_palette_index*3u+1];
        case PPA_VGA_REG_DAC_B: return m->vga_palette[m->vga_palette_index*3u+2];
        case PPA_VGA_REG_WIDTH_LO: return (uint8_t)m->vga_width;
        case PPA_VGA_REG_WIDTH_HI: return (uint8_t)(m->vga_width>>8);
        case PPA_VGA_REG_HEIGHT_LO: return (uint8_t)m->vga_height;
        case PPA_VGA_REG_HEIGHT_HI: return (uint8_t)(m->vga_height>>8);
        case PPA_VGA_REG_PAGE_LO: return (uint8_t)m->vga_page;
        case PPA_VGA_REG_PAGE_HI: return (uint8_t)(m->vga_page>>8);
        case PPA_VGA_REG_BPP: return m->vga_bpp;
        case PPA_VGA_REG_PLANE_WRITE: return m->vga_plane_write;
        case PPA_VGA_REG_PLANE_READ: return m->vga_plane_read;
        case PPA_VGA_REG_VRAM_SIZE_LO: return (uint8_t)m->vga_vram_size;
        case PPA_VGA_REG_VRAM_SIZE_MI: return (uint8_t)(m->vga_vram_size>>8);
        case PPA_VGA_REG_VRAM_SIZE_HI: return (uint8_t)(m->vga_vram_size>>16);
        default: return 0xFF;
    }
}
void ppa_vga_write(PpaModule *m,uint32_t offset,uint8_t value) {
    if(!m || m->type!=PPAM_TYPE_VGA) return;
    if(offset>=PPA_VGA_VRAM_OFFSET && offset<PPA_VGA_VRAM_OFFSET+65536u) {
        uint32_t pos=vram_pos(m,offset,true);
        if(m->vga_vram && pos<m->vga_vram_size) m->vga_vram[pos]=value;
        return;
    }
    switch(offset) {
        case PPA_VGA_REG_CONTROL: m->vga_enabled=(value&1)!=0; break;
        case PPA_VGA_REG_MODE: (void)mode_set(m,value); break;
        case PPA_VGA_REG_BORDER: m->vga_border=value; break;
        case PPA_VGA_REG_DAC_INDEX: m->vga_palette_index=value; break;
        case PPA_VGA_REG_DAC_R: m->vga_palette[m->vga_palette_index*3u]=value; break;
        case PPA_VGA_REG_DAC_G: m->vga_palette[m->vga_palette_index*3u+1]=value; break;
        case PPA_VGA_REG_DAC_B:
            m->vga_palette[m->vga_palette_index*3u+2]=value;
            m->vga_palette_index++; break;
        case PPA_VGA_REG_PAGE_LO:
            if(m->vga_bpp!=4 || m->vga_text) m->vga_page=(m->vga_page&0xFF00u)|value;
            break;
        case PPA_VGA_REG_PAGE_HI:
            if(m->vga_bpp!=4 || m->vga_text) m->vga_page=(uint16_t)((m->vga_page&0xFFu)|((uint16_t)value<<8));
            break;
        case PPA_VGA_REG_PLANE_WRITE: m->vga_plane_write=value&3u; break;
        case PPA_VGA_REG_PLANE_READ: m->vga_plane_read=value&3u; break;
        default: break;
    }
}
