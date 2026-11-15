#ifndef PPA_CONFIG_H
#define PPA_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define PPA_CONFIG_MAX_MEMORY 2097152u
#define PPA_CONFIG_MODULE_SLOTS 8
#define PPA_CONFIG_MODULE_PATH 1024

typedef struct {
    char path[PPA_CONFIG_MODULE_PATH];
    bool active;
    bool visible;
    int x, y, width, height;
} PpaModuleLayout;

typedef struct {
    uint32_t rom_size;
    uint32_t ram_size;
    bool auto_run;
    bool lamp_test;
    bool tty_enabled;
    int panel_x, panel_y, panel_width, panel_height;
    PpaModuleLayout modules[PPA_CONFIG_MODULE_SLOTS];
} PpaConfig;

/* Paths are relative to the executable directory, not the caller's CWD. */
void ppa_config_init_paths(const char *argv0);
const char *ppa_config_path(void);
const char *ppa_log_path(void);

void ppa_config_defaults(PpaConfig *config);
bool ppa_config_load(PpaConfig *config);
bool ppa_config_save(const PpaConfig *config);

#endif
