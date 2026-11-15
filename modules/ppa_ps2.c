#include "ppa_ps2.h"
#include <string.h>

static void enqueue(PpaModule *m, uint8_t c) {
    if(m->ps2_count >= PPAM_PS2_FIFO_SIZE) {
        m->ps2_overflow = true;
        return;
    }
    m->ps2_fifo[m->ps2_tail] = c;
    m->ps2_tail = (uint8_t)(m->ps2_tail + 1u);
    m->ps2_count++;
}
static void flush(PpaModule *m) {
    m->ps2_head=m->ps2_tail=m->ps2_count=0;
    m->ps2_overflow=false;
}
void ppa_ps2_reset(PpaModule *m) {
    if(!m || m->type!=PPAM_TYPE_PS2) return;
    memset(m->ps2_pressed,0,sizeof(m->ps2_pressed));
    flush(m);
    m->ps2_scanning=true;
    m->ps2_wait_led=false;
    m->ps2_leds=0;
    m->ps2_last=0;
}
uint8_t ppa_ps2_read(PpaModule *m,uint32_t offset) {
    if(!m || m->type!=PPAM_TYPE_PS2) return 0xFF;
    switch(offset) {
        case PPA_PS2_DATA: {
            if(!m->ps2_count) return 0;
            uint8_t v=m->ps2_fifo[m->ps2_head];
            m->ps2_head=(uint8_t)(m->ps2_head+1u);
            m->ps2_count--;
            return v;
        }
        case PPA_PS2_STATUS:
            return (uint8_t)((m->ps2_count?1u:0u) |
                (m->ps2_overflow?2u:0u) | (m->ps2_scanning?4u:0u));
        case PPA_PS2_CONTROL: return m->ps2_scanning?1u:0u;
        case PPA_PS2_COUNT: return (uint8_t)(m->ps2_count>255u?255u:m->ps2_count);
        case PPA_PS2_LAST: return m->ps2_last;
        case PPA_PS2_LEDS: return m->ps2_leds;
        default: return 0xFF;
    }
}
void ppa_ps2_write(PpaModule *m,uint32_t offset,uint8_t value) {
    if(!m || m->type!=PPAM_TYPE_PS2) return;
    if(offset==PPA_PS2_CONTROL) {
        m->ps2_scanning=(value&1u)!=0;
        if(value&0x80u) flush(m);
        if(!m->ps2_scanning)
            memset(m->ps2_pressed,0,sizeof(m->ps2_pressed));
    } else if(offset==PPA_PS2_COMMAND) {
        if(m->ps2_wait_led) {
            m->ps2_leds=value&7u;
            m->ps2_wait_led=false;
            enqueue(m,0xFA);
            return;
        }
        switch(value) {
            case 0xFF: /* keyboard reset: ACK + BAT success */
                ppa_ps2_reset(m);
                enqueue(m,0xFA); enqueue(m,0xAA); break;
            case 0xF4: m->ps2_scanning=true; enqueue(m,0xFA); break;
            case 0xF5: m->ps2_scanning=false; enqueue(m,0xFA); break;
            case 0xED: m->ps2_wait_led=true; enqueue(m,0xFA); break;
            case 0xF2: /* identify MF2 keyboard */
                enqueue(m,0xFA); enqueue(m,0xAB); enqueue(m,0x83); break;
            case 0xEE: enqueue(m,0xEE); break; /* echo */
            default: enqueue(m,0xFE); break; /* resend/unsupported */
        }
    }
}
void ppa_ps2_key(PpaModule *m,uint8_t scan_set2,bool extended,bool down) {
    if(!m || !m->installed || !m->active ||
       m->type!=PPAM_TYPE_PS2 || !m->ps2_scanning || !scan_set2) return;
    unsigned idx=scan_set2|(extended?0x80u:0u);
    if((m->ps2_pressed[idx]!=0)==down) return;
    m->ps2_pressed[idx]=down;
    m->ps2_last=scan_set2;
    if(extended) enqueue(m,0xE0);
    if(!down) enqueue(m,0xF0);
    enqueue(m,scan_set2);
}
