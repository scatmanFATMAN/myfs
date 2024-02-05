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

/**
 * Creates a prompt using the format string `fmt` and captures user input into
 * `dst`. The newline at the end of the input is stripped.
 *
 * @params[out] dst The buffer to store the input.
 * @params[in] size The size of `dst`.
 * @params[in[ fmt The printf-style format string for the prompt.
 */
void util_create_prompt(char *dst, int size, const char *fmt, ...);

/**
 * Creates a prompt using the format string `fmt` and captures user input into
 * `dst`. The newline at the end of the input is stripped. The echo to the terminal
 * is turned off while input is being read in so that observers cannot see the input
 * on the terminal.
 *
 * @params[out] dst The buffer to store the input.
 * @params[in] size The size of `dst`.
 * @params[in[ fmt The printf-style format string for the prompt.
 */
void util_create_prompt_password(char *dst, int size, const char *fmt, ...);
