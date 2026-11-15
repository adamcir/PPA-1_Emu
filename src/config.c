#define _GNU_SOURCE
#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char g_config_path[PATH_MAX] = "out/config.ini";
static char g_log_path[PATH_MAX] = "out/latest.log";

static void set_paths_from_executable(const char *path) {
    if (!path || !*path) return;
    char dir[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(dir)) return;
    memcpy(dir, path, len + 1);
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    if (slash == dir) slash[1] = '\0';
    else *slash = '\0';
    /* Check lengths before copying. Do not silently truncate paths. */
    size_t dir_len = strlen(dir);
    static const char config_name[] = "/config.ini";
    static const char log_name[] = "/latest.log";
    if (dir_len > sizeof(g_config_path) - sizeof(config_name) ||
        dir_len > sizeof(g_log_path) - sizeof(log_name)) {
        return; /* Keep previously configured fallback paths. */
    }
    memcpy(g_config_path, dir, dir_len);
    memcpy(g_config_path + dir_len, config_name, sizeof(config_name));
    memcpy(g_log_path, dir, dir_len);
    memcpy(g_log_path + dir_len, log_name, sizeof(log_name));
}

void ppa_config_init_paths(const char *argv0) {
#if defined(__linux__)
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        set_paths_from_executable(exe);
        return;
    }
#endif
    /* Fallback for platforms where /proc/self/exe does not exist. */
    if (argv0 && strchr(argv0, '/'))
        set_paths_from_executable(argv0);
}

const char *ppa_config_path(void) { return g_config_path; }
const char *ppa_log_path(void) { return g_log_path; }

void ppa_config_defaults(PpaConfig *config) {
    if (!config) return;
    config->rom_size = PPA_CONFIG_MAX_MEMORY;
    config->ram_size = PPA_CONFIG_MAX_MEMORY;
    config->auto_run = false;
    config->lamp_test = false;
    config->tty_enabled = false;
}

static char *trim(char *p) {
    while (isspace((unsigned char)*p)) p++;
    char *end = p + strlen(p);
    while (end > p && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return p;
}

static bool parse_memory(const char *s, uint32_t *dst) {
    if (!s || !*s || !isdigit((unsigned char)*s)) return false;
    char *end;
    errno = 0;
    unsigned long value = strtoul(s, &end, 10);
    if (errno || *end || value < 1 || value > PPA_CONFIG_MAX_MEMORY)
        return false;
    *dst = (uint32_t)value;
    return true;
}

static bool parse_bool(const char *s, bool *dst) {
    if (!strcmp(s, "1") || !strcmp(s, "true")) { *dst = true; return true; }
    if (!strcmp(s, "0") || !strcmp(s, "false")) { *dst = false; return true; }
    return false;
}

bool ppa_config_load(PpaConfig *config) {
    if (!config) return false;
    ppa_config_defaults(config);
    FILE *f = fopen(ppa_config_path(), "r");
    if (!f) {
        if (errno == ENOENT) return ppa_config_save(config);
        return false;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *key = trim(line);
        if (!*key || *key == ';' || *key == '#') continue;
        char *eq = strchr(key, '=');
        if (!eq) continue;
        *eq++ = '\0';
        key = trim(key);
        char *val = trim(eq);
        if (!strcmp(key, "rom_size")) parse_memory(val, &config->rom_size);
        else if (!strcmp(key, "ram_size")) parse_memory(val, &config->ram_size);
        else if (!strcmp(key, "auto_run")) parse_bool(val, &config->auto_run);
        else if (!strcmp(key, "lamp_test")) parse_bool(val, &config->lamp_test);
        else if (!strcmp(key, "tty_enabled")) parse_bool(val, &config->tty_enabled);
    }
    return fclose(f) == 0;
}

bool ppa_config_save(const PpaConfig *config) {
    if (!config) return false;
    /* A temporary file and rename keep the last good configuration. */
    char tmp[PATH_MAX];
    if (strlen(ppa_config_path()) + 5 >= sizeof(tmp)) return false;
    snprintf(tmp, sizeof(tmp), "%s.tmp", ppa_config_path());
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    int written = fprintf(f,
        "# PPA-1 Emulator configuration\n"
        "# Saved next to PPA-1_Emu. Memory sizes in bytes (1..2097152).\n"
        "rom_size=%u\n"
        "ram_size=%u\n"
        "auto_run=%u\n"
        "lamp_test=%u\n"
        "tty_enabled=%u\n",
        (unsigned)config->rom_size, (unsigned)config->ram_size,
        config->auto_run ? 1u : 0u, config->lamp_test ? 1u : 0u,
        config->tty_enabled ? 1u : 0u);
    bool ok = written >= 0 && fflush(f) == 0;
    if (fclose(f) != 0) ok = false;
    if (ok) ok = rename(tmp, ppa_config_path()) == 0;
    if (!ok) remove(tmp);
    return ok;
}
