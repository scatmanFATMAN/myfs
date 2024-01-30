#pragma once

/**
 * @file string.h
 *
 * Custom string functions.
 */

#include <string.h>

/**
 * Safely copy and NULL terminate a string.
 *
 * @param[in] dst The buffer to copy the string to.
 * @param[in] src The string to copy.
 * @param[in] size The size of the buffer pointed to by `dst`.
 * @return The number of characters copied, not including the NULL character.
 */
size_t strlcpy(char *dst, const char *src, size_t size);
