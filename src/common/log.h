#pragma once

/**
 * @file log.h
 *
 * A logging module.
 */

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
 * Writes a log message unless the `severity` is greater than the configured logging severity.
 *
 * @param[in] module A string that represents what module the message came from.
 * @param[in] severity The severity of the message.
 * @param[in] fmt A printf styled format string for the message.
 */
void log_write(const char *module, log_severity_t severity, const char *fmt, ...);
