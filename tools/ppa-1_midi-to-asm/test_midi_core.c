/* Native MIDI parser / four-voice optimization regression tests. */
#define _POSIX_C_SOURCE 200809L
#include "midi_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/wait.h>
#include <sys/stat.h>

static void u32be(FILE *f, unsigned v) {
    fputc((v >> 24) & 255, f);
    fputc((v >> 16) & 255, f);
    fputc((v >> 8) & 255, f);
    fputc(v & 255, f);
}
static void start_song(FILE *f, unsigned n) {
    fwrite("MThd", 1, 4, f); u32be(f, 6);
    fputc(0, f); fputc(n == 1 ? 0 : 1, f);
    fputc((n >> 8) & 255, f); fputc(n & 255, f);
    fputc(1, f); fputc(0xE0, f); /* 480 PPQN */
}
static void vlq(FILE *f, uint32_t v) {
    uint8_t data[4]; int n = 0;
    data[n++] = v & 127;
    while ((v >>= 7) && n < 4) data[n++] = (v & 127) | 128;
    while (n--) fputc(data[n], f);
}
static void simple_track(FILE *f, const char *name, unsigned ch,
                         unsigned delay_before, unsigned duration) {
    /* Track payload can be sized in advance: name event, on/off, EOT. */
    size_t name_len = strlen(name);
    assert(name_len < 60);
    char path[64];
    (void)path;
    unsigned first_size = delay_before > 127 ? delay_before > 16383 ? 3 : 2 : 1;
    unsigned duration_size = duration > 127 ? duration > 16383 ? 3 : 2 : 1;
    unsigned len = (unsigned)(4 + name_len + first_size + 3 + duration_size + 3 + 4);
    fwrite("MTrk", 1, 4, f); u32be(f, len);
    fputc(0, f); fputc(0xFF, f); fputc(3, f);
    fputc((int)name_len, f); fwrite(name, 1, name_len, f);
    vlq(f, delay_before); fputc(0x90 | (ch & 15), f); fputc(60 + ch, f); fputc(110, f);
    vlq(f, duration); fputc(0x80 | (ch & 15), f); fputc(60 + ch, f); fputc(0, f);
    fputc(0, f); fputc(0xFF, f); fputc(0x2F, f); fputc(0, f);
}
static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long len = ftell(f);
    assert(len > 0);
    rewind(f);
    char *data = malloc((size_t)len + 1);
    assert(data && fread(data, 1, (size_t)len, f) == (size_t)len);
    data[len] = 0;
    fclose(f);
    return data;
}
static void two_tracks(void) {
    char midi[] = "/tmp/ppa-midi-core-XXXXXX";
    int fd = mkstemp(midi); assert(fd >= 0);
    FILE *f = fdopen(fd, "wb"); assert(f);
    start_song(f, 2);
    simple_track(f, "Lead", 0, 0, 480);
    simple_track(f, "Bass", 1, 480, 480);
    assert(fclose(f) == 0);

    PpaMidiTrack tracks[8]; int count = 0;
    char error[256] = "";
    assert(ppa_midi_open(midi, tracks, 8, &count, error, sizeof(error)));
    assert(count == 2);
    assert(tracks[0].notes == 1 && tracks[1].notes == 1);
    assert(tracks[0].enabled && tracks[1].enabled);
    tracks[0].wave = 2; /* triangle /\/\/\ */
    tracks[1].wave = 1; /* saw /|/| */
    char out[] = "/tmp/ppa-midi-core-asm-XXXXXX";
    fd = mkstemp(out); assert(fd >= 0); close(fd);
    int frames = 0, banks = 0;
    assert(ppa_midi_export(midi, tracks, count, true, out, 100000, 10,
                           &frames, &banks, error, sizeof(error)));
    assert(frames >= 2 && banks == 1);
    char *asmcode = read_all(out);
    assert(strstr(asmcode, ".bank 0001"));
    assert(strstr(asmcode, "ST $0022,A")); /* dynamic waveform */
    assert(strstr(asmcode, "midi_bank_next:"));
    assert(strstr(asmcode, ".byte $00,$00 ; song finished"));
    free(asmcode);
    unlink(out); unlink(midi);
}
static void long_song(void) {
    char midi[] = "/tmp/ppa-midi-long-XXXXXX";
    int fd = mkstemp(midi); assert(fd >= 0);
    FILE *f = fdopen(fd, "wb"); assert(f);
    start_song(f, 1);
    simple_track(f, "200 seconds", 0, 0, 480 * 400);
    assert(fclose(f) == 0);
    PpaMidiTrack tracks[8]; int count = 0;
    char error[256] = "";
    assert(ppa_midi_open(midi, tracks, 8, &count, error, sizeof(error)));
    char out[] = "/tmp/ppa-midi-long-asm-XXXXXX";
    fd = mkstemp(out); assert(fd >= 0); close(fd);
    int frames = 0, banks = 0;
    assert(ppa_midi_export(midi, tracks, count, false, out, 100000, 10,
                           &frames, &banks, error, sizeof(error)));
    assert(frames >= 1 && banks >= 1);
    unlink(out); unlink(midi);
}
static void run_tool(const char *exec, const char *input,
                     const char *output, const char *option) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        if (option) execl(exec, exec, input, output, option, (char *)NULL);
        else execl(exec, exec, input, output, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void multi_bank_song(void) {
    /* 10,000 alternating note events force >1 music bank. */
    char midi[] = "/tmp/ppa-midi-banked-XXXXXX";
    int fd = mkstemp(midi); assert(fd >= 0);
    FILE *f = fdopen(fd, "wb"); assert(f);
    start_song(f, 1);
    fwrite("MTrk", 1, 4, f);
    u32be(f, 5000u * 8u + 4u);
    for (unsigned i = 0; i < 5000; i++) {
        fputc(120, f); fputc(0x90, f); fputc(60, f); fputc(100, f);
        fputc(120, f); fputc(0x80, f); fputc(60, f); fputc(0, f);
    }
    fputc(0, f); fputc(0xFF, f); fputc(0x2F, f); fputc(0, f);
    assert(fclose(f) == 0);

    PpaMidiTrack tracks[8]; int count = 0;
    char error[256] = "";
    assert(ppa_midi_open(midi, tracks, 8, &count, error, sizeof(error)));
    const char *out = "examples/.test_native_bank.asm";
    int frames = 0, banks = 0;
    assert(ppa_midi_export(midi, tracks, count, false, out, 100000, 10,
                           &frames, &banks, error, sizeof(error)));
    assert(frames > 4681 && banks >= 2);
    char *asmcode = read_all(out);
    assert(strstr(asmcode, ".bank 0001"));
    assert(strstr(asmcode, ".bank 0002"));
    assert(strstr(asmcode, ".byte $FF,$FF ; next ROM bank"));
    free(asmcode);

    /* Compile the native C ASM and verify exact bank-aligned PRG/ROMPACK. */
    char prg[] = "/tmp/ppa-native-prg-XXXXXX";
    fd = mkstemp(prg); assert(fd >= 0); close(fd);
    char rom[] = "/tmp/ppa-native-rom-XXXXXX";
    fd = mkstemp(rom); assert(fd >= 0); close(fd);
    run_tool("./out/asm/ppa-1_asm", out, prg, "--program");
    struct stat stats;
    assert(stat(prg, &stats) == 0 && stats.st_size >= 3 * 65536);
    run_tool("./out/tools/ppa_rompack", prg, rom, NULL);
    assert(stat(rom, &stats) == 0 && stats.st_size == 2 * 1024 * 1024);

    unlink(prg); unlink(rom); unlink(out); unlink(midi);
}

/* A five-note chord within ONE MIDI track must spread across all four
 * oscillators. HIGH priority should select notes 76, 72, 67, 64,
 * leaving the lowest note 60 out until a voice becomes free. */
static void one_track_polyphony(void) {
    char midi[] = "/tmp/ppa-midi-poly-XXXXXX";
    int fd = mkstemp(midi); assert(fd >= 0);
    FILE *f = fdopen(fd, "wb"); assert(f);
    start_song(f, 1);
    fwrite("MTrk", 1, 4, f);
    u32be(f, 5u * 4u + (5u + 4u * 4u) + 4u);
    const unsigned notes[] = {60, 64, 67, 72, 76};
    for (unsigned i = 0; i < 5; ++i) {
        fputc(0, f); fputc(0x90, f); fputc(notes[i], f); fputc(100, f);
    }
    for (unsigned i = 0; i < 5; ++i) {
        if (i == 0) vlq(f, 480);
        else fputc(0, f);
        fputc(0x80, f); fputc(notes[i], f); fputc(0, f);
    }
    fputc(0, f); fputc(0xFF, f); fputc(0x2F, f); fputc(0, f);
    assert(fclose(f) == 0);
    PpaMidiTrack tracks[8]; int count = 0;
    char error[256] = "";
    assert(ppa_midi_open(midi, tracks, 8, &count, error, sizeof(error)));
    assert(count == 1 && tracks[0].notes == 5);
    PpaMidiPreview preview = {0};
    assert(ppa_midi_preview(midi, tracks, count, true, &preview,
                            error, sizeof(error)));
    assert(preview.peak_polyphony[0] == 5);
    assert(preview.max_simultaneous_notes == 5);
    int mapped = 0;
    for (int voice = 0; voice < 4; ++voice)
        if (preview.owner[voice][0] == 1) mapped++;
    assert(mapped == 4);
    tracks[0].wave = 2;
    tracks[0].poly = 1; /* high */
    char out[] = "/tmp/ppa-midi-poly-asm-XXXXXX";
    fd = mkstemp(out); assert(fd >= 0); close(fd);
    int frames = 0, banks = 0;
    assert(ppa_midi_export(midi, tracks, count, true, out, 100000, 10,
                           &frames, &banks, error, sizeof(error)));
    char *asmcode = read_all(out);
    char *data = strstr(asmcode, ".bank 0001");
    assert(data != NULL);
    data = strstr(data, ".byte $");
    assert(data != NULL);
    unsigned char frame[18] = {0};
    unsigned k = 0;
    /* The 18-byte optimized frame is split into 16+2 .byte rows. */
    char *cursor = data;
    while (k < 18) {
        char *line = strstr(cursor, ".byte ");
        assert(line != NULL);
        char *end = strchr(line, '\n');
        assert(end != NULL);
        for (char *p = line; p < end && k < 18; ++p) {
            if (*p == '$') {
                unsigned value;
                assert(sscanf(p + 1, "%2x", &value) == 1);
                frame[k++] = (unsigned char)value;
            }
        }
        cursor = end + 1;
    }
    const unsigned frequencies[] = {659, 523, 392, 330};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned offset = 2 + i * 4;
        unsigned hz = frame[offset] | ((unsigned)frame[offset + 1] << 8);
        assert(hz == frequencies[i]);
        assert(frame[offset + 2] > 0);
        assert(frame[offset + 3] == 2); /* triangle travels with note */
    }
    free(asmcode);
    unlink(out); unlink(midi);
}

int main(void) {
    two_tracks();
    one_track_polyphony();
    long_song();
    multi_bank_song();
    puts("PPA-1 MIDI native C parser and optimizer: OK");
    return 0;
}
