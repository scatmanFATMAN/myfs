#pragma once

/**
 * @file config.h
 *
 * A configuration module.
 */

#include <stdbool.h>

/**
 * Initializes the config system. Must be called before any
 * other config function is called.
 */
void config_init();

/**
 * Frees the config system. No other config functions may be
 * called after this.
 */
void config_free();

void config_set_error_func(void (*config_error_func)(const char *message));

/**
 * Sets the default value for a config parameter referenced by `name`. This function
 * should be called once for every possible config parameter before config_read() is
 * called.
 *
 * @param[in] name The name of the parameter.
 * @param[in] name_command_line The command line switch or NULL to ignore.
 * @param[in] name_config_file The config file name or NULL to ignore.
 * @param[in] value_default The default value for this parameter.
 * @param[in] help Help text to display if --help is used.
 */
void config_set_default(const char *name, const char *name_command_line, const char *name_config_file, const char *value_default, const char *help);

/**
 * Reads in command line arguments `argv` that's `argrc` in size. This should
 * typically be the `argc` and `argv` passed into main(). This also reads a config
 * file located at `path`.
 *
 * The file is read first, then the command line arguments. That means the command
 * line arguments overwrite (take precedence over) anything set in the config file.
 *
 * If `argc` is 0 or `argv` is NULL, parsing the command line arguments will be
 * skipped. Likewise, if `path` is NULL then reading the config file will be skipped.
 *
 * @param[in] argc The number of command line arguments in `argv`.
 * @param[in] argv An array of command line arguments.
 * @param[in] path The path to a config file.
 * @return `false` if an invalid config was found, otherwise `true`.
 */
bool config_read(int argc, char **argv, const char *path);

/**
 * Set the value of a config parameter. The value is copied so you may pass in static strings.
 *
 * @param[in] name The name of the parameter.
 * @param[in] value The value of the parameter.
 * @return `false` if the parameter was not found, otherwise `true`.
 */
bool config_set(const char *name, const char *value);

/**
 * Gets the value of a config parameter. If not found, NULL is returned.
 *
 * @param[in] name The name of the parameter.
 * @return The value of of the parameter or NULL if not found.
 */
const char * config_get(const char *name);

/**
 * Gets the value of a config parameter as an unsigned int. If not found, 0 is returned.
 *
 * @param[in] name The name of the parameter.
 * @return The value of of the parameter or 0 if not found.
 */

unsigned int config_get_uint(const char *name);
