/**
 * @file string.c
 */

#include <ctype.h>
#include "string.h"

size_t
strlcpy(char *dst, const char *src, size_t size) {
    char *d = dst;
    const char *s = src;
    size_t n = size;

    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0') {
                break;
            }
        }
    }

    if (n == 0) {
        if (size != 0) {
            *d = '\0';
        }
        while (*s++);
    }

    return s - src - 1;
}

bool
str_ends_with(const char *str, const char *match) {
    size_t str_len, match_len;

    str_len = strlen(str);
    match_len = strlen(match);

    if (match_len == 0 || match_len > str_len) {
        return false;
    }

    return strncmp(str + (str_len - match_len), match, match_len) == 0;
}
