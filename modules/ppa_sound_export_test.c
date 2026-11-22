#include "ppa_sound_export.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t little32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int main(void) {
    const char *path = "out/ppa_sound_export_test.wav";
    PpaSoundExport rec = {0};
    const int32_t stems[] = {
        0, 0x123456,
        0, -1,
        0, -8388608
    };
    assert(ppa_sound_export_start(&rec, path, PPA_SOUND_FMT_WAV24, 44100));
    assert(rec.active);
    assert(ppa_sound_export_write(&rec, stems, 3, 2, 1));
    assert(rec.frames == 3);
    assert(ppa_sound_export_stop(&rec));
    assert(!rec.active);

    unsigned char bytes[53] = {0};
    FILE *f = fopen(path, "rb");
    assert(f && fread(bytes, 1, sizeof(bytes), f) == sizeof(bytes));
    assert(fgetc(f) == EOF);
    fclose(f);
    assert(memcmp(bytes, "RIFF", 4) == 0);
    assert(little32(bytes + 4) == 45);
    assert(memcmp(bytes + 8, "WAVEfmt ", 8) == 0);
    assert(bytes[20] == 1 && bytes[22] == 1);
    assert(little32(bytes + 24) == 44100);
    assert(little32(bytes + 28) == 132300);
    assert(bytes[34] == 24);
    assert(memcmp(bytes + 36, "data", 4) == 0);
    assert(little32(bytes + 40) == 9);
    assert(bytes[44] == 0x56 && bytes[45] == 0x34 && bytes[46] == 0x12);
    assert(bytes[47] == 0xFF && bytes[48] == 0xFF && bytes[49] == 0xFF);
    assert(bytes[50] == 0 && bytes[51] == 0 && bytes[52] == 0x80);
    remove(path);
    puts("PPA-SOUND: 24-bit WAV export (format, size, slot isolation): OK");
    return 0;
}
