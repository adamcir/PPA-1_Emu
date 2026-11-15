#ifndef PPA_MONITOR_H
#define PPA_MONITOR_H
#include "ppa_module_ui.h"

typedef enum {
    PPA_MONITOR_OFF,
    PPA_MONITOR_NO_SIGNAL,
    PPA_MONITOR_ENTERING_SLEEP,
    PPA_MONITOR_SLEEP,
    PPA_MONITOR_ACTIVE
} PpaMonitorState;

typedef enum { PPA_MONITOR_LCD, PPA_MONITOR_CRT } PpaMonitorStyle;

typedef struct {
    bool initialized, power_on, auto_sleep, osd_open, info_open;
    int brightness, contrast, saturation, sharpness, gamma;
    int selected;
    PpaMonitorStyle style;
    PpaMonitorState state;
    Uint64 no_signal_since, popup_until;
    bool last_signal;
} PpaMonitor;

/* One independent physical monitor per PPAM slot. Never changes card VRAM. */
PpaMonitor *ppa_monitor_for_slot(int slot);
void ppa_monitor_tick(PpaMonitor *m, const PPA1ModuleInfo *info);
void ppa_monitor_button(int slot, float x, float y);
uint32_t ppa_monitor_adjust_pixel(const PpaMonitor *m, uint32_t rgba);
void ppa_monitor_draw(SDL_Renderer *r, TTF_Font *font, const PPA1ModuleInfo *info,
                      int slot, const SDL_FRect *screen);
#endif
