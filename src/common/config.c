/**
 * @file config.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include "string.h"
#include "config.h"

typedef struct config_t config_t;

/**
 * Represents a config parameter.
 */
struct config_t {
    char *name;                 //!< The name of the config parameter.
    char *name_command_line;    //!< The name of the config parameter's command line switch.
    char *name_config_file;     //!< The name of the config parameters in the config file.
    char *value;                //!< The value of the config parameter.
    char *value_default;        //!< The default value of the config parameter. Need to keep this around when displaying help.
    config_func_t func;         //!< A function to call to set the config parameter.
    char *help;                 //!< Text to display for the help.
    bool priority;              //!< Parse this config's command line before the config file.
    config_t *next;             //!< A pointer to the next config struct in the linke;
};

/** Linked list of all the config parameters. */
static config_t *configs = NULL;

/** A description of the program to print out when --help is used. */
static char *config_description = NULL;

/** Error callback function when an error occurs. */
static config_error_func_t config_error_func = NULL;

void
config_init() {
}

void
config_free() {
    config_t *config, *config_del;

    config = configs;
    while (config != NULL) {
        config_del = config;
        config = config->next;

        free(config_del->name);
        if (config_del->name_command_line != NULL) {
            free(config_del->name_command_line);
        }
        if (config_del->name_config_file != NULL) {
            free(config_del->name_config_file);
        }
        free(config_del->value);
        free(config_del->value_default);
        free(config_del->help);
        free(config_del);
    }

    if (config_description != NULL) {
        free(config_description);
    }
}

void
config_set_error_func(config_error_func_t func) {
    config_error_func = func;
}

static void
config_errorf(const char *fmt, ...) {
    char message[512];
    va_list ap;

    if (config_error_func != NULL) {
        va_start(ap, fmt);
        vsnprintf(message, sizeof(message), fmt, ap);
        va_end(ap);

        config_error_func(message);
    }
}

void
config_set_description(const char *fmt, ...) {
    va_list ap;
    int ret;

    if (config_description != NULL) {
        free(config_description);
    }

    va_start(ap, fmt);
    ret = vasprintf(&config_description, fmt, ap);
    va_end(ap);

    if (ret == -1) {
        config_errorf("Error setting description: Out of memory");
    }
}

void
config_set_default(const char *name, const char *name_command_line, const char *name_config_file, const char *value_default, config_func_t func, const char *help) {
    config_t *config;

    //Build the config structure.
    config = calloc(1, sizeof(*config));
    config->name = strdup(name);
    if (name_command_line != NULL) {
        config->name_command_line = strdup(name_command_line);
    }
    if (name_config_file != NULL) {
        config->name_config_file = strdup(name_config_file);
    }
    if (value_default != NULL) {
        config->value = strdup(value_default);
        config->value_default = strdup(value_default);
    }
    config->func = func;
    if (help != NULL) {
        config->help = strdup(help);
    }

    //Add to the linked list.
    if (configs == NULL) {
        configs = config;
    }
    else {
        config->next = configs;
        configs = config;
    }
}

void
config_set_default_bool(const char *name, const char *name_command_line, const char *name_config_file, bool value_default, config_func_t func, const char *help) {
    char default_value_str[8];

    snprintf(default_value_str, sizeof(default_value_str), "%s", value_default ? "true" : "false");

    config_set_default(name, name_command_line, name_config_file, default_value_str, func, help);
}

static config_t *
config_find(const char *name) {
    config_t *config;

    config = configs;
    while (config != NULL) {
        if (strcmp(config->name, name) == 0) {
            return config;
        }

        config = config->next;
    }

    return NULL;
}

void
config_set_priority(const char *name) {
    config_t *config;

    config = config_find(name);

    if (config == NULL) {
        config_errorf("Error setting priority for '%s': Config not found", name);
        return;
    }

    config->priority = true;
}

/**
 * Left and right trim a string in place. Left trimming is done by shifting characters left using
 * memove(). Therefore, the caller does not need to worry about saving the original address of `str` if
 * the memory is to be free()'d.
 *
 * @param[in] str The string to left and right trim.
 * @return The same pointer as `str`.
 */
static char *
config_trim(char *str) {
    char *ptr;

    //Left trim.
    ptr = str;
    while (isspace(*ptr)) {
        ptr++;
    }
    //If we had to left trim, move the new start to the beginning of 'str', including the NULL terminator.
    if (str != ptr) {
        memmove(str, ptr, strlen(ptr) + 1);
    }

    //Right trim.
    ptr = str + strlen(str) - 1;
    while (ptr > str && isspace(*ptr)) {
        *ptr = '\0';
        ptr--;
    }

    return str;
}

static void
config_print_help() {
    config_t *config;

    if (config_description != NULL) {
        fprintf(stderr, "%s\n\n", config_description);
    }

    fprintf(stderr, "-----------------------------------------------------------------------------------------------------------------------------------\n");
    fprintf(stderr, "%-25s%-25s%-25s%-25s%-20s\n", "Name", "Command Line", "Config File", "Default Value", "Help");
    fprintf(stderr, "-----------------------------------------------------------------------------------------------------------------------------------\n");

    config = configs;
    while (config != NULL) {
        fprintf(stderr, "%-25s", config->name);
        fprintf(stderr, "%-25s", config->name_command_line != NULL ? config->name_command_line : "");
        fprintf(stderr, "%-25s", config->name_config_file != NULL ? config->name_config_file : "");
        fprintf(stderr, "%-25s", config->value_default != NULL ? config->value_default : "");
        fprintf(stderr, "%-20s", config->help);
        fprintf(stderr, "\n");
        config = config->next;
    }

    fprintf(stderr, "------------------------------------------------------------------------------------------------------------------------------------\n");
    fprintf(stderr, "\n");
}

bool
config_read_file(const char *path) {
    char line[512], *key, *value, *save;
    bool found, success = true;
    FILE *f;
    config_t *config;

    f = fopen(path, "r");
    if (f == NULL) {
        config_errorf("Error reading '%s': %s", path, strerror(errno));
        return false;
    }

    //Read each line. Each line's key and value are separated by an '=' sign. Leading and Trailing whitespace is trimmed.
    //The following are examples of valid lines:
    //
    //  key = value
    //  key=value
    //  key     =       value
    while (fgets(line, sizeof(line), f) != NULL) {
        config_trim(line);

        //Any blank lines or lines that start with a '#' are ignored.
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        //The key is on the left of the '='.
        key = strtok_r(line, "=", &save);
        if (key != NULL) {
            config_trim(key);

            //The value is on the right of the '='.
            value = strtok_r(NULL, "\n", &save);
            if (value != NULL) {
                config_trim(value);

                //Loop through all the possible configs and look for the key
                found = false;
                config = configs;
                while (config != NULL) {
                    if (config->name_config_file != NULL && strcmp(key, config->name_config_file) == 0) {
                        found = true;

                        //If there's a user defined function, call that instead
                        if (config->func != NULL) {
                            success = config->func(config->name, value);
                        }
                        else {
                            config_set(config->name, value);
                        }

                        break;
                    }

                    config = config->next;
                }

                if (!found) {
                    config_errorf("Error parsing '%s': Unknown key '%s'", path, key);
                    success = false;
                }
            }
        }
    }

    fclose(f);
    return success;
}

bool
config_read_command_line(int argc, char **argv, bool priority) {
    config_t *config;
    bool found;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            config_print_help();
            return false;
        }

        //Make sure there's a value associated with the parameter.
        if (i + 1 >= argc) {
            config_errorf("Error parsing command line arguments: Parameter '%s' has no value", argv[i]);
            return false;
        }

        //Look for the command line parameter in the config list.
        found = false;
        config = configs;
        while (config != NULL) {
            if (config->name_command_line != NULL && strcmp(argv[i], config->name_command_line) == 0 && ((priority && config->priority) || (!priority && !config->priority))) {
                found = true;

                //If there's a user defined function, call that instead.
                if (config->func != NULL) {
                    if (!config->func(config->name, argv[i + 1])) {
                        return false;
                    }
                }
                else {
                    config_set(config->name, argv[i + 1]);
                }

                break;
            }

            config = config->next;
        }

        if (!found) {
            config_errorf("Error parsing command line arguments: Parameter '%s' not found", argv[i]);
            return false;
        }

        //Skip the value and go to the next config parameter.
        i++;
    }

    return true;
}

bool
config_read(int argc, char **argv, const char *path) {
    return config_read_command_line(argc, argv, true) &&
           config_read_file(path) &&
           config_read_command_line(argc, argv, false);
}

bool
config_has(const char *name) {
    config_t *config;

    config = config_find(name);

    return config != NULL && config->value != NULL;
}

bool
config_set(const char *name, const char *value) {
    config_t *config;

    config = config_find(name);
    if (config == NULL) {
        config_errorf("Error setting config '%s': Not found", name);
        return false;
    }

    //Make sure any previous value is free()'d.
    if (config->value != NULL) {
        free(config->value);
    }

    config->value = strdup(value);
    return true;
}

bool
config_set_bool(const char *name, bool value) {
    char value_str[8];

    snprintf(value_str, sizeof(value_str), "%s", value ? "true" : "false");

    return config_set(name, value_str);
}

const char *
config_get(const char *name) {
    config_t *config;

    config = config_find(name);
    if (config == NULL) {
        config_errorf("Error getting config '%s': Not found", name);
        return NULL;
    }

    return config->value;
}

char *
config_dupe(const char *name) {
    const char *value;

    value = config_get(name);

    return value == NULL ? NULL : strdup(value);
}

unsigned int
config_get_uint(const char *name) {
    const char *value;

    value = config_get(name);

    return value == NULL ? 0 : strtoul(value, NULL, 10);
}
