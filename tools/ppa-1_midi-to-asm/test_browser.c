#define _POSIX_C_SOURCE 200809L
#include "browser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(void) {
    char home[]="/tmp/ppa-browser-test-XXXXXX";
    assert(mkdtemp(home));
    char conf[4096];
    assert(snprintf(conf,sizeof(conf),"%s/.config",home)<(int)sizeof(conf));
    assert(mkdir(conf,0700)==0);
    char sub[4096];
    assert(snprintf(sub,sizeof(sub),"%s/ppa-1-midi-to-asm",conf)<(int)sizeof(sub));
    assert(mkdir(sub,0700)==0);
    char file[4096];
    assert(snprintf(file,sizeof(file),"%s/browser.conf",sub)<(int)sizeof(file));
    FILE *f=fopen(file,"w");
    assert(f);
    fprintf(f,"PPA-MIDI-BROWSER-1\n");
    fprintf(f,"midi=%s\n",home);
    fprintf(f,"project=%s\n",home);
    fprintf(f,"asm=%s\n",home);
    fprintf(f,"favorite=%s\n",home);
    assert(fclose(f)==0);
    const char *existing=getenv("HOME");
    char *old=existing?strdup(existing):NULL;
    const char *config=getenv("XDG_CONFIG_HOME");
    char *old_config=config?strdup(config):NULL;
    assert(setenv("HOME",home,1)==0);
    unsetenv("XDG_CONFIG_HOME");
    PpaBrowserState state;
    ppa_browser_init(&state);
    assert(!strcmp(ppa_browser_last(&state,PPA_BROWSER_MIDI),home));
    assert(!strcmp(ppa_browser_last(&state,PPA_BROWSER_PROJECT),home));
    assert(!strcmp(ppa_browser_last(&state,PPA_BROWSER_ASM_SAVE),home));
    assert(state.favorite_count==1);
    assert(!strcmp(state.favorites[0],home));
    if(old){setenv("HOME",old,1);free(old);}else unsetenv("HOME");
    if(old_config){setenv("XDG_CONFIG_HOME",old_config,1);free(old_config);}
    else unsetenv("XDG_CONFIG_HOME");
    assert(unlink(file)==0);
    assert(rmdir(sub)==0);
    assert(rmdir(conf)==0);
    char songs[4096];
    snprintf(songs,sizeof(songs),"%s/PPA-Songs",home);
    assert(rmdir(songs)==0);
    assert(rmdir(home)==0);
    puts("PPA-1 MIDI browser settings: OK");
    return 0;
}
