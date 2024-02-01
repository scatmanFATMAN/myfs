/**
 * @file log.c
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include "log.h"

typedef struct {
    log_severity_t severity;        //<! The minimum severity to log
    bool to_stdout;                 //<! Logging to stdout?
    bool to_syslog;                 //<! Logging to syslog?
} log_t;

static log_t log;

void
log_init() {
    log.severity = LOG_SEVERITY_INFO;
    log.to_stdout = true;
    log.to_syslog = false;
}

void
log_free() {
    if (log.to_syslog) {
        closelog();
    }
}

void
log_set_severity(log_severity_t severity) {
    log.severity = severity;
}

void
log_to_stdout(bool enable) {
    log.to_stdout = enable;
}

void
log_to_syslog(const char *name) {
    if (name != NULL) {
        openlog(name, LOG_PID | LOG_NDELAY, LOG_USER);
        log.to_syslog = false;
    }
    else {
        closelog();
        log.to_syslog = false;
    }
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

static int
log_syslog_severity(log_severity_t severity) {
    switch (severity) {
        case LOG_SEVERITY_ERR:
            return LOG_ERR;
        case LOG_SEVERITY_WARN:
            return LOG_WARNING;
        case LOG_SEVERITY_INFO:
            return LOG_INFO;
        case LOG_SEVERITY_DEBUG:
            return LOG_DEBUG;
    }

    return 0;   //not sure what this will do
}

void
log_write(const char *module, log_severity_t severity, const char *fmt, ...) {
    char message[512];
    va_list ap;
    time_t now;
    struct tm tm;

    //make sure the logging system is logging to someplace
    if (!log.to_stdout && !log.to_syslog) {
        return;
    }

    //Make sure the logging system is set to log this severity
    if (severity > log.severity) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    if (log.to_stdout) {
        now = time(NULL);
        memset(&tm, 0, sizeof(tm));
        localtime_r(&now, &tm);

        printf("[%02d:%02d:%02d] %c [%s] %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, log_severity_char(severity), module, message);
    }

    if (log.to_syslog) {
        syslog(log_syslog_severity(severity), "%s", message);
    }
}
