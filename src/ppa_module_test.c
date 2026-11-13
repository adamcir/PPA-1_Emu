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
    ppam_bus_destroy(&b);
    puts("PPAMOD LIGHT loader/bus/pulse tests OK");
    return 0;
}
