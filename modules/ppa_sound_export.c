#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "ppa_sound_export.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <io.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static void le16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static void le32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16);
    p[3] = (uint8_t)(n >> 24);
}

const char *ppa_sound_export_format_name(PpaSoundExportFormat fmt) {
    switch (fmt) {
        case PPA_SOUND_FMT_WAV24: return "WAV 24-bit PCM (lossless)";
        case PPA_SOUND_FMT_FLAC24: return "FLAC 24-bit (lossless)";
        case PPA_SOUND_FMT_OGG: return "OGG Vorbis (high quality)";
        case PPA_SOUND_FMT_MP3: return "MP3 320 kbps";
        default: return "Unknown";
    }
}
const char *ppa_sound_export_extension(PpaSoundExportFormat fmt) {
    switch (fmt) {
        case PPA_SOUND_FMT_WAV24: return "wav";
        case PPA_SOUND_FMT_FLAC24: return "flac";
        case PPA_SOUND_FMT_OGG: return "ogg";
        case PPA_SOUND_FMT_MP3: return "mp3";
        default: return NULL;
    }
}

static bool wav_header(FILE *f, uint32_t sample_rate, uint32_t bytes) {
    uint8_t h[44] = {0};
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36u + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);              /* PCM format chunk */
    le16(h + 20, 1);               /* integer PCM */
    le16(h + 22, 1);               /* single PPA-SOUND module = mono */
    le32(h + 24, sample_rate);
    le32(h + 28, sample_rate * 3u);
    le16(h + 32, 3);               /* block align */
    le16(h + 34, 24);              /* genuine 24-bit synth source */
    memcpy(h + 36, "data", 4);
    le32(h + 40, bytes);
    return fseek(f, 0, SEEK_SET) == 0 &&
           fwrite(h, 1, sizeof(h), f) == sizeof(h);
}

static bool make_temporary(PpaSoundExport *rec) {
    int n = snprintf(rec->temporary_path, sizeof(rec->temporary_path),
                     "%s.ppa-XXXXXX", rec->path);
    if (n < 0 || n >= (int)sizeof(rec->temporary_path)) return false;
#ifdef _WIN32
    if (_mktemp_s(rec->temporary_path, strlen(rec->temporary_path) + 1) != 0)
        return false;
    rec->wav = fopen(rec->temporary_path, "wb+");
    return rec->wav != NULL;
#else
    int fd = mkstemp(rec->temporary_path);
    if (fd < 0) return false;
    rec->wav = fdopen(fd, "wb+");
    if (!rec->wav) {
        close(fd);
        remove(rec->temporary_path);
        return false;
    }
    return true;
#endif
}

bool ppa_sound_export_start(PpaSoundExport *rec, const char *path,
                            PpaSoundExportFormat fmt, uint32_t sample_rate) {
    if (!rec || rec->active || !path || !*path || !sample_rate ||
        sample_rate > UINT32_MAX / 3u ||
        !ppa_sound_export_extension(fmt) || strlen(path) >= sizeof(rec->path))
        return false;
    memset(rec, 0, sizeof(*rec));
    rec->format = fmt;
    rec->sample_rate = sample_rate;
    memcpy(rec->path, path, strlen(path) + 1);

    if (fmt == PPA_SOUND_FMT_WAV24)
        rec->wav = fopen(rec->path, "wb+");
    else if (!make_temporary(rec))
        rec->wav = NULL;

    if (!rec->wav || !wav_header(rec->wav, sample_rate, 0)) {
        if (rec->wav) fclose(rec->wav);
        rec->wav = NULL;
        if (rec->temporary_path[0]) remove(rec->temporary_path);
        snprintf(rec->status, sizeof(rec->status), "Cannot create WAV recording");
        return false;
    }
    rec->active = true;
    snprintf(rec->status, sizeof(rec->status), "RENDERING - press STOP to save");
    return true;
}

bool ppa_sound_export_write(PpaSoundExport *rec, const int32_t *stems,
                            uint32_t frames, uint32_t slot_count, uint32_t slot) {
    if (!rec || !rec->active || !rec->wav || rec->failed ||
        !stems || slot_count == 0 || slot >= slot_count)
        return false;
    if (rec->frames + frames > (UINT32_MAX - 36u) / 3u) {
        rec->failed = true;
        snprintf(rec->status, sizeof(rec->status), "Recording exceeds WAV 4 GB limit");
        return false;
    }
    uint8_t chunk[4096 * 3];
    uint32_t pos = 0;
    while (pos < frames) {
        uint32_t count = frames - pos;
        if (count > 4096) count = 4096;
        for (uint32_t i = 0; i < count; ++i) {
            int32_t sample = stems[((size_t)(pos + i) * slot_count) + slot];
            if (sample < -8388608) sample = -8388608;
            if (sample >  8388607) sample =  8388607;
            uint32_t bits = (uint32_t)sample;
            chunk[3 * i]     = (uint8_t)bits;
            chunk[3 * i + 1] = (uint8_t)(bits >> 8);
            chunk[3 * i + 2] = (uint8_t)(bits >> 16);
        }
        if (fwrite(chunk, 3, count, rec->wav) != count) {
            rec->failed = true;
            snprintf(rec->status, sizeof(rec->status), "Disk write error");
            return false;
        }
        pos += count;
    }
    rec->frames += frames;
    return true;
}

/* Never invoke a shell: the user-selected path is passed as one argv item.
 * Codec conversion happens when STOP is pressed, not during CPU emulation. */
static bool convert_with_ffmpeg(const PpaSoundExport *rec) {
    const char *codec = NULL;
    const char *quality = NULL, *quality_value = NULL;
    const char *format = NULL;
    switch (rec->format) {
        case PPA_SOUND_FMT_FLAC24:
            codec = "flac"; quality = "-compression_level"; quality_value = "8";
            format = "flac"; break;
        case PPA_SOUND_FMT_OGG:
            codec = "libvorbis"; quality = "-q:a"; quality_value = "8";
            format = "ogg"; break;
        case PPA_SOUND_FMT_MP3:
            codec = "libmp3lame"; quality = "-b:a"; quality_value = "320k";
            format = "mp3"; break;
        default: return false;
    }
    const char *args[] = {
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
        "-i", rec->temporary_path,
        "-codec:a", codec, quality, quality_value,
        "-f", format, rec->path, NULL
    };
#ifdef _WIN32
    return _spawnvp(_P_WAIT, "ffmpeg", args) == 0;
#else
    pid_t child = fork();
    if (child == 0) {
        execvp("ffmpeg", (char *const *)args);
        _exit(127);
    }
    if (child < 0) return false;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

bool ppa_sound_export_stop(PpaSoundExport *rec) {
    if (!rec || !rec->active) return false;
    rec->active = false;
    bool ok = !rec->failed &&
        rec->frames <= (UINT32_MAX - 36u) / 3u &&
        wav_header(rec->wav, rec->sample_rate, (uint32_t)(rec->frames * 3u));
    if (fclose(rec->wav) != 0) ok = false;
    rec->wav = NULL;

    if (ok && rec->format != PPA_SOUND_FMT_WAV24)
        ok = convert_with_ffmpeg(rec);
    if (rec->temporary_path[0]) {
        if (ok) remove(rec->temporary_path);
        /* Keep source WAV when ffmpeg is not installed or conversion fails. */
    }
    if (ok)
        snprintf(rec->status, sizeof(rec->status), "Saved successfully");
    else if (!rec->failed)
        snprintf(rec->status, sizeof(rec->status),
                 "Export failed - check disk / ffmpeg (temp WAV kept)");
    return ok;
}
