#include "logger.h"

#include <stdarg.h>
#include <time.h>

static FILE *g_log = NULL;

int ppa_log_open(const char *path) {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }

    g_log = fopen(path, "w");
    if (!g_log) return 0;

    time_t now = time(NULL);
    struct tm tm_now;
#if defined(_WIN32)
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif

    char stamp[64];
    if (strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_now) > 0) {
        fprintf(g_log, "PPA-1 Emulator log started %s\n", stamp);
        fprintf(g_log, "========================================\n");
    }

    fflush(g_log);
    return 1;
}

void ppa_log_close(void) {
    if (!g_log) return;
    fflush(g_log);
    fclose(g_log);
    g_log = NULL;
}

int ppa_log_printf(const char *fmt, ...) {
    va_list ap_stdout;
    va_list ap_log;

    va_start(ap_stdout, fmt);
    va_copy(ap_log, ap_stdout);

    int rc = vprintf(fmt, ap_stdout);
    fflush(stdout);

    if (g_log) {
        vfprintf(g_log, fmt, ap_log);
        fflush(g_log);
    }

    va_end(ap_log);
    va_end(ap_stdout);
    return rc;
}

void ppa_log_flush(void) {
    fflush(stdout);
    if (g_log) fflush(g_log);
}

FILE *ppa_log_file(void) {
    return g_log;
}
