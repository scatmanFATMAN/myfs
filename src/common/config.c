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
    config_t *next;             //!< A pointer to the next config struct in the linke;
};

static config_t *configs = NULL;
static void (*config_error)(const char *message) = NULL;

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
}

void
config_set_error_func(void (*config_error_func)(const char *message)) {
    config_error = config_error_func;
}

static void
config_errorf(const char *fmt, ...) {
    char message[512];
    va_list ap;

    if (config_error != NULL) {
        va_start(ap, fmt);
        vsnprintf(message, sizeof(message), fmt, ap);
        va_end(ap);

        config_error(message);
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

bool
config_read(int argc, char **argv, const char *path) {
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
                    if (strcmp(key, config->name) == 0) {
                        found = true;

                        //If there's a user defined function, call that instead
                        if (config->func != NULL) {
                            success = config->func(key, value);
                        }
                        else {
                            config_set(key, value);
                        }

                        break;
                    }
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

unsigned int
config_get_uint(const char *name) {
    config_t *config;

    config = config_find(name);
    if (config == NULL) {
        config_errorf("Error getting uint config '%s': Not found", name);
        return 0;
    }

    return strtoul(config->value, NULL, 10);
}
