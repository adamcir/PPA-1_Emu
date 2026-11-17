#ifndef PPA1_MIDI_CORE_H
#define PPA1_MIDI_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PPA_MIDI_MAX_TRACKS 1024

typedef struct {
    char id[32];
    char name[512];
    unsigned notes;
    unsigned source;
    unsigned channel;
    bool enabled;
    int voice;
    int wave;
    int poly;
    int volume;
    int transpose;
} PpaMidiTrack;

/* Load MIDI format 0/1 and enumerate every track/channel containing notes.
 * All selected tracks are initially enabled for interactive removal. */
bool ppa_midi_open(const char *path, PpaMidiTrack *tracks, int capacity,
                   int *count, char *error, size_t error_size);

/* Visible optimized arrangement, sampled from the SAME allocator used by ASM.
 * One row for each of the 4 physical oscillators and up to 64 time cells.
 * owner[cell] = 0 (silent) or input track index + 1.
 * pitch[cell] = sounding MIDI pitch, for showing split chord voices.
 * peak_polyphony = highest simultaneously held note count in each track.
 * dropped_cells indicates how many audible logical notes could not fit
 * in the four physical oscillators over the sampled event timeline. */
#define PPA_MIDI_PREVIEW_COLUMNS 64
typedef struct {
    int peak_polyphony[PPA_MIDI_MAX_TRACKS];
    unsigned selected_tracks;
    unsigned max_simultaneous_notes;
    unsigned dropped_note_events;
    unsigned cells;
    int owner[4][PPA_MIDI_PREVIEW_COLUMNS];
    int pitch[4][PPA_MIDI_PREVIEW_COLUMNS];
} PpaMidiPreview;

bool ppa_midi_preview(const char *path, const PpaMidiTrack *tracks, int count,
                      bool optimize, PpaMidiPreview *result,
                      char *error, size_t error_size);

/* Convert selected tracks directly to multi-bank ASM:
 * 0000: player, 0001..001F: frame data, FF/FF = next bank.
 * optimize=true means reuse idle oscillator slots and switch waveform.
 * No Python runtime, third-party MIDI libraries or extra data files.
 */
bool ppa_midi_export(const char *path, const PpaMidiTrack *tracks, int count,
                     bool optimize, const char *output_path,
                     int clock_hz, int tick_ms,
                     int *frames, int *banks, char *error, size_t error_size);

#endif
