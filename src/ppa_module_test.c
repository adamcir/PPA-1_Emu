#include "ppa_module.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    PpaModuleBus b;
    ppam_bus_init(&b);
    assert(ppam_load(&b, 0, "modules/light.ppamod"));
    assert(!ppam_get(&b, 0)->active);
    assert(ppam_read(&b, 0x2000, 0x10) == 0xFF);
    assert(ppam_set_active(&b, 0, true));
    assert(ppam_read(&b, 0x2000, 0) == 'P');
    assert(ppam_read(&b, 0x2000, 0x10) == 0);
    ppam_write(&b, 0x2000, 0x10, 1);
    assert(ppam_get(&b, 0)->light_on);
    ppam_tick(&b, 999);
    assert(ppam_get(&b, 0)->light_on);
    ppam_tick(&b, 1);
    assert(!ppam_get(&b, 0)->light_on);
    ppam_write(&b, 0x2000, 0x10, 1);
    ppam_write(&b, 0x2000, 0x10, 0);
    assert(!ppam_get(&b, 0)->light_on);
    assert(ppam_read(&b, 0x3000, 0x10) == 0xFF);
    assert(ppam_set_active(&b, 0, false));
    ppam_eject(&b, 0);
    assert(!ppam_get(&b, 0)->installed);
    /* SOUND/1: four-voice oscillator exposed through slot-relative registers. */
    assert(ppam_load(&b, 1, "modules/ppa-sound-1.ppamod"));
    assert(ppam_get(&b, 1)->type == PPAM_TYPE_SOUND);
    assert(ppam_set_active(&b, 1, true));
    ppam_write(&b, 0x3000, 0x10, 1);   /* master ON */
    ppam_write(&b, 0x3000, 0x11, 255); /* master volume */
    ppam_write(&b, 0x3000, 0x20, 0xB8); /* 440 Hz low */
    ppam_write(&b, 0x3000, 0x21, 0x01); /* 440 Hz high */
    ppam_write(&b, 0x3000, 0x22, 3);    /* sine */
    ppam_write(&b, 0x3000, 0x23, 255);
    ppam_write(&b, 0x3000, 0x24, 1);
    assert(ppam_read(&b, 0x3000, 0x20) == 0xB8);
    assert(ppam_read(&b, 0x3000, 0x21) == 0x01);
    int16_t samples[512] = {0};
    ppam_mix_audio(&b, samples, 512, 44100);
    bool audible = false;
    for (int i = 0; i < 512; ++i)
        if (samples[i]) { audible = true; break; }
    assert(audible);
    ppam_write(&b, 0x3000, 0x10, 0); /* mute all */
    ppam_mix_audio(&b, samples, 512, 44100);
    for (int i = 0; i < 512; ++i) assert(samples[i] == 0);
    ppam_bus_destroy(&b);
    puts("PPAMOD LIGHT loader/bus/pulse tests OK");
    return 0;
}
