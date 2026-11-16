#include "ppa_module.h"
#include "ppa_ps2.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    PpaModuleBus b;
    ppam_bus_init(&b);
    assert(ppam_load(&b, 0, "out/modules/light.ppamod"));
    assert(!ppam_get(&b, 0)->active);
    assert(ppam_read(&b, 0x2000, 0x10) == 0xFF);
    assert(ppam_set_active(&b, 0, true));
    assert(ppam_read(&b, 0x2000, 0) == 'P');
    /* Identity is in a ROM-like window, always relative to slot base. */
    assert(ppam_read(&b, 0x2000, 0x02) == PPAM_TYPE_LIGHT);
    assert(ppam_read(&b, 0x2000, 0x03) == 2); /* PPAMOD file format */
    assert(ppam_read(&b, 0x2000, 0x04) == 1);
    assert(ppam_read(&b, 0x2000, 0x05) == 0);
    assert(ppam_read(&b, 0x2000, 0x06) == 0);
    assert(ppam_read(&b, 0x2000, 0x07) == 7); /* LIGHT/1 */
    assert(ppam_read(&b, 0x2000, 0x08) == 0x80);
    assert(ppam_read(&b, 0x2000, 0x09) == 0);
    assert(ppam_read(&b, 0x2000, 0x0A) == 1);
    assert(ppam_read(&b, 0x2000, 0x0B) == 0x20);
    assert(ppam_read(&b, 0x2000, 0x0C) == 0);
    assert(ppam_read(&b, 0x2000, 0x0D) == 1);
    assert(ppam_read(&b, 0x2000, 0x80) == 'L');
    assert(ppam_read(&b, 0x2000, 0x86) == '1');
    assert(ppam_read(&b, 0x2000, 0x87) == 0);
    ppam_write(&b, 0x2000, 0x80, 0x41);
    ppam_write(&b, 0x2000, 0x04, 0x41);
    assert(ppam_read(&b, 0x2000, 0x80) == 'L');
    assert(ppam_read(&b, 0x2000, 0x04) == 1);
    assert(ppam_find_type(&b, PPAM_TYPE_LIGHT, 0) == 0);
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 0) == -1);
    assert(ppam_count_type(&b, PPAM_TYPE_LIGHT) == 1);
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
    assert(ppam_load(&b, 1, "out/modules/ppa-sound-1.ppamod"));
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

    assert(ppam_load(&b, 2, "out/modules/ppa-vga-1.ppamod"));
    assert(ppam_get(&b, 2)->type == PPAM_TYPE_VGA);
    assert(ppam_set_active(&b, 2, true));
    assert(ppam_read(&b, 0x4000, 0x18) == 0x40);
    assert(ppam_read(&b, 0x4000, 0x19) == 0x01);
    assert(ppam_read(&b, 0x4000, 0x1A) == 0xC8);
    ppam_write(&b, 0x4000, 0x10, 1);
    assert(ppam_get(&b, 2)->vga_enabled);
    ppam_write(&b, 0x4001, 0x0000, 0xE0);
    ppam_write(&b, 0x4001, 0xF9FF, 0x03);
    assert(ppam_read(&b, 0x4001, 0x0000) == 0xE0);
    assert(ppam_read(&b, 0x4001, 0xF9FF) == 0x03);
    assert(ppam_read(&b, 0x4001, 0xFA00) == 0xFF);

    ppam_write(&b, 0x4000, 0x14, 7);
    ppam_write(&b, 0x4000, 0x15, 0x12);
    ppam_write(&b, 0x4000, 0x16, 0x34);
    ppam_write(&b, 0x4000, 0x17, 0x56);
    assert(ppam_get(&b, 2)->vga_palette[7 * 3 + 0] == 0x12);
    assert(ppam_get(&b, 2)->vga_palette[7 * 3 + 1] == 0x34);
    assert(ppam_get(&b, 2)->vga_palette[7 * 3 + 2] == 0x56);
    assert(ppam_get(&b, 2)->vga_palette_index == 8);

    /* IBM VGA-compatible PPA register mode numbers (not BIOS calls). */
    ppam_write(&b, 0x4000, 0x12, 0x13);  /* mode 13h */
    assert(ppam_get(&b, 2)->vga_width == 320);
    assert(ppam_get(&b, 2)->vga_height == 200);
    assert(ppam_get(&b, 2)->vga_bpp == 8);
    ppam_write(&b, 0x4000, 0x12, 0x04);  /* 320x200 CGA-like 4 colors */
    assert(ppam_get(&b, 2)->vga_bpp == 2);
    assert(ppam_get(&b, 2)->vga_vram_size == 16000);
    ppam_write(&b, 0x4000, 0x12, 0x11);  /* 640x480 monochrome */
    assert(ppam_get(&b, 2)->vga_bpp == 1);
    assert(ppam_get(&b, 2)->vga_vram_size == 38400);
    ppam_write(&b, 0x4000, 0x12, 0x0D);  /* 320x200 EGA 16 colors */
    assert(ppam_get(&b, 2)->vga_vram_size == 32000);
    ppam_write(&b, 0x4000, 0x20, 3);
    ppam_write(&b, 0x4001, 0, 0x80);
    ppam_write(&b, 0x4000, 0x21, 3);
    assert(ppam_read(&b, 0x4001, 0) == 0x80);
    ppam_write(&b, 0x4000, 0x12, 0x12);  /* mode 12h */
    assert(ppam_get(&b, 2)->vga_width == 640);
    assert(ppam_get(&b, 2)->vga_height == 480);
    assert(ppam_get(&b, 2)->vga_vram_size == 153600);
    ppam_write(&b, 0x4000, 0x20, 2);    /* select write plane 2 */
    ppam_write(&b, 0x4001, 0x0000, 0x80);
    ppam_write(&b, 0x4000, 0x21, 2);    /* select read plane 2 */
    assert(ppam_read(&b, 0x4001, 0x0000) == 0x80);
    ppam_write(&b, 0x4000, 0x21, 0);
    assert(ppam_read(&b, 0x4001, 0x0000) == 0);
    ppam_write(&b, 0x4000, 0x12, 0x02); /* 80x25 character/attribute memory */
    assert(ppam_get(&b, 2)->vga_text);
    assert(ppam_read(&b, 0x4001, 0) == ' ');
    assert(ppam_read(&b, 0x4001, 1) == 7);
    ppam_write(&b, 0x4001, 0, 'A');
    ppam_write(&b, 0x4001, 1, 0x1E);
    assert(ppam_read(&b, 0x4001, 0) == 'A');
    assert(ppam_read(&b, 0x4001, 1) == 0x1E);

    ppam_write(&b, 0x4000, 0x12, 0x83); /* Full HD RGB888 */
    assert(ppam_get(&b, 2)->vga_width == 1920);
    assert(ppam_get(&b, 2)->vga_height == 1080);
    assert(ppam_get(&b, 2)->vga_vram_size == 6220800u);
    assert(ppam_read(&b, 0x4000, 0x1E) == 24);
    ppam_write(&b, 0x4001, 0, 0x22);   /* pixel 0 red byte */
    ppam_write(&b, 0x4000, 0x1C, 1);   /* next 64 KiB VRAM page */
    ppam_write(&b, 0x4001, 0, 0xAB);
    assert(ppam_read(&b, 0x4001, 0) == 0xAB);
    ppam_write(&b, 0x4000, 0x1C, 0);
    assert(ppam_read(&b, 0x4001, 0) == 0x22);
    ppam_write(&b, 0x4000, 0x12, 0x55); /* unsupported -> unchanged */
    assert(ppam_read(&b, 0x4000, 0x12) == 0x83);
    ppam_write(&b, 0x4000, 0x1C, 0xFF);
    assert(ppam_read(&b, 0x4001, 0) == 0xFF);

    ppam_reset(&b);
    assert(!ppam_get(&b, 2)->vga_enabled);
    assert(ppam_read(&b, 0x4001, 0x0000) == 0x00);

    /* PPA-PS/2/1: Set 2 make/break and extended keys through slot registers. */
    assert(ppam_load(&b, 3, "out/modules/ppa-ps2-1.ppamod"));
    assert(ppam_get(&b, 3)->type == PPAM_TYPE_PS2);
    assert(!ppam_get(&b, 3)->active);
    assert(ppam_read(&b, 0x5000, PPA_PS2_STATUS) == 0xFF);
    assert(ppam_set_active(&b, 3, true));
    assert(ppam_read(&b, 0x5000, 0x02) == PPAM_TYPE_PS2);
    assert(ppam_read(&b, 0x5000, PPA_PS2_STATUS) == 4);
    ppa_ps2_key(&b.slots[3], 0x15, false, true);  /* Q press */
    ppa_ps2_key(&b.slots[3], 0x15, false, true);  /* OS repeat filtered */
    assert(ppam_read(&b, 0x5000, PPA_PS2_COUNT) == 1);
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0x15);
    ppa_ps2_key(&b.slots[3], 0x15, false, false);
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xF0);
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0x15);
    assert(ppam_read(&b, 0x5000, PPA_PS2_STATUS) == 4);
    ppa_ps2_key(&b.slots[3], 0x75, true, true); /* arrow up */
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xE0);
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0x75);
    ppa_ps2_key(&b.slots[3], 0x75, true, false);
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xE0);
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xF0);
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0x75);
    ppam_write(&b, 0x5000, PPA_PS2_COMMAND, 0xED); /* set LEDs */
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xFA);
    ppam_write(&b, 0x5000, PPA_PS2_COMMAND, 0x04); /* Caps Lock */
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xFA);
    assert(ppam_read(&b, 0x5000, PPA_PS2_LEDS) == 4);
    ppam_write(&b, 0x5000, PPA_PS2_COMMAND, 0xF5); /* disable scans */
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xFA);
    ppa_ps2_key(&b.slots[3], 0x1C, false, true);
    assert(ppam_read(&b, 0x5000, PPA_PS2_COUNT) == 0);
    ppam_write(&b, 0x5000, PPA_PS2_COMMAND, 0xFF); /* reset, BAT */
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xFA);
    assert(ppam_read(&b, 0x5000, PPA_PS2_DATA) == 0xAA);
    assert(ppam_read(&b, 0x5000, PPA_PS2_LEDS) == 0);
    ppam_reset(&b);
    assert(ppam_read(&b, 0x5000, PPA_PS2_COUNT) == 0);

    /* Discovery chooses active cards and respects duplicate types. */
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 0) == 1);
    assert(ppam_find_type(&b, PPAM_TYPE_VGA, 0) == 2);
    assert(ppam_find_type(&b, PPAM_TYPE_PS2, 0) == 3);
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 1) == -1);
    assert(ppam_read(&b, 0x3000, 0x80) == 'P');
    assert(ppam_read(&b, 0x4000, 0x80) == 'P');
    assert(ppam_read(&b, 0x5000, 0x80) == 'P');
    assert(ppam_read(&b, 0x4000, 0x0B) == 0x40);
    assert(ppam_read(&b, 0x5000, 0x0A) == 4);

    assert(ppam_load(&b, 4, "out/modules/ppa-sound-1.ppamod"));
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 1) == -1);
    assert(ppam_set_active(&b, 4, true));
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 0) == 1);
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 1) == 4);
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 2) == -1);
    assert(ppam_count_type(&b, PPAM_TYPE_SOUND) == 2);
    assert(ppam_read(&b, 0x6000, 0x0B) == 0x60);
    assert(ppam_read(&b, 0x6000, 0x0A) == 5);
    ppam_set_active(&b, 1, false);
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 0) == 4);
    assert(ppam_count_type(&b, PPAM_TYPE_SOUND) == 1);
    ppam_eject(&b, 4);
    assert(ppam_find_type(&b, PPAM_TYPE_SOUND, 0) == -1);

    /* Legacy v1 cards still expose synthesized identity, without requiring
     * a new image conversion or changing the original 24-byte header. */
    const char *legacy_path = "out/modules/legacy-v1-test.ppamod";
    const uint8_t legacy_image[24] = {
        'P','P','A','M', 1,0, 1,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0
    };
    FILE *legacy = fopen(legacy_path, "wb");
    assert(legacy != NULL);
    assert(fwrite(legacy_image, 1, sizeof(legacy_image), legacy) ==
           sizeof(legacy_image));
    assert(fclose(legacy) == 0);
    assert(ppam_load(&b, 0, legacy_path));
    assert(ppam_set_active(&b, 0, true));
    assert(ppam_read(&b, 0x2000, 0x03) == 1);
    assert(ppam_read(&b, 0x2000, 0x04) == 1);
    assert(ppam_read(&b, 0x2000, 0x80) == 'L');
    ppam_eject(&b, 0);
    assert(remove(legacy_path) == 0);

    ppam_bus_destroy(&b);
    puts("PPAMOD v1/v2 identity/discovery LIGHT/SOUND/VGA/PS2 tests OK");
    return 0;
}
