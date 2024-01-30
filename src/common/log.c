/**
 * @file log.c
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "log.h"

typedef struct {
    log_severity_t severity;
} log_t;

static log_t log;

void
log_init() {
    log.severity = LOG_SEVERITY_INFO;
}

void
log_free() {
}

static char
log_severity_char(log_severity_t severity) {
    switch (severity) {
        case LOG_SEVERITY_ERR:
            return 'E';
        case LOG_SEVERITY_WARN:
            return 'W';
        case LOG_SEVERITY_INFO:
            return 'I';
        case LOG_SEVERITY_DEBUG:
            return 'D';
    }

    return 'U';
}

void
log_write(const char *module, log_severity_t severity, const char *fmt, ...) {
    char message[512];
    va_list ap;
    time_t now;
    struct tm tm;

    //Make sure we're set to log this severity
    if (severity > log.severity) {
        return;
    }

    now = time(NULL);
    memset(&tm, 0, sizeof(tm));
    localtime_r(&now, &tm);

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    //TODO: log to file and console?

    printf("[%02d:%02d:%02d] %c [%s] %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, log_severity_char(severity), module, message);
}
