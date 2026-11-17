/* PPA-SOUND/1 ADSR registers, gate transitions and legacy compatibility. */
#include "ppa_sound.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    PpaModuleBus *bus=calloc(1,sizeof(*bus));assert(bus);
    PpaModule *m=&bus->slots[0];
    m->installed=true;m->active=true;m->type=PPAM_TYPE_SOUND;
    ppa_sound_init(m);
    ppa_sound_reset(m);
    m->sound_master=true;
    ppa_sound_write(m,0x20,0xB8);ppa_sound_write(m,0x21,0x01); /* 440 Hz */
    ppa_sound_write(m,0x22,3);ppa_sound_write(m,0x23,255);
    ppa_sound_write(m,0x24,1);
    int16_t buffer[2048];
    /* Legacy sound is audible without any GATE writes. */
    ppam_mix_audio(bus,buffer,2048,44100);
    int nonzero=0;
    for(int i=0;i<2048;i++)if(buffer[i])nonzero++;
    assert(nonzero>1500 && !m->sound[0].adsr_active);

    /* Attack length 128 ~0.50 s; decay 0 => sustain 128/255.
     * Release 128 ~1.0 s from the held envelope level. */
    ppa_sound_write(m,0x25,128);
    ppa_sound_write(m,0x26,0);
    ppa_sound_write(m,0x27,128);
    ppa_sound_write(m,0x28,128);
    ppa_sound_write(m,0x29,2); /* explicit retrigger */
    assert(m->sound[0].adsr_active && m->sound[0].gate);
    ppam_mix_audio(bus,buffer,1024,44100);
    float attack_level=m->sound[0].envelope_level;
    assert(attack_level>0.0f && attack_level<0.15f);
    for(int i=0;i<22;i++)ppam_mix_audio(bus,buffer,1024,44100);
    assert(m->sound[0].envelope_level>0.49f &&
           m->sound[0].envelope_level<0.52f);
    /* Changing VOLUME and FREQ to zero is common at MIDI note-off.
     * Release must use the last note's amplitude and pitch. */
    ppa_sound_write(m,0x23,0);
    ppa_sound_write(m,0x20,0);
    ppa_sound_write(m,0x21,0);
    ppa_sound_write(m,0x29,0);
    assert(!m->sound[0].gate);
    assert(m->sound[0].release_frequency==440);
    assert(m->sound[0].release_volume==255);
    ppam_mix_audio(bus,buffer,1024,44100);
    nonzero=0;
    for(int i=0;i<1024;i++)if(buffer[i])nonzero++;
    assert(nonzero>400 && m->sound[0].envelope_phase==4);
    for(int i=0;i<50;i++)ppam_mix_audio(bus,buffer,1024,44100);
    assert(m->sound[0].envelope_phase==0 && m->sound[0].envelope_level==0.0f);
    /* Same test across all four oscillator register windows. */
    for(int i=0;i<4;i++) {
        int base=0x20+i*16;
        ppa_sound_write(m,base+5,(uint8_t)(i+1));
        ppa_sound_write(m,base+6,(uint8_t)(i+2));
        ppa_sound_write(m,base+7,(uint8_t)(i+3));
        ppa_sound_write(m,base+8,(uint8_t)(i+4));
        assert(ppa_sound_read(m,base+5)==i+1);
        assert(ppa_sound_read(m,base+6)==i+2);
        assert(ppa_sound_read(m,base+7)==i+3);
        assert(ppa_sound_read(m,base+8)==i+4);
        ppa_sound_write(m,base+9,2);
        assert(ppa_sound_read(m,base+9)==1);
        ppa_sound_write(m,base+9,0);
        assert(ppa_sound_read(m,base+9)==0);
    }
    free(bus);
    puts("PPA-SOUND/1 ADSR regression tests: OK");
    return 0;
}
