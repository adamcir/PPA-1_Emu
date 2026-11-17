#ifndef PPA1_MIDI_BROWSER_H
#define PPA1_MIDI_BROWSER_H
#include <stdbool.h>
#include <stddef.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum {
    PPA_BROWSER_MIDI,
    PPA_BROWSER_PROJECT,
    PPA_BROWSER_ASM_SAVE
} PpaBrowserMode;

typedef struct {
    char last_midi[PATH_MAX];
    char last_project[PATH_MAX];
    char last_export[PATH_MAX];
    char favorites[8][PATH_MAX];
    int favorite_count;
} PpaBrowserState;

/* State is persisted in $XDG_CONFIG_HOME/ppa-1-midi-to-asm/browser.conf
 * or $HOME/.config/ppa-1-midi-to-asm/browser.conf. */
void ppa_browser_init(PpaBrowserState *state);
bool ppa_browser_show(PpaBrowserState *state, PpaBrowserMode mode,
                      const char *suggested_filename, char *result, size_t result_len);
const char *ppa_browser_last(const PpaBrowserState *state, PpaBrowserMode mode);
#endif
