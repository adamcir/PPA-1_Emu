#include "ppa_sound.h"
#include <math.h>
#include <string.h>

void ppa_sound_init(PpaModule *m) {
    m->sound_volume = 255;
    m->noise_state = 0x1A2B3C4Du;
    for (int ch = 0; ch < PPAM_AUDIO_CHANNELS; ch++)
        m->sound[ch].frequency = 440;
}
void ppa_sound_reset(PpaModule *m) {
    m->sound_master = false;
    for (int ch = 0; ch < PPAM_AUDIO_CHANNELS; ch++) {
        m->sound[ch].enabled = false;
        m->sound[ch].phase = 0.0f;
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
            case 3: v->volume = value; break;
            case 4: v->enabled = (value & 1) != 0; break;
            default: break;
        }
    }
}

/* 16-bit mono PCM synthesizer. Host audio sampling runs independently of
 * emulated CPU instruction rate; control register changes are CPU-driven. */
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
                if (!v->enabled || !v->volume || !v->frequency) continue;
                float t = v->phase;
                float val;
                switch (v->waveform) {
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
                voice += val * ((float)v->volume / 255.0f) * 0.20f;
                t += (float)v->frequency / (float)sample_rate;
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
