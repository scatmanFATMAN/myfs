#pragma once

#include <string.h>

/**
 * Parses a path into its file name component and copies it into a buffer.
 *
 * @param[in] path The path to get the file name component of.
 * @param[out] dst The buffer to copy the file name component into.
 * @param[in[ size The size of `buffer`.
 * @return a pointer to `dst`.
 */
const char * util_basename(const char *path, char *dst, size_t size);

/**
 * Parses a path into its directory component and copies it into a buffer.
 *
 * @param[in] path The path to get the directory component of.
 * @param[out] dst The buffer to copy the directory component into.
 * @param[in[ size The size of `buffer`.
 * @return a pointer to `dst`.
 */
const char * util_dirname(const char *path, char *dst, size_t size);
