#include "ppa_sound.h"
#include <math.h>
#include <string.h>

/* Four independent hardware envelopes. ADSR 0 is instantaneous;
 * 255 corresponds to 2 s A/D or 4 s release. Curved time mapping gives
 * useful control over short percussive notes. Envelope runs per PCM sample,
 * regardless of the visibility of the CPU panel or module UI. */
static float envelope_seconds(uint8_t value, float maximum) {
    float fraction = (float)value / 255.0f;
    return fraction * fraction * maximum;
}
static uint32_t envelope_length(uint8_t value, uint32_t sample_rate, float maximum) {
    return (uint32_t)(envelope_seconds(value, maximum) * sample_rate);
}
static void envelope_gate(PpaSoundChannel *v, uint8_t value) {
    bool next = (value & 1u) != 0 || (value & 2u) != 0;
    bool retrigger = (value & 2u) != 0;
    v->adsr_active = true;
    if (next && (!v->gate || retrigger)) {
        v->envelope_phase = 1;
        v->envelope_samples = 0;
        v->envelope_level = 0.0f;
        v->release_start_level = 0.0f;
    } else if (!next && v->gate) {
        v->envelope_phase = 4;
        v->release_start_level = v->envelope_level;
        v->envelope_samples = 0;
    }
    if (next) {
        v->release_frequency = v->frequency;
        v->release_waveform = v->waveform;
        if (v->volume) v->release_volume = v->volume;
    }
    v->gate = next;
}
static float envelope_sample(PpaSoundChannel *v, uint32_t rate) {
    if (!v->adsr_active) return 1.0f;
    const float sustain = (float)v->sustain / 255.0f;
    uint32_t length;
    switch (v->envelope_phase) {
        case 1: /* Attack. */
            length = envelope_length(v->attack, rate, 2.0f);
            if (!length) {
                v->envelope_level = 1.0f;
                v->envelope_phase = 2;
                v->envelope_samples = 0;
            } else {
                v->envelope_samples++;
                v->envelope_level = fminf(1.0f, (float)v->envelope_samples / length);
                if (v->envelope_samples >= length) {
                    v->envelope_phase = 2;
                    v->envelope_samples = 0;
                }
            }
            break;
        case 2: /* Decay towards sustain. */
            length = envelope_length(v->decay, rate, 2.0f);
            if (!length) {
                v->envelope_level = sustain;
                v->envelope_phase = 3;
            } else {
                v->envelope_samples++;
                v->envelope_level = 1.0f - (1.0f - sustain) *
                    fminf(1.0f, (float)v->envelope_samples / length);
                if (v->envelope_samples >= length) v->envelope_phase = 3;
            }
            break;
        case 3:
            v->envelope_level = sustain;
            break;
        case 4: /* Release from the level at the falling GATE edge. */
            length = envelope_length(v->release, rate, 4.0f);
            if (!length) {
                v->envelope_level = 0.0f;
                v->envelope_phase = 0;
            } else {
                v->envelope_samples++;
                v->envelope_level = v->release_start_level *
                    (1.0f - fminf(1.0f, (float)v->envelope_samples / length));
                if (v->envelope_samples >= length) v->envelope_phase = 0;
            }
            break;
        default:
            v->envelope_level = 0.0f;
            break;
    }
    return v->envelope_level;
}

void ppa_sound_init(PpaModule *m) {
    m->sound_volume = 255;
    m->noise_state = 0x1A2B3C4Du;
    for (int ch = 0; ch < PPAM_AUDIO_CHANNELS; ch++) {
        PpaSoundChannel *v = &m->sound[ch];
        v->frequency = 440;
        v->sustain = 255;
    }
}
void ppa_sound_reset(PpaModule *m) {
    m->sound_master = false;
    for (int ch = 0; ch < PPAM_AUDIO_CHANNELS; ch++) {
        PpaSoundChannel *v = &m->sound[ch];
        v->enabled = false;
        v->phase = 0.0f;
        v->adsr_active = false;
        v->gate = false;
        v->attack = v->decay = v->release = 0;
        v->sustain = 255;
        v->envelope_phase = 0;
        v->envelope_samples = 0;
        v->envelope_level = 0.0f;
        v->release_start_level = 0.0f;
        v->release_volume = 0;
        v->release_frequency = 0;
        v->release_waveform = 0;
    }
    memset(m->scope, 0, sizeof(m->scope));
}
uint8_t ppa_sound_read(const PpaModule *m, uint32_t offset) {
    if (offset == 0x10) return m->sound_master ? 1 : 0;
    if (offset == 0x11) return m->sound_volume;
    if (offset >= 0x20 && offset < 0x60) {
        unsigned int ch = (unsigned int)((offset - 0x20) / 0x10);
        unsigned int reg = (unsigned int)((offset - 0x20) % 0x10);
        const PpaSoundChannel *v = &m->sound[ch];
        switch (reg) {
            case 0: return (uint8_t)v->frequency;
            case 1: return (uint8_t)(v->frequency >> 8);
            case 2: return v->waveform;
            case 3: return v->volume;
            case 4: return v->enabled ? 1 : 0;
            case 5: return v->attack;
            case 6: return v->decay;
            case 7: return v->sustain;
            case 8: return v->release;
            case 9: return v->gate ? 1 : 0;
            default: break;
        }
    }
    return 0xFF;
}
void ppa_sound_write(PpaModule *m, uint32_t offset, uint8_t value) {
    if (offset == 0x10) { m->sound_master = (value & 1) != 0; return; }
    if (offset == 0x11) { m->sound_volume = value; return; }
    if (offset >= 0x20 && offset < 0x60) {
        unsigned int ch = (unsigned int)((offset - 0x20) / 0x10);
        unsigned int reg = (unsigned int)((offset - 0x20) % 0x10);
        PpaSoundChannel *v = &m->sound[ch];
        switch (reg) {
            case 0: v->frequency = (v->frequency & 0xFF00u) | value; break;
            case 1: v->frequency = (v->frequency & 0x00FFu) | ((uint16_t)value << 8); break;
            case 2: v->waveform = value % 5; break;
            case 3:
                v->volume = value;
                if (!v->adsr_active || v->gate) {
                    if (value) v->release_volume = value;
                }
                break;
            case 4: v->enabled = (value & 1) != 0; break;
            case 5: v->attack = value; break;
            case 6: v->decay = value; break;
            case 7: v->sustain = value; break;
            case 8: v->release = value; break;
            case 9: envelope_gate(v, value); break;
            default: break;
        }
    }
}
/* 16-bit mono PCM synthesizer; GATE/ADSR and voice phase run in the audio
 * mixer independently of the visibility of any SDL window. */
void ppam_mix_audio(PpaModuleBus *bus, int16_t *output, uint32_t frames,
                    uint32_t sample_rate) {
    if (!bus || !output || sample_rate == 0) return;
    for (uint32_t i = 0; i < frames; i++) {
        float mixed = 0.0f;
        for (int s = 0; s < PPAM_SLOTS; s++) {
            PpaModule *m = &bus->slots[s];
            if (!m->installed || !m->active ||
                m->type != PPAM_TYPE_SOUND || !m->sound_master) {
                if (m->type == PPAM_TYPE_SOUND)
                    m->scope[i % PPAM_SCOPE_SAMPLES] = 0;
                continue;
            }
            float voice = 0.0f;
            for (int ch = 0; ch < PPAM_AUDIO_CHANNELS; ch++) {
                PpaSoundChannel *v = &m->sound[ch];
                float env = envelope_sample(v, sample_rate);
                if (!v->enabled || env <= 0.0f) continue;
                uint8_t volume = v->adsr_active && !v->gate
                               ? v->release_volume : v->volume;
                uint16_t frequency = v->adsr_active && !v->gate
                                   ? v->release_frequency : v->frequency;
                uint8_t waveform = v->adsr_active && !v->gate
                                  ? v->release_waveform : v->waveform;
                if (!volume || !frequency) continue;
                float t = v->phase;
                float val;
                switch (waveform) {
                    case 1: val = 2.0f * t - 1.0f; break;
                    case 2: val = 1.0f - 4.0f * fabsf(t - 0.5f); break;
                    case 3: val = sinf(t * 6.28318530718f); break;
                    case 4:
                        m->noise_state ^= m->noise_state << 13;
                        m->noise_state ^= m->noise_state >> 17;
                        m->noise_state ^= m->noise_state << 5;
                        val = (m->noise_state & 1u) ? 1.0f : -1.0f;
                        break;
                    default: val = t < 0.5f ? 1.0f : -1.0f; break;
                }
                voice += val * ((float)volume / 255.0f) * env * 0.20f;
                t += (float)frequency / (float)sample_rate;
                t -= floorf(t);
                v->phase = t;
            }
            voice *= (float)m->sound_volume / 255.0f;
            m->scope[i % PPAM_SCOPE_SAMPLES] =
                (int8_t)fmaxf(-127.0f, fminf(127.0f, voice * 127.0f));
            mixed += voice;
        }
        mixed = fmaxf(-1.0f, fminf(1.0f, mixed));
        output[i] = (int16_t)(mixed * 24575.0f);
    }
}
