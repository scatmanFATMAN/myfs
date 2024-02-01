#pragma once

/**
 * @file log.h
 *
 * A logging module.
 */

#include <stdbool.h>

/**
 * Convenience macros so the severity is chosen by the macro name.
 */
#define log_err(module, fmt, ...)   log_write(module, LOG_SEVERITY_ERR, fmt, ##__VA_ARGS__)
#define log_warn(module, fmt, ...)  log_write(module, LOG_SEVERITY_WARN, fmt, ##__VA_ARGS__)
#define log_info(module, fmt, ...)  log_write(module, LOG_SEVERITY_INFO, fmt, ##__VA_ARGS__)
#define log_debug(module, fmt, ...) log_write(module, LOG_SEVERITY_DEBUG, fmt, ##__VA_ARGS__)

/**
 * The log severities.
 */
typedef enum {
    LOG_SEVERITY_ERR,   //!< Error messages.
    LOG_SEVERITY_WARN,  //!< Warn messages.
    LOG_SEVERITY_INFO,  //!< Info messages.
    LOG_SEVERITY_DEBUG  //!< Debug messages.
} log_severity_t;

/**
 * Initializes the log system. This must be called before any other log functions are called.
*/
void log_init();

/**
 * Frees the log system. No more log functions can be called after this.
 */
void log_free();

/**
 * Sets the minimum logging level.
 *
 * @param[in] severity The minimum logging severity.
 */
void log_set_severity(log_severity_t severity);

/**
 * Turn on or off logging to the console.
 *
 * @param[in] enable `true` to turn on console logging, `false` to disable it.
 */
void log_to_stdout(bool enable);

/**
 * Turn on or off logging to syslog. If `name` is not `NULL`, then `openlog()` is called. If
 * `name` is `NULL`, then `closelog()` is called.
 *
 * @param[in] name The name sent to syslog.
 */
void log_to_syslog(const char *name);

/**
 * Writes a log message unless the `severity` is greater than the configured logging severity.
 *
 * @param[in] module A string that represents what module the message came from.
 * @param[in] severity The severity of the message.
 * @param[in] fmt A printf styled format string for the message.
 */
void log_write(const char *module, log_severity_t severity, const char *fmt, ...);
