#ifndef PPA_SOUND_EXPORT_H
#define PPA_SOUND_EXPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* PCM is produced at 24-bit precision by the PPA-SOUND mixer.
 * WAV works without external libraries. FLAC/OGG/MP3 use ffmpeg
 * AFTER recording, so encoding cannot stall the emulated CPU. */
typedef enum {
    PPA_SOUND_FMT_WAV24 = 0,
    PPA_SOUND_FMT_FLAC24,
    PPA_SOUND_FMT_OGG,
    PPA_SOUND_FMT_MP3,
    PPA_SOUND_FMT_COUNT
} PpaSoundExportFormat;

typedef struct {
    FILE *wav;
    PpaSoundExportFormat format;
    uint32_t sample_rate;
    uint64_t frames;
    bool active;
    bool failed;
    char path[1024];
    char temporary_path[1100];
    char status[128];
} PpaSoundExport;

const char *ppa_sound_export_format_name(PpaSoundExportFormat format);
const char *ppa_sound_export_extension(PpaSoundExportFormat format);
bool ppa_sound_export_start(PpaSoundExport *rec, const char *path,
                            PpaSoundExportFormat fmt, uint32_t sample_rate);
/* 24-bit signed samples in int32, interleaved per frame across slot_count slots. */
bool ppa_sound_export_write(PpaSoundExport *rec, const int32_t *stems,
                            uint32_t frames, uint32_t slot_count, uint32_t slot);
bool ppa_sound_export_stop(PpaSoundExport *rec);

#endif
