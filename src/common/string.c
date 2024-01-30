/**
 * @file string.c
 */

#include <ctype.h>
#include "string.h"

/**
 * Safely copy and NULL terminate a string.
 *
 * @param[in] dst The buffer to copy the string to.
 * @param[in] src The string to copy.
 * @param[in] size The size of the buffer pointed to by `dst`.
 * @return The number of characters copied, not including the NULL character.
 */
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

/**
 * Left and right trim a string in place. Left trimming is done by shifting characters left using
 * memove(). Therefore, the caller does not need to worry about saving the original address of `str` if
 * the memory is to be free()'d.
 *
 * @param[in] str The string to left and right trim.
 * @return The same pointer as `str`.
 */
char *
myfs_trim(char *str) {
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
