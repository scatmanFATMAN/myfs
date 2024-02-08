#pragma once

/**
 * @file config.h
 *
 * A configuration module.
 */

#include <stdbool.h>

/**
 * A callback function to call when an error in this module occurs. The first parameter
 * is an error message.
 */
typedef void (*config_error_func_t)(const char *);

/**
 * A callback function to call to set the config parameter. The first parameter is the name
 * of the parameter and the second parameter is the value.
 * Return `false` to return `false` from `config_read()`.
 */
typedef bool (*config_func_t)(const char *, const char *);

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

/**
 * Sets an error callback to be called when an error occurs.
 *
 * @param[in] func A function pointer to the error handler.
 */
void config_set_error_func(config_error_func_t func);

/**
 * Sets the description of the program which is printed out when --help is used.
 *
 * @param[in] fmt A printf styled format string.
 */
void config_set_description(const char *fmt, ...);

/**
 * Sets the default value for a config parameter referenced by `name`. This function
 * should be called once for every possible config parameter before config_read() is
 * called.
 *
 * @param[in] name The name of the parameter.
 * @param[in] name_command_line The command line switch or NULL to ignore.
 * @param[in] name_config_file The config file name or NULL to ignore.
 * @param[in] value_default The default value for this parameter.
 * @param[in] func A callback function used to set the parameter.
 * @param[in] help Help text to display if --help is used.
 */
void config_set_default(const char *name, const char *name_command_line, const char *name_config_file, const char *value_default, config_func_t func, const char *help);

/**
 * Sets the default int value for a config parameter referenced by `name`. This function
 * should be called once for every possible config parameter before config_read() is
 * called.
 *
 * @param[in] name The name of the parameter.
 * @param[in] name_command_line The command line switch or NULL to ignore.
 * @param[in] name_config_file The config file name or NULL to ignore.
 * @param[in] value_default The default value for this parameter.
 * @param[in] func A callback function used to set the parameter.
 * @param[in] help Help text to display if --help is used.
 */
void config_set_default_int(const char *name, const char *name_command_line, const char *name_config_file, int value_default, config_func_t func, const char *help);

/**
 * Sets the default boolean value for a config parameter referenced by `name`. This function
 * should be called once for every possible config parameter before config_read() is
 * called.
 *
 * @param[in] name The name of the parameter.
 * @param[in] name_command_line The command line switch or NULL to ignore.
 * @param[in] name_config_file The config file name or NULL to ignore.
 * @param[in] value_default The default value for this parameter.
 * @param[in] func A callback function used to set the parameter.
 * @param[in] help Help text to display if --help is used.
 */
void config_set_default_bool(const char *name, const char *name_command_line, const char *name_config_file, bool value_default, config_func_t func, const char *help);

/**
 * Parses this config's command like parameter before the configuration file.
 *
 * @param[in] The name of the parameter.
 */
void config_set_priority(const char *name);

/**
 * Reads a config file located at `path`.
 *
 * @param[in] path The config file to read.
 * @return `false` if an invalid config was found, otherwise `true`.
 */
bool config_read_file(const char *path);

/**
 * Reads in command line arguments `argv` that's `argc` in size. This should
 * typically be the `argc` and `argv` passed into `main()`. If `priority` is set to `true`,
 * only parameters that have been set to a priority will be evaluated.
 *
 * @param[in] argc The number of command line arguments in `argv`.
 * @param[in] argv An array of command line arguments.
 * @param[in] priority `true` to only look at high priority configs, otherwise `false` to only look at low priority configs.
 * @return `false` if an invalid config was found, otherwise `true`.
 */
bool config_read_command_line(int argc, char **argv, bool priority);

/**
 * Reads in command line arguments `argv` that's `argrc` in size. This should
 * typically be the `argc` and `argv` passed into `main()`. This also reads a config
 * file located at `path`. This is equivalent to calling:
 * `config_read_command_line(argc, argv, true);`
 * `config_read_file(path);`
 * `config_read_command_line(argc, argv, false);`
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
 * Determines if the config parameter is set.
 *
 * @param[in] name The name of the parameter.
 * @return `true` if the parameter is set and its value is not `NULL`.
 */
bool config_has(const char *name);

/**
 * Set the value of a config parameter. The value is copied so you may pass in static strings.
 *
 * @param[in] name The name of the parameter.
 * @param[in] value The value of the parameter.
 * @return `false` if the parameter was not found, otherwise `true`.
 */
bool config_set(const char *name, const char *value);

/**
 * Set the boolean value of a config parameter. The value is copied so you may pass in static strings.
 *
 * @param[in] name The name of the parameter.
 * @param[in] value The value of the parameter.
 * @return `false` if the parameter was not found, otherwise `true`.
 */
bool config_set_bool(const char *name, bool value);

/**
 * Gets the value of a config parameter. If not found, NULL is returned.
 *
 * @param[in] name The name of the parameter.
 * @return The value of of the parameter or NULL if not found.
 */
const char * config_get(const char *name);

/**
 * Gets the value of a config parameter. If not found, NULL is returned. This value
 * must be free'd after use.
 *
 * @param[in] name The name of the parameter.
 * @return The value of of the parameter or NULL if not found.
 */
char * config_dupe(const char *name);

/**
 * Gets the value of a config parameter as an int. If not found, 0 is returned.
 *
 * @param[in] name The name of the parameter.
 * @return The value of of the parameter or 0 if not found.
 */
int config_get_int(const char *name);

/**
 * Gets the value of a config parameter as an unsigned int. If not found, 0 is returned.
 *
 * @param[in] name The name of the parameter.
 * @return The value of of the parameter or 0 if not found.
 */
unsigned int config_get_uint(const char *name);
