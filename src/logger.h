#ifndef PPA1_LOGGER_H
#define PPA1_LOGGER_H

#include <stdio.h>

int ppa_log_open(const char *path);
void ppa_log_close(void);
int ppa_log_printf(const char *fmt, ...);
void ppa_log_flush(void);
FILE *ppa_log_file(void);

#endif
