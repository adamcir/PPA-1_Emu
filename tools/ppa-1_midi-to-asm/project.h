#ifndef PPA1_MIDI_PROJECT_H
#define PPA1_MIDI_PROJECT_H
#include "midi_core.h"
#include <stdbool.h>
#include <stddef.h>

/* PPASP1 little-endian portable binary project format.
 * Owns embedded source MIDI and all per-track / per-part settings.
 * Save always writes into $HOME/PPA-Songs/<sanitized-name>.ppasp.
 * Load extracts embedded MIDI into a temporary owned file; caller must
 * unlink that temporary MIDI on exit after loading a different project. */
bool ppa_project_save(const char *name, const char *midi_path,
                      const PpaMidiTrack *tracks, const PpaMidiPart *parts,
                      int count, bool optimize, int clock_hz, int tick_ms,
                      char *saved_path, size_t saved_cap,
                      char *error, size_t error_cap);
bool ppa_project_load(const char *project_path, char *temp_midi,
                      size_t temp_cap, PpaMidiTrack *tracks,
                      PpaMidiPart *parts, int cap, int *count,
                      bool *optimize, int *clock_hz, int *tick_ms,
                      char *error, size_t error_cap);
bool ppa_project_folder(char *path, size_t cap,
                        char *error, size_t error_cap);
#endif
