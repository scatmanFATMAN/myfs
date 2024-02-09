#pragma once

#include <string.h>
#include <sys/types.h>

/**
 * Parses a path into its file name component and copies it into a buffer.
 *
 * @param[in] path The path to get the file name component of.
 * @param[out] dst The buffer to copy the file name component into.
 * @param[in] size The size of `dst`.
 * @return a pointer to `dst`.
 */
const char * util_basename(const char *path, char *dst, size_t size);

/**
 * Parses a path into its directory component and copies it into a buffer.
 *
 * @param[in] path The path to get the directory component of.
 * @param[out] dst The buffer to copy the directory component into.
 * @param[in] size The size of `dst`.
 * @return a pointer to `dst`.
 */
const char * util_dirname(const char *path, char *dst, size_t size);

/**
 * Looks up a Linux user by its ID and copies its name into a buffer.
 *
 * @param[in] uid The User ID of the user.
 * @param[out] dst The buffer to copy the user's name into.
 * @param[in] size The size of `dst`.
 * @return 0 on success, or an errno otherwise.
 */
int util_username(uid_t uid, char *dst, size_t size);

/**
 * Looks up a Linux user by its name and gets its UID.
 *
 * @param[in] name The name of the user.
 * @param[out] uid The buffer to store the UID into.
 * @return 0 on success, or an errno otherwise.
 */
int util_user_id(const char *name, uid_t *uid);

/**
 * Determines if a Linux user exists by name.
 *
 * @param[in] name The name of the user.
 * @return `true` is the user exists, otherwise `false`.
 */
bool util_user_exists(const char *name);

/**
 * Looks up a Linux group by its ID and copies its name into a buffer.
 *
 * @param[in] gid The Group ID of the group.
 * @param[out] dst The buffer to copy the group's name into.
 * @param[in] size The size of `dst`.
 * @return 0 on success, or an errno otherwise.
 */
int util_groupname(gid_t gid, char *dst, size_t size);

/**
 * Looks up a Linux group by its name and gets its GID.
 *
 * @param[in] name The name of the group.
 * @param[out] gid The buffer to store the GID into.
 * @return 0 on success, or an errno otherwise.
 */
int util_group_id(const char *name, gid_t *gid);

/**
 * Determines if a Linux group exists by name.
 *
 * @param[in] name The name of the group.
 * @return `true` is the group exists, otherwise `false`.
 */
bool util_group_exists(const char *name);

/**
 * Creates a prompt using the format string `fmt` and captures user input into
 * `dst`. The newline at the end of the input is stripped.
 *
 * @params[out] dst The buffer to store the input.
 * @params[in] size The size of `dst`.
 * @params[in] fmt The printf-style format string for the prompt.
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
 * @params[in] fmt The printf-style format string for the prompt.
 */
void util_create_prompt_password(char *dst, int size, const char *fmt, ...);
