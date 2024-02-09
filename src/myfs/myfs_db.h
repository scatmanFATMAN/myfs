#pragma once

/**
 * @file myfs_db.h
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "myfs.h"

/**
 * Inserts a new file record into MariaDB with the given file type and parent.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] name The name of the file.
 * @param[in] type The file type.
 * @param[in] parent_id The File ID of the parent the file should be created in.
 * @param[in] mode The mode to create the file with.
 * @param[in] content Any initial content for the file.
 * @return `true` if the file was created, otherwise `false`.
 */
bool myfs_db_file_create(myfs_t *myfs, const char *name, myfs_file_type_t type, unsigned int parent_id, mode_t mode, const char *content);

/**
 * Deletes a file from MariaDB. If this file is a parent to other files, all children will
 * be deleted in a cascading fashion.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID of the file to delete.
 * @return `true` if the file was deleted, otherwise `false`.
 */
bool myfs_db_file_delete(myfs_t *myfs, unsigned int file_id);

/**
 * Update the last accessed and last modified timestamps of the given File ID.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID to update.
 * @param[in] last_accessed_on The last accessed timestamp to set.
 * @param[in] last_modified_on The last modified timestamp to set.
 * @return `true` if the file was updated, otherwise `false`.
 */
bool myfs_db_file_set_times(myfs_t *myfs, unsigned int file_id, time_t last_accessed_on, time_t last_modified_on);

/**
 * Sets the user and group of the given File ID. If either `user` or `group` are NULL or blank, then that
 * value will be ignored.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID to update.
 * @param[in] user The user to set, or NULL or blank to not set.
 * @param[in] group The group to set, or NULL or blank to not set.
 * @return `true` if the file was updated, otherwise `false`.
 */
bool myfs_db_file_chown(myfs_t *myfs, unsigned int file_id, const char *user, const char *group);

/**
 * Sets the mode of the given File ID.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID to update.
 * @param[in] mode The mode to set.
 * @return `true` if the file was updated, otherwise `false`.
 */
bool myfs_db_file_chmod(myfs_t *myfs, unsigned int file_id, mode_t mode);

/**
 * Swaps two files atomically.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file1 The first file to swap.
 * @param[in] file2 The second file to swap.
 * @return `true` if the files were swaped, otherwise `false`.
 */
bool myfs_db_file_swap(myfs_t *myfs, myfs_file_t *file1, myfs_file_t *file2);

/**
 * Updates both the parent and the name of a file (moves/renames it).
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID to rename.
 * @param[in] parent_id The new Parent ID to set.
 * @param[in] name The new name to set.
 * @return `true` if the file was renamed, otherwise `false`.
 */
bool myfs_db_file_rename(myfs_t *myfs, unsigned int file_id, unsigned int parent_id, const char *name);

/**
 * Gets the file content and optionally its size.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID to get content for.
 * @param[out] len The length of `content`, or `NULL` to ignore.
 * @return The file content which must be free()'d or `NULL` if an error occurred.
 */
char * myfs_db_file_get_content(myfs_t *myfs, unsigned int file_id, size_t *len);

/**
 * Sets the size of the content. This is going to be interesting functionality in a database.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID to update.
 * @param[in] size The size to set the content to.
 * @return `true` if the file was updated, otherwise `false`.
 */
bool myfs_db_file_set_content_size(myfs_t *myfs, unsigned int file_id, off_t size);

/**
 * Queries MariaDB for a MyFS file's data, including its parent and possibly its children. If querying for
 * the file's children, it must be a MYSYS_FILE_TYPE_DIRECTORY.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID of the MyFS file.
 * @param[in] include_children `true` to also query for the MyFS's children.
 * @return The MyFS file or `NULL` if an error occurred.
 */
myfs_file_t * myfs_db_file_query(myfs_t *myfs, unsigned int file_id, bool include_children);

/**
 * Queries MariaDB for a MyFS's file by its name.  If querying for the file's children,
 * it must be a MYSYS_FILE_TYPE_DIRECTORY.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] name The name of the MyFS file to look for.
 * @param[in] parent_id The Parent ID of the MyFS file to look for.
 * @param[in] include_children `true` to also query for the MyFS's children.
 * @return The MyFS file or `NULL` if an error occurred.
 */
myfs_file_t * myfs_db_file_query_name(myfs_t *myfs, const char *name, unsigned int parent_id, bool include_children);

/**
 * Gets the number of files in the database.
 *
 * @param[in] myfs The MyFS context.
 * @param[out] count The number of files in the database.
 * @return `true` on success, otherwise `false`.
 */
bool myfs_db_get_num_files(myfs_t *myfs, uint64_t *count);

/**
 * Gets the disk spaced used by the database in bytes.
 *
 * @param[in] myfs The MyFS context.
 * @param[out] space The spaced used in bytes.
 * @return `true` on success, otherwise `false`.
 */
bool myfs_db_get_space_used(myfs_t *myfs, uint64_t *space);
