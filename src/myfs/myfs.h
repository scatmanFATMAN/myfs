#pragma once

/**
 * @file myfs.h
 */

#include <stdbool.h>
#include <time.h>
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include "../common/db.h"

/** The maximum length a file name can be. */
#define MYFS_FILE_NAME_MAX_LEN 64

/** The maximum length a file path can be. */
#define MYFS_PATH_NAME_MAX_LEN 1024

/** The maximum number of open files. */
#define MYFS_FILES_OPEN_MAX 128

/** The maximum length of a user name and group name, as specified in Linux's useradd and groupadd programs. */
#define MYFS_USER_NAME_MAX_LEN  32
#define MYFS_GROUP_NAME_MAX_LEN 32

/**
 *  The possible types for files.
 */
typedef enum {
    MYFS_FILE_TYPE_INVALID,         //!< Default value and shouldn't be used except to check for error conditions.
    MYFS_FILE_TYPE_FILE,            //!< Regular file.
    MYFS_FILE_TYPE_DIRECTORY,       //!< Directory.
    MYFS_FILE_TYPE_SOFT_LINK        //!< Symbolic or Soft Link.
} myfs_file_type_t;

/**
 *  Represents a file from the database.
 */
typedef struct myfs_file_t myfs_file_t;

struct myfs_file_t {
    unsigned int file_id;               //!< Unique File ID from the database.
    char name[64 + 1];                  //!< The basename of the file.
    myfs_file_type_t type;              //!< The type of file this is.
    struct stat st;                     //!< Linux's struct stat for this file.
    myfs_file_t *parent;                //!< The parent of this file or NULL if this file represents the root directory.
    myfs_file_t **children;             //!< The files in this directory or NULL if this file is not a directory.
    unsigned int children_count;        //!< The number of files in this directory.
};

/**
 * The MyFS context that will be available in FUSE callbacks.
 */
typedef struct {
    db_t db;
    myfs_file_t *files[MYFS_FILES_OPEN_MAX];
} myfs_t;

/**
 * Initializes a MyFS file.
 *
 * @param[in] file The MyFS file.
 */
void myfs_file_init(myfs_file_t *file);

/**
 * Frees a MyFS file. This also fees `file`, the pointer passed into the function.
 *
 * @param[in] file The MyFS file.
 */
void myfs_file_free(myfs_file_t *file);

/**
 * Returns the enum file type based on its string value.
 *
 * @param[in] type The enum file type as a string.
 * @return The enum file type.
 */
myfs_file_type_t myfs_file_type(const char *type);

/**
 * Returns the enum file type as a string.
 *
 * @param[in] type The enum file type.
 * @return The enum file type as a string.
 */
const char * myfs_file_type_str(myfs_file_type_t type);

/**
 * Connects MyFS to MariaDB.
 *
 * @param[in] myfs The MyFS context.
 * @return `true` on success, otherwise `false`.
 */
bool myfs_connect(myfs_t *myfs);

/**
 * Disconnects MyFS from MariaDB and cleans up.
 *
 * @param[in] myfs The MyFS context.
 */
void myfs_disconnect(myfs_t *myfs);

/**
 * FUSE callbacks below.
 */
int myfs_statfs(const char *path, struct statvfs *stv);
int myfs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi);
int myfs_access(const char *path, int mode);
int myfs_truncate(const char *path, off_t size, struct fuse_file_info *fi);
int myfs_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi);
int myfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi);
int myfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi);
int myfs_opendir(const char *path, struct fuse_file_info *fi);
int myfs_releasedir(const char *path, struct fuse_file_info *fi);
int myfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int myfs_unlink(const char *path);
int myfs_rmdir(const char *path);
int myfs_mkdir(const char *path, mode_t mode);
int myfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int myfs_flush(const char *path, struct fuse_file_info *fi);
int myfs_open(const char *path, struct fuse_file_info *fi);
int myfs_release(const char *path, struct fuse_file_info *fi);
int myfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi);
int myfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi);
int myfs_rename(const char *path_old, const char *path_new, unsigned int flags);
int myfs_symlink(const char *target, const char *path);
int myfs_readlink(const char *path, char *buf, size_t size);
