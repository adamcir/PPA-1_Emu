/*
 * Standard MIDI File 0/1 parser and four-channel PPA-SOUND/1 ASM generator.
 * Pure C11: no Python, no MIDI library, no native executable module loading.
 * MIDI conversion runs on host; generated ASM runs on a physical PPA-1.
 */
#define _POSIX_C_SOURCE 200809L
#include "midi_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <limits.h>

#define ROM_BANK_COUNT 31u
#define ROM_BANK_BYTES 65536u
#define MIDI_EVENT_LIMIT 4000000u
enum {EV_NOTE_ON = 1, EV_NOTE_OFF = 2, EV_CC = 3};

typedef struct {
    uint64_t tick, order, unit;
    uint16_t source;
    uint8_t channel, kind, a, b;
    int track_index;
} MidiEvent;
typedef struct {
    uint64_t tick, order;
    uint32_t usec;
    double start_usec;
} Tempo;
typedef struct {
    unsigned char *data;
    size_t length, count, events_cap, tempos_count, tempos_cap;
    MidiEvent *events;
    Tempo *tempos;
    PpaMidiTrack tracks[PPA_MIDI_MAX_TRACKS];
    int track_count;
    uint16_t division;
    uint64_t end_tick;
} Midi;
typedef struct {
    uint16_t pressed, sustain_count;
    uint8_t velocity;
    uint64_t order;
} Note;
typedef struct {
    Note note[128];
    bool sustain;
    uint8_t cc7, cc11;
} Playing;
typedef struct {
    uint16_t frequency[4];
    uint8_t volume[4], waveform[4];
} SoundState;
typedef struct {
    uint16_t duration;
    SoundState state;
} SoundFrame;
typedef struct {
    SoundFrame *frames;
    size_t count, cap, max;
} FrameList;

static void err(char *dst, size_t n, const char *fmt, ...) {
    if (!dst || !n) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(dst, n, fmt, args);
    va_end(args);
}
static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static bool read_vlq(const uint8_t *bytes, size_t *at, size_t end,
                     uint32_t *value) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        if (*at >= end) return false;
        uint8_t b = bytes[(*at)++];
        result = (result << 7) | (b & 127u);
        if (!(b & 128u)) { *value = result; return true; }
    }
    return false;
}
static bool grow_array(void **ptr, size_t *cap, size_t wanted, size_t elem) {
    if (wanted <= *cap) return true;
    size_t next = *cap ? *cap * 2 : 256;
    if (next < wanted) next = wanted;
    if (next > SIZE_MAX / elem) return false;
    void *p = realloc(*ptr, next * elem);
    if (!p) return false;
    *ptr = p;
    *cap = next;
    return true;
}
static void free_midi(Midi *m) {
    free(m->data); free(m->events); free(m->tempos); free(m);
}
static int index_for(Midi *m, unsigned source, unsigned channel, bool create) {
    for (int i = 0; i < m->track_count; ++i)
        if (m->tracks[i].source == source && m->tracks[i].channel == channel)
            return i;
    if (!create || m->track_count >= PPA_MIDI_MAX_TRACKS) return -1;
    PpaMidiTrack *t = &m->tracks[m->track_count];
    memset(t, 0, sizeof(*t));
    t->source = source;
    t->channel = channel;
    snprintf(t->id, sizeof(t->id), "%u:%u", source, channel);
    t->volume = 200;
    t->wave = m->track_count % 4;
    t->poly = 1;
    t->voice = m->track_count < 4 ? m->track_count + 1 : 0;
    t->enabled = true;
    return m->track_count++;
}
static int cmp_tracks(const void *va, const void *vb) {
    const PpaMidiTrack *a = va, *b = vb;
    if (a->source != b->source) return a->source < b->source ? -1 : 1;
    return a->channel < b->channel ? -1 : a->channel > b->channel;
}
static int cmp_events(const void *va, const void *vb) {
    const MidiEvent *a = va, *b = vb;
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}
static int cmp_tempos(const void *va, const void *vb) {
    const Tempo *a = va, *b = vb;
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}
static bool add_event(Midi *m, MidiEvent e) {
    if (m->count >= MIDI_EVENT_LIMIT) return false;
    if (!grow_array((void **)&m->events, &m->events_cap,
                    m->count + 1, sizeof(*m->events))) return false;
    m->events[m->count++] = e;
    return true;
}
static bool add_tempo(Midi *m, Tempo tempo) {
    if (!grow_array((void **)&m->tempos, &m->tempos_cap,
                    m->tempos_count + 1, sizeof(*m->tempos))) return false;
    m->tempos[m->tempos_count++] = tempo;
    return true;
}
static Midi *parse(const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "rb");
    if (!file) { err(error, error_size, "Cannot open MIDI file: %s", path); return NULL; }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file); err(error, error_size, "MIDI seek failed"); return NULL;
    }
    long length = ftell(file);
    if (length < 14 || length > 256L * 1024L * 1024L) {
        fclose(file); err(error, error_size, "Invalid or excessively large MIDI"); return NULL;
    }
    rewind(file);
    Midi *m = calloc(1, sizeof(*m));
    if (!m) { fclose(file); err(error, error_size, "Out of memory"); return NULL; }
    m->data = malloc((size_t)length);
    if (!m->data || fread(m->data, 1, (size_t)length, file) != (size_t)length) {
        fclose(file); err(error, error_size, "Cannot read MIDI"); free_midi(m); return NULL;
    }
    fclose(file);
    m->length = (size_t)length;
    const uint8_t *d = m->data;
    if (memcmp(d, "MThd", 4)) {
        err(error, error_size, "Not a Standard MIDI File (missing MThd)");
        free_midi(m); return NULL;
    }
    uint32_t header_len = be32(d + 4);
    if (header_len < 6 || 8ull + header_len > m->length) {
        err(error, error_size, "Truncated MIDI header");
        free_midi(m); return NULL;
    }
    uint16_t format = be16(d + 8), chunks = be16(d + 10);
    m->division = be16(d + 12);
    if (format > 1 || chunks == 0 || chunks > 1024 ||
        !m->division || (m->division & 0x8000u)) {
        err(error, error_size, "Use MIDI format 0/1 with PPQN time division");
        free_midi(m); return NULL;
    }
    size_t pos = 8 + header_len;
    uint64_t order = 0;
    char (*names)[256] = calloc(chunks, 256);
    if (!names) { err(error, error_size, "Out of memory"); free_midi(m); return NULL; }
    bool ok = true;
    for (unsigned tr = 0; tr < chunks && ok; ++tr) {
        if (pos + 8 > m->length || memcmp(d + pos, "MTrk", 4)) {
            err(error, error_size, "Missing MTrk #%u", tr + 1); ok = false; break;
        }
        uint32_t size = be32(d + pos + 4);
        pos += 8;
        if (size > m->length - pos) {
            err(error, error_size, "Truncated MTrk #%u", tr + 1); ok = false; break;
        }
        size_t at = pos, end = pos + size;
        uint64_t tick = 0;
        uint8_t running = 0;
        while (at < end) {
            uint32_t delta;
            if (!read_vlq(d, &at, end, &delta) || UINT64_MAX - tick < delta) {
                err(error, error_size, "Malformed delta time, track %u", tr + 1);
                ok = false; break;
            }
            tick += delta;
            if (at >= end) { err(error, error_size, "Truncated MIDI event"); ok = false; break; }
            uint8_t status = d[at];
            if (status & 0x80u) {
                ++at;
                if (status == 0xFFu) {
                    running = 0;
                    if (at >= end) { ok = false; break; }
                    uint8_t meta = d[at++];
                    uint32_t n;
                    if (!read_vlq(d, &at, end, &n) || n > end - at) {
                        err(error, error_size, "Malformed MIDI meta event");
                        ok = false; break;
                    }
                    if (meta == 3u) {
                        size_t chars = n < 255 ? n : 255;
                        memcpy(names[tr], d + at, chars);
                        names[tr][chars] = 0;
                    } else if (meta == 0x51u && n == 3) {
                        uint32_t us = ((uint32_t)d[at] << 16) |
                                      ((uint32_t)d[at+1] << 8) | d[at+2];
                        if (us && !add_tempo(m, (Tempo){tick, order, us, 0})) {
                            err(error, error_size, "Out of MIDI tempo memory"); ok = false;
                        }
                    }
                    at += n;
                    ++order;
                    if (meta == 0x2Fu) break;
                    continue;
                }
                if (status == 0xF0u || status == 0xF7u) {
                    running = 0;
                    uint32_t n;
                    if (!read_vlq(d, &at, end, &n) || n > end - at) {
                        err(error, error_size, "Truncated MIDI SysEx");
                        ok = false; break;
                    }
                    at += n;
                    ++order;
                    continue;
                }
                if (status >= 0xF0u) {
                    err(error, error_size, "Unsupported MIDI system message");
                    ok = false; break;
                }
                running = status;
            } else if (!running) {
                err(error, error_size, "MIDI running status without channel status");
                ok = false; break;
            }
            uint8_t code = running & 0xF0u;
            unsigned channel = running & 0x0Fu;
            unsigned data_len = (code == 0xC0u || code == 0xD0u) ? 1u : 2u;
            if (at + data_len > end) {
                err(error, error_size, "Truncated channel event");
                ok = false; break;
            }
            uint8_t a = d[at], b = data_len == 2 ? d[at+1] : 0;
            if ((a & 128u) || (b & 128u)) {
                err(error, error_size, "Invalid MIDI data byte");
                ok = false; break;
            }
            at += data_len;
            int idx = -1;
            uint8_t kind = 0;
            if (code == 0x80u || code == 0x90u) {
                kind = code == 0x90u && b > 0 ? EV_NOTE_ON : EV_NOTE_OFF;
                idx = index_for(m, tr, channel, kind == EV_NOTE_ON);
                if (idx < 0 && kind == EV_NOTE_ON) {
                    err(error, error_size, "Too many active MIDI track/channel streams");
                    ok = false; break;
                }
                if (idx >= 0 && kind == EV_NOTE_ON) m->tracks[idx].notes++;
            } else if (code == 0xB0u &&
                       (a == 7 || a == 11 || a == 64 || a == 120 || a == 123)) {
                kind = EV_CC;
                idx = index_for(m, tr, channel, false);
            }
            if (kind && !add_event(m, (MidiEvent){
                    tick, order, 0, (uint16_t)tr, (uint8_t)channel,
                    kind, a, b, idx})) {
                err(error, error_size, "MIDI contains too many events");
                ok = false; break;
            }
            ++order;
        }
        if (tick > m->end_tick) m->end_tick = tick;
        pos = end;
    }
    if (!ok) { free(names); free_midi(m); return NULL; }
    qsort(m->tracks, (size_t)m->track_count, sizeof(m->tracks[0]), cmp_tracks);
    for (int i = 0; i < m->track_count; ++i) {
        PpaMidiTrack *t = &m->tracks[i];
        if (names[t->source][0])
            snprintf(t->name, sizeof(t->name), "%s / channel %u",
                     names[t->source], t->channel + 1);
        else
            snprintf(t->name, sizeof(t->name), "Track %u / channel %u",
                     t->source + 1, t->channel + 1);
    }
    free(names);
    if (!m->track_count) {
        err(error, error_size, "MIDI has no NOTE ON events");
        free_midi(m); return NULL;
    }
    for (size_t i = 0; i < m->count; ++i)
        m->events[i].track_index = index_for(m, m->events[i].source,
                                              m->events[i].channel, false);
    qsort(m->events, m->count, sizeof(m->events[0]), cmp_events);
    qsort(m->tempos, m->tempos_count, sizeof(m->tempos[0]), cmp_tempos);
    return m;
}
bool ppa_midi_open(const char *path, PpaMidiTrack *tracks, int capacity,
                   int *count, char *error, size_t error_size) {
    if (!path || !tracks || !count || capacity <= 0) return false;
    Midi *m = parse(path, error, error_size);
    if (!m) return false;
    bool ok = m->track_count <= capacity;
    if (ok) {
        memcpy(tracks, m->tracks, (size_t)m->track_count * sizeof(*tracks));
        *count = m->track_count;
    } else {
        err(error, error_size, "Too many track/channel streams (%d)",
            m->track_count);
    }
    free_midi(m);
    return ok;
}
/* Convert an absolute MIDI tick position to quantized wall-clock units
 * using a sorted tempo map, with tempo 500000 us/quarter at tick zero. */
static uint64_t convert_tick(const Midi *m, uint64_t tick, int step_ms) {
    double elapsed = 0.0;
    uint64_t cursor = 0;
    uint32_t tempo = 500000;
    for (size_t i = 0; i < m->tempos_count && m->tempos[i].tick <= tick; ++i) {
        Tempo next = m->tempos[i];
        if (next.tick > cursor)
            elapsed += ((double)(next.tick - cursor) * tempo) / m->division;
        cursor = next.tick;
        tempo = next.usec;
    }
    elapsed += ((double)(tick - cursor) * tempo) / m->division;
    double units = elapsed / (step_ms * 1000.0);
    if (units > (double)UINT64_MAX - 1.0) return UINT64_MAX;
    return (uint64_t)floor(units + 0.5);
}
static uint16_t midi_frequency(int note) {
    if (note < 0 || note > 127) return 0;
    double hz = 440.0 * pow(2.0, (note - 69.0) / 12.0);
    if (hz > 65535) return 65535;
    if (hz < 1) return 1;
    return (uint16_t)lround(hz);
}
static int selected_note(const Playing *state, const PpaMidiTrack *config,
                         uint8_t *velocity) {
    int selected = -1;
    uint64_t selected_order = 0;
    for (int note = 0; note < 128; ++note) {
        const Note *n = &state->note[note];
        if (!n->pressed && !n->sustain_count) continue;
        bool pick = selected < 0;
        if (config->poly == 0 && note < selected) pick = true;
        if (config->poly == 1 && note > selected) pick = true;
        if (config->poly == 2 && n->order > selected_order) pick = true;
        if (pick) {
            selected = note;
            selected_order = n->order;
            *velocity = n->velocity;
        }
    }
    return selected;
}
static void update_state(Playing *st, MidiEvent *event) {
    uint8_t key = event->a;
    if (event->kind == EV_NOTE_ON) {
        Note *note = &st->note[key];
        if (note->pressed < UINT16_MAX) ++note->pressed;
        note->velocity = event->b;
        note->order = event->order;
    } else if (event->kind == EV_NOTE_OFF) {
        Note *note = &st->note[key];
        if (note->pressed) {
            --note->pressed;
            if (st->sustain && note->sustain_count < UINT16_MAX)
                ++note->sustain_count;
        }
    } else if (event->kind == EV_CC) {
        if (key == 7) st->cc7 = event->b;
        if (key == 11) st->cc11 = event->b;
        if (key == 64) {
            st->sustain = event->b >= 64;
            if (!st->sustain)
                for (int k = 0; k < 128; ++k) st->note[k].sustain_count = 0;
        }
        if (key == 120 || key == 123)
            for (int k = 0; k < 128; ++k) {
                st->note[k].pressed = 0;
                st->note[k].sustain_count = 0;
            }
    }
}
static bool same_state(const SoundState *a, const SoundState *b, bool dynamic) {
    for (int i = 0; i < 4; ++i)
        if (a->frequency[i] != b->frequency[i] ||
            a->volume[i] != b->volume[i] ||
            (dynamic && a->waveform[i] != b->waveform[i]))
            return false;
    return true;
}
static bool append_frames(FrameList *out, uint64_t duration, SoundState state,
                          bool dynamic) {
    while (duration) {
        if (out->count && same_state(&out->frames[out->count - 1].state,
                                     &state, dynamic) &&
            out->frames[out->count - 1].duration < 65534) {
            uint32_t capacity = 65534u - out->frames[out->count - 1].duration;
            uint32_t amount = duration < capacity ? (uint32_t)duration : capacity;
            out->frames[out->count - 1].duration += (uint16_t)amount;
            duration -= amount;
            continue;
        }
        if (out->count >= out->max ||
            !grow_array((void **)&out->frames, &out->cap,
                        out->count + 1, sizeof(*out->frames)))
            return false;
        uint16_t amount = duration > 65534 ? 65534 : (uint16_t)duration;
        out->frames[out->count++] = (SoundFrame){amount, state};
        duration -= amount;
    }
    return true;
}
/* A voice is assigned to a particular (track, MIDI pitch), not just a
 * track. This permits the same MIDI chord to occupy all four oscillators.
 * An assignment survives note/CC updates until its note becomes inaudible. */
static uint8_t note_amplitude(const PpaMidiTrack *track,
                              const Playing *state, int pitch) {
    const Note *note = &state->note[pitch];
    if (!track->enabled || (!note->pressed && !note->sustain_count))
        return 0;
    uint32_t value = (uint32_t)track->volume * note->velocity *
                     state->cc7 * state->cc11 / (127u * 127u * 127u);
    return (uint8_t)(value > 255 ? 255 : value);
}
static bool key_is_assigned(const int assigned[4], int key) {
    for (int voice = 0; voice < 4; ++voice)
        if (assigned[voice] == key) return true;
    return false;
}
/* Compare notes within their original MIDI track using the user's
 * requested polyphony policy (low, high or latest). */
static bool preferred_note(const PpaMidiTrack *track, const Playing *state,
                           int candidate, int current) {
    if (current < 0) return true;
    if (track->poly == 0) return candidate < current;
    if (track->poly == 1) return candidate > current;
    if (state->note[candidate].order != state->note[current].order)
        return state->note[candidate].order > state->note[current].order;
    return candidate > current;
}
static SoundState compose(const PpaMidiTrack *tracks, const Playing *state,
                          int count, bool optimized, int assigned[4]) {
    SoundState result = {0};
    if (!optimized) {
        /* Legacy behavior: one monophonic priority-selected note per track. */
        for (int t = 0; t < count; ++t) {
            if (!tracks[t].enabled || tracks[t].voice < 1 ||
                tracks[t].voice > 4) continue;
            uint8_t velocity = 0;
            int note = selected_note(&state[t], &tracks[t], &velocity);
            if (note < 0) continue;
            uint8_t amplitude = note_amplitude(&tracks[t], &state[t], note);
            unsigned voice = (unsigned)(tracks[t].voice - 1);
            result.frequency[voice] = midi_frequency(note + tracks[t].transpose);
            result.volume[voice] = amplitude;
            result.waveform[voice] = (uint8_t)tracks[t].wave;
        }
        return result;
    }

    /* Keep continuing notes on the same physical voice, even when the
     * chord changes; note-off and sustain are processed by update_state. */
    for (int voice = 0; voice < 4; ++voice) {
        int key = assigned[voice];
        if (key < 0) continue;
        int track = key / 128, note = key % 128;
        if (track >= count ||
            !note_amplitude(&tracks[track], &state[track], note))
            assigned[voice] = -1;
    }

    /* First offer one priority-selected pitch to each track. This prevents
     * a dense chord from one track from consuming every newly free voice
     * before another selected track has a chance to play. */
    for (int voice = 0; voice < 4; ++voice) {
        if (assigned[voice] >= 0) continue;
        int best_key = -1;
        uint8_t best_volume = 0;
        for (int t = 0; t < count; ++t) {
            if (!tracks[t].enabled) continue;
            bool represented = false;
            for (int v = 0; v < 4; ++v)
                if (assigned[v] >= 0 && assigned[v] / 128 == t)
                    represented = true;
            if (represented) continue;
            uint8_t velocity = 0;
            int note = selected_note(&state[t], &tracks[t], &velocity);
            if (note < 0 || key_is_assigned(assigned, t * 128 + note))
                continue;
            uint8_t amp = note_amplitude(&tracks[t], &state[t], note);
            if (amp > best_volume) {
                best_volume = amp;
                best_key = t * 128 + note;
            }
        }
        if (best_key >= 0) assigned[voice] = best_key;
        else break;
    }

    /* Spend remaining idle voices on *additional pitches of the same
     * tracks*. Within one track obey low/high/latest selection for the
     * next pitch; across tracks choose the loudest available candidate. */
    for (int voice = 0; voice < 4; ++voice) {
        if (assigned[voice] >= 0) continue;
        int best_key = -1;
        uint8_t best_volume = 0;
        for (int t = 0; t < count; ++t) {
            if (!tracks[t].enabled) continue;
            int chosen = -1;
            for (int note = 0; note < 128; ++note) {
                if (key_is_assigned(assigned, t * 128 + note) ||
                    !note_amplitude(&tracks[t], &state[t], note))
                    continue;
                if (preferred_note(&tracks[t], &state[t], note, chosen))
                    chosen = note;
            }
            if (chosen < 0) continue;
            uint8_t amp = note_amplitude(&tracks[t], &state[t], chosen);
            if (amp > best_volume) {
                best_volume = amp;
                best_key = t * 128 + chosen;
            }
        }
        if (best_key < 0) break;
        assigned[voice] = best_key;
    }
    for (int voice = 0; voice < 4; ++voice) {
        int key = assigned[voice];
        if (key < 0) continue;
        int t = key / 128, note = key % 128;
        result.frequency[voice] = midi_frequency(note + tracks[t].transpose);
        result.volume[voice] = note_amplitude(&tracks[t], &state[t], note);
        result.waveform[voice] = (uint8_t)tracks[t].wave;
    }
    return result;
}
/* Preview is produced by executing the exact same note->voice allocator as
 * the exported ASM. Track rows are expanded by their observed peak
 * polyphony, while physical voice bars show actual allocated notes.
 * The 64-cell sampling is a display approximation, never output data. */
bool ppa_midi_preview(const char *path, const PpaMidiTrack *tracks, int count,
                      bool optimize, PpaMidiPreview *result,
                      char *error, size_t error_size) {
    if (!path || !tracks || count < 1 || count > PPA_MIDI_MAX_TRACKS || !result) {
        err(error, error_size, "Invalid MIDI preview arguments");
        return false;
    }
    memset(result, 0, sizeof(*result));
    result->cells = PPA_MIDI_PREVIEW_COLUMNS;
    Midi *m = parse(path, error, error_size);
    if (!m) return false;
    if (m->track_count != count) {
        err(error, error_size, "MIDI tracks changed; reopen MIDI");
        free_midi(m); return false;
    }
    for (int i = 0; i < count; ++i) {
        if (strcmp(tracks[i].id, m->tracks[i].id)) {
            err(error, error_size, "Track order changed; reopen MIDI");
            free_midi(m); return false;
        }
        if (tracks[i].enabled) result->selected_tracks++;
    }
    Playing *state = calloc((size_t)count, sizeof(*state));
    if (!state) {
        err(error, error_size, "Out of memory");
        free_midi(m);
        return false;
    }
    for (int i = 0; i < count; ++i) {
        state[i].cc7 = 127;
        state[i].cc11 = 127;
    }
    int assigned[4] = {-1, -1, -1, -1};
    uint64_t total_ticks = m->end_tick ? m->end_tick : 1;
    uint64_t previous_tick = 0;
    int owner[4] = {0}, pitch[4] = {0};
    size_t event = 0;
    /* Fill time slices with assignments made after the MIDI event group
     * at the interval's starting point, including intervals of silence. */
    for (unsigned cell = 0; cell < PPA_MIDI_PREVIEW_COLUMNS; ++cell) {
        uint64_t instant = (uint64_t)((long double)total_ticks * cell /
                                    PPA_MIDI_PREVIEW_COLUMNS);
        while (event < m->count && m->events[event].tick <= instant) {
            uint64_t tick = m->events[event].tick;
            while (event < m->count && m->events[event].tick == tick) {
                MidiEvent *e = &m->events[event++];
                if (e->track_index >= 0 && e->track_index < count)
                    update_state(&state[e->track_index], e);
            }
            /* Peak chord size is measured at every exact event boundary,
             * not just the 64 display samples: short staccato chords
             * must still expand into the correct number of logical lanes. */
            unsigned sounding = 0;
            for (int t = 0; t < count; ++t) {
                if (!tracks[t].enabled) continue;
                unsigned notes = 0;
                for (int note = 0; note < 128; ++note)
                    if (note_amplitude(&tracks[t], &state[t], note)) ++notes;
                if (notes > (unsigned)result->peak_polyphony[t])
                    result->peak_polyphony[t] = (int)notes;
                sounding += notes;
            }
            if (sounding > result->max_simultaneous_notes)
                result->max_simultaneous_notes = sounding;
            /* Composition at every MIDI event boundary preserves the voice
             * retention decisions used in generate_frames(). */
            if (optimize)
                (void)compose(tracks, state, count, true, assigned);
            previous_tick = tick;
        }
        (void)previous_tick;
        unsigned audible = 0;
        for (int t = 0; t < count; ++t) {
            if (!tracks[t].enabled) continue;
            unsigned notes = 0;
            for (int n = 0; n < 128; ++n)
                if (note_amplitude(&tracks[t], &state[t], n)) notes++;
            if (notes > (unsigned)result->peak_polyphony[t])
                result->peak_polyphony[t] = (int)notes;
            audible += notes;
        }
        if (audible > result->max_simultaneous_notes)
            result->max_simultaneous_notes = audible;
        if (optimize) {
            (void)compose(tracks, state, count, true, assigned);
            unsigned allocated = 0;
            for (int voice = 0; voice < 4; ++voice) {
                if (assigned[voice] < 0) continue;
                int index = assigned[voice] / 128, note = assigned[voice] % 128;
                if (index >= count || !note_amplitude(&tracks[index], &state[index], note))
                    continue;
                result->owner[voice][cell] = index + 1;
                result->pitch[voice][cell] = note;
                allocated++;
            }
            if (audible > allocated) result->dropped_note_events += audible - allocated;
        } else {
            for (int t = 0; t < count; ++t) {
                if (!tracks[t].enabled || tracks[t].voice < 1 ||
                    tracks[t].voice > 4) continue;
                uint8_t velocity = 0;
                int note = selected_note(&state[t], &tracks[t], &velocity);
                if (note < 0 || !note_amplitude(&tracks[t], &state[t], note))
                    continue;
                int voice = tracks[t].voice - 1;
                result->owner[voice][cell] = t + 1;
                result->pitch[voice][cell] = note;
            }
        }
    }
    free(state);
    free_midi(m);
    return true;
}

static bool generate_frames(Midi *m, const PpaMidiTrack *config,
                            int count, bool optimized, int step_ms,
                            FrameList *out, char *error, size_t error_size) {
    if (step_ms < 1 || step_ms > 250) {
        err(error, error_size, "Time unit must be 1..250 milliseconds");
        return false;
    }
    const int bytes = optimized ? 18 : 14;
    out->max = ROM_BANK_COUNT * ((ROM_BANK_BYTES - 2u) / (unsigned)bytes);
    Playing *state = calloc((size_t)count, sizeof(*state));
    if (!state) { err(error, error_size, "Out of memory"); return false; }
    for (int i = 0; i < count; ++i) { state[i].cc7 = 127; state[i].cc11 = 127; }
    for (size_t i = 0; i < m->count; ++i)
        m->events[i].unit = convert_tick(m, m->events[i].tick, step_ms);
    uint64_t end_time = convert_tick(m, m->end_tick, step_ms) +
                        (200u + (unsigned)step_ms - 1u) / (unsigned)step_ms;
    int assigned[4] = {-1, -1, -1, -1};
    SoundState prior = {0};
    uint64_t previous = 0;
    bool ok = true;
    for (size_t i = 0; i < m->count; ) {
        uint64_t instant = m->events[i].unit;
        if (instant > previous && !append_frames(
                out, instant - previous, prior, optimized)) {
            ok = false; break;
        }
        do {
            MidiEvent *e = &m->events[i++];
            int index = e->track_index;
            if (index >= 0 && index < count)
                update_state(&state[index], e);
        } while (i < m->count && m->events[i].unit == instant);
        prior = compose(config, state, count, optimized, assigned);
        previous = instant;
    }
    if (ok && end_time > previous)
        ok = append_frames(out, end_time - previous, prior, optimized);
    free(state);
    if (!ok)
        err(error, error_size, "Song exceeds available 2 MiB ROM: max %zu frames",
            out->max);
    return ok;
}
static void asm_line(FILE *f, const char *line) {
    fputs(line, f);
    fputc('\n', f);
}
static void write_player(FILE *f, const PpaMidiTrack *tracks,
                         int count, bool optimized, int clock_hz, int tick_ms) {
    int iterations = (int)lround((double)clock_hz * tick_ms / 8000.0);
    int waves[4] = {0, 0, 0, 0};
    if (!optimized)
        for (int i = 0; i < count; ++i)
            if (tracks[i].enabled && tracks[i].voice >= 1 &&
                tracks[i].voice <= 4)
                waves[tracks[i].voice - 1] = tracks[i].wave;
    asm_line(f, "; Generated by PPA-1_MIDI-to-ASM C11 / ncurses");
    asm_line(f, "; Executable player: ROM 0000. Music data: ROM 0001..001F.");
    asm_line(f, "; Data bank switch: FF FF; song finished: 00 00.");
    asm_line(f, ".bank 0000");
    asm_line(f, ".org $0000");
    asm_line(f, "    JMP midi_start");
    asm_line(f, ".include \"includes/ppa-slots.inc\"");
    asm_line(f, ".include \"includes/ppa-sound.inc\"");
    asm_line(f, "");
    asm_line(f, "midi_start:");
    asm_line(f, "    CALL sound_init");
    asm_line(f, "    MOV BR-HI,#10");
    asm_line(f, "    MOV BR-LO,#00");
    asm_line(f, "    LD A,$0061");
    asm_line(f, "    CMP A,#A0");
    asm_line(f, "    JZ midi_no_sound");
    asm_line(f, "    CMP A,#00");
    asm_line(f, "    JZ midi_no_sound");
    asm_line(f, "    MOV A,#01");
    asm_line(f, "    ST $0064,A");
    asm_line(f, "    CALL slots_use_sound");
    for (int v = 0; v < 4; ++v) {
        fprintf(f, "    MOV A,#%02X\n    ST $%04X,A\n",
                waves[v], 0x22 + v * 16);
    }
    asm_line(f, "    MOV Y-HI,#00");
    asm_line(f, "    MOV Y-LO,#00");
    asm_line(f, "midi_frame:");
    asm_line(f, "    CALL midi_next");
    asm_line(f, "    MOV X-HI,A");
    asm_line(f, "    CALL midi_next");
    asm_line(f, "    MOV X-LO,A");
    asm_line(f, "    MOV A,X-HI");
    asm_line(f, "    CMP A,#FF");
    asm_line(f, "    JNZ midi_duration");
    asm_line(f, "    MOV A,X-LO");
    asm_line(f, "    CMP A,#FF");
    asm_line(f, "    JZ midi_bank_next");
    asm_line(f, "midi_duration:");
    asm_line(f, "    MOV A,X-HI");
    asm_line(f, "    OR A,X-LO");
    asm_line(f, "    JZ midi_done");
    for (int v = 0; v < 4; ++v) {
        asm_line(f, "    CALL midi_next");
        asm_line(f, "    MOV B,A");
        asm_line(f, "    CALL midi_next");
        asm_line(f, "    MOV C,A");
        fprintf(f, "    CALL sound_voice%d\n", v + 1);
        asm_line(f, "    CALL midi_next");
        asm_line(f, "    CALL slots_use_sound");
        fprintf(f, "    ST $%04X,A\n", 0x23 + v * 16);
        if (optimized) {
            asm_line(f, "    CALL midi_next");
            asm_line(f, "    CALL slots_use_sound");
            fprintf(f, "    ST $%04X,A\n", 0x22 + v * 16);
        }
    }
    asm_line(f, "midi_wait:");
    fprintf(f, "    MOV D,#%02X\n", iterations);
    asm_line(f, "midi_inner:");
    asm_line(f, "    DEC D");
    asm_line(f, "    JNZ midi_inner");
    asm_line(f, "    CMP X-LO,#00");
    asm_line(f, "    JNZ midi_low");
    asm_line(f, "    DEC X-HI");
    asm_line(f, "midi_low:");
    asm_line(f, "    DEC X-LO");
    asm_line(f, "    MOV A,X-HI");
    asm_line(f, "    OR A,X-LO");
    asm_line(f, "    JNZ midi_wait");
    asm_line(f, "    JMP midi_frame");
    asm_line(f, "midi_bank_next:");
    asm_line(f, "    MOV Y-HI,#00");
    asm_line(f, "    MOV Y-LO,#00");
    asm_line(f, "    MOV BR-HI,#10");
    asm_line(f, "    MOV BR-LO,#00");
    asm_line(f, "    LD A,$0064");
    asm_line(f, "    INC A");
    asm_line(f, "    ST $0064,A");
    asm_line(f, "    JMP midi_frame");
    asm_line(f, "midi_next:");
    asm_line(f, "    PUSH B");
    asm_line(f, "    MOV BR-HI,#10");
    asm_line(f, "    MOV BR-LO,#00");
    asm_line(f, "    LD B,$0064");
    asm_line(f, "    MOV BR-HI,#00");
    asm_line(f, "    MOV BR-LO,B");
    asm_line(f, "    LD A,Y");
    asm_line(f, "    INC Y-LO");
    asm_line(f, "    JNZ midi_no_carry");
    asm_line(f, "    INC Y-HI");
    asm_line(f, "midi_no_carry:");
    asm_line(f, "    POP B");
    asm_line(f, "    RET");
    asm_line(f, "midi_done:");
    asm_line(f, "    CALL sound_stop");
    asm_line(f, "midi_no_sound:");
    asm_line(f, "    HLT");
}
static bool write_data(FILE *f, const FrameList *v, bool optimized,
                       int *bank_total) {
    unsigned width = optimized ? 18u : 14u;
    unsigned per_bank = (ROM_BANK_BYTES - 2u) / width;
    size_t banks = v->count ? (v->count + per_bank - 1u) / per_bank : 1u;
    if (banks > ROM_BANK_COUNT) return false;
    for (size_t bank = 0; bank < banks; ++bank) {
        fprintf(f, "\n.bank %04zX\n.org $0000\n", bank + 1u);
        size_t from = bank * per_bank, end = from + per_bank;
        if (end > v->count) end = v->count;
        for (size_t i = from; i < end; ++i) {
            const SoundFrame *frame = &v->frames[i];
            unsigned char bytes[18];
            bytes[0] = (uint8_t)(frame->duration >> 8);
            bytes[1] = (uint8_t)frame->duration;
            for (int a = 0; a < 4; ++a) {
                size_t offset = 2u + (unsigned)a * (optimized ? 4u : 3u);
                bytes[offset] = (uint8_t)frame->state.frequency[a];
                bytes[offset + 1] = (uint8_t)(frame->state.frequency[a] >> 8);
                bytes[offset + 2] = frame->state.volume[a];
                if (optimized) bytes[offset + 3] = frame->state.waveform[a];
            }
            for (unsigned j = 0; j < width; j += 16) {
                fprintf(f, "    .byte ");
                unsigned n = width - j < 16 ? width - j : 16;
                for (unsigned k = 0; k < n; ++k)
                    fprintf(f, "%s$%02X", k ? "," : "", bytes[j + k]);
                fputc('\n', f);
            }
        }
        asm_line(f, bank + 1 < banks
                    ? "    .byte $FF,$FF ; next ROM bank"
                    : "    .byte $00,$00 ; song finished");
    }
    *bank_total = (int)banks;
    return true;
}
bool ppa_midi_export(const char *path, const PpaMidiTrack *tracks, int count,
                     bool optimize, const char *output_path,
                     int clock_hz, int tick_ms,
                     int *frames, int *banks, char *error, size_t error_size) {
    if (!path || !tracks || count <= 0 || !output_path) {
        err(error, error_size, "Invalid arguments");
        return false;
    }
    int iterations = (int)lround((double)clock_hz * tick_ms / 8000.0);
    if (clock_hz < 100 || clock_hz > 10000000 || tick_ms < 1 || tick_ms > 250 ||
        iterations < 1 || iterations > 255) {
        err(error, error_size, "Clock/tick invalid; delay loop must fit 1..255");
        return false;
    }
    Midi *m = parse(path, error, error_size);
    if (!m) return false;
    if (m->track_count != count) {
        err(error, error_size, "MIDI tracks changed since file was opened");
        free_midi(m); return false;
    }
    PpaMidiTrack *mapping = calloc((size_t)count, sizeof(*mapping));
    if (!mapping) { free_midi(m); err(error, error_size, "Out of memory"); return false; }
    int selected = 0;
    for (int i = 0; i < count; ++i) {
        if (strcmp(m->tracks[i].id, tracks[i].id)) {
            err(error, error_size, "Track identifiers changed");
            free(mapping); free_midi(m); return false;
        }
        mapping[i] = tracks[i];
        if (mapping[i].enabled) {
            ++selected;
            if (mapping[i].wave < 0 || mapping[i].wave > 4 ||
                mapping[i].volume < 0 || mapping[i].volume > 255 ||
                mapping[i].poly < 0 || mapping[i].poly > 2) {
                err(error, error_size, "Invalid track settings");
                free(mapping); free_midi(m); return false;
            }
        }
    }
    if (!selected || (!optimize && selected > 4)) {
        err(error, error_size, selected ? "More than 4 tracks: enable optimization"
                                      : "Select at least one track");
        free(mapping); free_midi(m); return false;
    }
    if (!optimize) {
        int voice = 0;
        for (int i = 0; i < count; ++i)
            mapping[i].voice = mapping[i].enabled ? ++voice : 0;
    }
    FrameList list = {0};
    bool ok = generate_frames(m, mapping, count, optimize, tick_ms,
                              &list, error, error_size);
    if (ok) {
        FILE *f = fopen(output_path, "wb");
        if (!f) {
            err(error, error_size, "Cannot write ASM: %s", output_path);
            ok = false;
        } else {
            write_player(f, mapping, count, optimize, clock_hz, tick_ms);
            int total_banks = 0;
            ok = write_data(f, &list, optimize, &total_banks);
            int write_failed = ferror(f);
            int close_failed = fclose(f);
            if (write_failed || close_failed != 0) {
                err(error, error_size, "Cannot finish ASM output");
                ok = false;
            }
            if (ok) {
                if (frames) *frames = (int)list.count;
                if (banks) *banks = total_banks;
            }
            if (!ok) remove(output_path);
        }
    }
    free(list.frames);
    free(mapping);
    free_midi(m);
    return ok;
}
