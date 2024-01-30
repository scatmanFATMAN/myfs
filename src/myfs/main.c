#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include "../common/log.h"
#include "../common/config.h"
#include "../common/string.h"
#include "../common/db.h"
#include "version.h"

#define MODULE "Main"

/** The maximum length a file name can be. */
#define MYFS_FILE_NAME_MAX_LEN 64

/** The maximum length a file path can be. */
#define MYFS_PATH_NAME_MAX_LEN 1024

/** The maximum number of open files. */
#define MYFS_FILES_OPEN_MAX 128

#define MYFS_TRACE
#if defined(MYFS_TRACE)
# define MYFS_LOG_TRACE(fmt, ...)                   \
        do {                                        \
            printf("[%s] ", __FUNCTION__);          \
            printf(fmt, ##__VA_ARGS__);             \
            printf("\n");                           \
            fflush(stdout);                         \
        } while(0)
#else
# define MYFS_LOG_TRACE(fmt, ...)
#endif

/**
 *  The possible types for files.
 */
typedef enum {
    MYFS_FILE_TYPE_INVALID,         //!< Default value and shouldn't be used except to check for error conditions.
    MYFS_FILE_TYPE_FILE,            //!< Regular file.
    MYFS_FILE_TYPE_DIRECTORY        //!< Directory.
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
 * Parses a path into its directory component and copies it into a buffer.
 *
 * @param[in] path The path to get the directory component of.
 * @param[out] dst The buffer to copy the directory component into.
 * @param[in[ size The size of `buffer`.
 * @return a pointer to `dst`.
 */
static const char *
myfs_dirname(const char *path, char *dst, size_t size) {
    char *path_dupe, *dir;

    //Duplicate path since dirname() modifies the argument
    path_dupe = strdup(path);

    //Return a pointer to the modified path (eg. a \0 is added at the last '/').
    dir = dirname(path_dupe);

    //Copy into the return buffer.
    strlcpy(dst, dir, size);

    //Free the duplicated path.
    free(path_dupe);

    return dst;
}

/**
 * Parses a path into its file name component and copies it into a buffer.
 *
 * @param[in] path The path to get the file name component of.
 * @param[out] dst The buffer to copy the file name component into.
 * @param[in[ size The size of `buffer`.
 * @return a pointer to `dst`.
 */
static const char *
myfs_basename(const char *path, char *dst, size_t size) {
    char *path_dupe, *name;

    //Duplicate path since banename() modifies the argument
    path_dupe = strdup(path);

    //Return a pointer to the modified name.
    name = basename(path_dupe);

    //Copy into the return buffer.
    strlcpy(dst, name, size);

    //Free the duplicated path.
    free(path_dupe);

    return dst;
}

static void
config_error(const char *message) {
    log_err(MODULE, "%s", message);
}

/**
 * Returns the enum file type based on its string value.
 *
 * @param[in] type The enum file type as a string.
 * @return The enum file type.
 */
myfs_file_type_t
myfs_file_type(const char *type) {
    if (strcmp(type, "File") == 0) {
        return MYFS_FILE_TYPE_FILE;
    }

    if (strcmp(type, "Directory") == 0) {
        return MYFS_FILE_TYPE_DIRECTORY;
    }

    return MYFS_FILE_TYPE_INVALID;
}

/**
 * Returns the enum file type as a string.
 *
 * @param[in] The enum file type.
 * @return The enum file type as a string.
 */
static const char *
myfs_file_type_str(myfs_file_type_t type) {
    switch (type) {
        case MYFS_FILE_TYPE_FILE:
            return "File";
        case MYFS_FILE_TYPE_DIRECTORY:
            return "Directory";
        case MYFS_FILE_TYPE_INVALID:
            break;
    }

    return "Invalid";
}

/**
 * Initializes a MyFS file.
 *
 * @param[in] file The MyFS file.
 */
static void
myfs_file_init(myfs_file_t *file) {
    memset(file, 0, sizeof(*file));
}

/**
 * Frees a MyFS file.
 *
 * @param[in] file The MyFS file.
 */
static void
myfs_file_free(myfs_file_t *file) {
    unsigned int i;

    if (file->parent != NULL) {
        free(file->parent);
    }

    if (file->children != NULL) {
        for (i = 0; i < file->children_count; i++) {
            free(file->children[i]);
        }

        free(file->children);
    }
}

/**
 * Connects MyFS to MariaDB.
 *
 * @param[in] myfs The MyFS context.
 * @return `true` on success, otherwise `false`.
 */
static bool
myfs_connect(myfs_t *myfs) {
    bool success;

    success = db_connect(&myfs->db, config_get("mariadb_host"), config_get("mariadb_user"), config_get("mariadb_password"), config_get("mariadb_database"),config_get_uint("mariadb_port"));

    if (!success) {
        log_err(MODULE, "Error connecting to MariaDB: %s", db_error(&myfs->db));
    }

    return success;
}

/**
 * Disconnects MyFS from MariaDB.
 *
 * @param[in] myfs The MyFS context.
 */
static void
myfs_disconnect(myfs_t *myfs) {
    db_disconnect(&myfs->db);
}

/**
 * Inserts a new file record into MariaDB with the given file type and parent.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] name The name of the file.
 * @param[in] type The file type.
 * @param[in] parent_id The File ID of the parent the file should be created in.
 * @return `true` if the file was created, otherwise `false`.
 */
static bool
myfs_file_create(myfs_t *myfs, const char *name, myfs_file_type_t type, unsigned int parent_id) {
    char *name_esc, content[8];
    bool success;

    name_esc = db_escape(&myfs->db, name);

    //Files get a blank string while directories and other file types get NULL
    if (type == MYFS_FILE_TYPE_FILE) {
        strcpy(content, "''");
    }
    else {
        strcpy(content, "NULL");
    }

    success = db_queryf(&myfs->db, "INSERT INTO `files` (`parent_id`,`name`,`type`,`content`,`created_on`,`last_accessed_on`,`last_modified_on`,`last_status_changed_on`)\n"
                                   "VALUES (%u,'%s',%u,%s,UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP())",
                                   parent_id, name_esc, type, content);

    free(name_esc);

    if (!success) {
        log_err(MODULE, "Error creating file '%s' with Parent ID %u: %s", name, parent_id, db_error(&myfs->db));
    }

    return success;
}

/**
 * Deletes a file from MariaDB. If this file is a parent to other files, all children will
 * be deleted in a cascading fashion.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID of the file to delete.
 * @return `true` if the file was deleted, otherwise `false`.
 */
static bool
myfs_file_delete(myfs_t *myfs, unsigned int file_id) {
    bool success;

    //TODO: Support soft delete?

    success = db_queryf(&myfs->db, "DELETE FROM `files`\n"
                                   "WHERE `file_id`=%u",
                                   file_id);

    if (!success) {
        log_err(MODULE, "Error deleting File ID %u: %s", file_id, db_error(&myfs->db));
    }

    return success;
}

/**
 * Update the last accessed and last modified timestamps of the given File ID.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID to update.
 * @param[in] last_accessed_on The last accessed timestamp to set.
 * @param[in] last_modified_on The last modified timestamp to set.
 * @return `true` if the file was updated, otherwise `false`.
 */
static bool
myfs_file_update_times(myfs_t *myfs, unsigned int file_id, time_t last_accessed_on, time_t last_modified_on) {
    bool success;

    success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                   "SET `last_accessed_on`=%ld,`last_modified_on`=%ld\n"
                                   "WHERE `file_id`=%u",
                                   last_accessed_on, last_modified_on,
                                   file_id);

    if (!success) {
        log_err(MODULE, "Error updating times for File ID %u: %s", file_id, db_error(&myfs->db));
    }

    return success;
}

/**
 * Sets the size of the content. This is going to be interesting functionality in a database.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID to update.
 * @param[in] size The size to set the content to.
 * @return `true` if the file was updated, otherwise `false`.
 */
static bool
myfs_file_set_content_size(myfs_t *myfs, unsigned int file_id, off_t size) {
    MYSQL_RES *res;
    MYSQL_ROW row;
    off_t current_size = -1, diff;
    bool success;

    //First, get the current size of the content so we know if we need to shrink, grow, or do nothing.
    res = db_selectf(&myfs->db, "SELECT IFNULL(LENGTH(`content`),0)\n"
                                "FROM `files`\n"
                                "WHERE `file_id`=%u",
                                file_id);

    if (res == NULL) {
        log_err(MODULE, "Error truncating File ID %u: %s", file_id, db_error(&myfs->db));
        return false;
    }

    row = mysql_fetch_row(res);
    if (row != NULL) {
        current_size = strtoul(row[0], NULL, 10);
    }

    mysql_free_result(res);

    if (current_size == -1) {
        log_err(MODULE, "Error truncating File ID %u: Not found", file_id);
        return false;
    }

    diff = size - current_size;

    //Shrink or boner?
    if (diff > 0) {
        //Simply add blanks onto the end
        success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                       "SET `content`=CONCAT(IFNULL(`content`,''),REPEAT(' ',%zd))\n"
                                       "WHERE `file_id`=%u",
                                       diff,
                                       file_id);

        if (!success) {
            log_err(MODULE, "Error truncating File ID %u: %s", file_id, db_error(&myfs->db));
            return false;
        }
    }
    else if (diff < 0) {
        success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                       "SET `content`=SUBSTRING(IFNULL(`content`,''),0,%zd)\n"
                                       "WHERE `file_id`=%u",
                                       size,
                                       file_id);

        if (!success) {
            log_err(MODULE, "Error truncating File ID %u: %s", file_id, db_error(&myfs->db));
            return false;
        }
    }

    return true;
}

static myfs_file_t * myfs_file_query(myfs_t *myfs, unsigned int file_id, bool include_children);

/**
 * Queries MariaDB for a MyFS's file's children. The file must be a MYFS_FILE_TYPE_DIRECTORY.
 *
 * @param[in] myfs The MyFS context.
 * @param[in,out] file The MyFS file to get children for.
 */
static void
myfs_file_query_children(myfs_t *myfs, myfs_file_t *file) {
    unsigned int i = 0;
    MYSQL_RES *res;
    MYSQL_ROW row;

    MYFS_LOG_TRACE("Begin; FileID[%u]; Name[%s]", file->file_id, file->name);

    if (file->type != MYFS_FILE_TYPE_DIRECTORY) {
        log_err(MODULE, "Error getting children for file '%s': Not a directory", file->name);
        return;
    }

    res = db_selectf(&myfs->db, "SELECT `file_id`\n"
                                "FROM `files`\n"
                                "WHERE `parent_id`=%u\n"
                                "AND `file_id`!=0\n"
                                "ORDER BY `name` ASC",
                                file->file_id);

    file->children_count = mysql_num_rows(res);
    file->children = calloc(file->children_count, sizeof(myfs_file_t));

    MYFS_LOG_TRACE("Children[%u]", file->children_count);

    while ((row = mysql_fetch_row(res)) != NULL) {
        file->children[i++] = myfs_file_query(myfs, strtoul(row[0], NULL, 10), false);
    }

    mysql_free_result(res);

    MYFS_LOG_TRACE("End");
}

/**
 * Queries MariaDB for a MyFS file's data, including its parent and possibly its children. If querying for
 * the file's children, it must be a MYSYS_FILE_TYPE_DIRECTORY.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] file_id The File ID of the MyFS file.
 * @param[in] include_children `true` to also query for the MyFS's children.
 * @return The MyFS file or `NULL` if an error occurred.
 */
static myfs_file_t *
myfs_file_query(myfs_t *myfs, unsigned int file_id, bool include_children) {
    myfs_file_t *file = NULL;
    MYSQL_RES *res;
    MYSQL_ROW row;

    MYFS_LOG_TRACE("Begin; FileID[%u]; IncludeChildren[%s]", file_id, include_children ? "Yes" : "No");

    res = db_selectf(&myfs->db, "SELECT `file_id`,`name`,`parent_id`,`type`,`last_accessed_on`,`last_modified_on`,`last_status_changed_on`,IFNULL(LENGTH(`content`),0)\n"
                                "FROM `files`\n"
                                "WHERE `file_id`=%u",
                                file_id);

    if (res == NULL) {
        log_err(MODULE, "Error getting file with File ID %u: %s", file_id, db_error(&myfs->db));
        return NULL;
    }

    row = mysql_fetch_row(res);
    if (row == NULL) {
        log_err(MODULE, "Error getting file with File ID %u: Not found", file_id);
    }
    else {
        file = malloc(sizeof(*file));
        myfs_file_init(file);

        file->file_id = strtoul(row[0], NULL, 10);
        strlcpy(file->name, row[1], sizeof(file->name));
        if (file_id > 0) {
            //Only grab the parent if this file is not the root.
            file->parent = myfs_file_query(myfs, strtoul(row[2], NULL, 10), false);
        }
        file->type = myfs_file_type(row[3]);

        //Setup the struct stat.
        switch (file->type) {
            case MYFS_FILE_TYPE_FILE:
                file->st.st_mode = S_IFREG | 0600;
                file->st.st_nlink = 1;
                file->st.st_size = strtoul(row[7], NULL, 10);
                break;
            case MYFS_FILE_TYPE_DIRECTORY:
                file->st.st_mode = S_IFDIR | 0700;
                file->st.st_nlink = 2;
                break;
            case MYFS_FILE_TYPE_INVALID:
                break;
        }
        file->st.st_uid = getuid();
        file->st.st_gid = getgid();
        file->st.st_atime = strtoll(row[4], NULL, 10);
        file->st.st_mtime = strtoll(row[5], NULL, 10);
        file->st.st_ctime = strtoll(row[6], NULL, 10);
    }

    mysql_free_result(res);

    if (file != NULL && include_children) {
        myfs_file_query_children(myfs, file);
    }

    MYFS_LOG_TRACE("End");

    return file;

}

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
static myfs_file_t *
myfs_file_query_name(myfs_t *myfs, const char *name, unsigned int parent_id, bool include_children) {
    myfs_file_t *file = NULL;
    MYSQL_RES *res;
    MYSQL_ROW row;
    char *name_esc = NULL;

    MYFS_LOG_TRACE("Begin; Name[%s]; ParentID[%u]; IncludeChildren[%s]", name, parent_id, include_children ? "Yes" : "No");

    //Escape the name if needed.
    if (name != NULL && name[0] != '\0') {
        name_esc = db_escape(&myfs->db, name);
    }

    res = db_selectf(&myfs->db, "SELECT `file_id`\n"
                                "FROM `files`\n"
                                "WHERE `parent_id`=%u\n"
                                "AND `name`='%s'",
                                parent_id,
                                name_esc == NULL ? "" : name_esc);

    if (name_esc != NULL) {
        free(name_esc);
    }

    if (res == NULL) {
        log_err(MODULE, "Error getting file '%s' with parent id %u: %s", name, parent_id, db_error(&myfs->db));
        return NULL;
    }

    row = mysql_fetch_row(res);

    //Don't output an error if the file doesn't exist. FUSE will try to stat() files to see if they exist before making other calls.
    if (row != NULL) {
        file = myfs_file_query(myfs, strtoul(row[0], NULL, 10), include_children);
    }

    mysql_free_result(res);

    MYFS_LOG_TRACE("End");

    return file;
}

/**
 * Queries MariaDB for MyFS file based on a full file path. For example, if `path` is /path/to/file, then
 * the MyFS represnted by the name 'file' with parent 'to' will be returned.
 *
 * @param[in] myfs The MyFS context.
 * @param[in] path The path to the MyFS file.
 * @param[in] include_children `true` to also query for the MyFS's children.
 * @return The MyFS file or `NULL` if an error occurred.
 */
static myfs_file_t *
myfs_file_get(myfs_t *myfs, const char *path, bool include_children) {
    char *path_dupe, *name, *save;
    unsigned int parent_id;
    myfs_file_t *file = NULL;

    MYFS_LOG_TRACE("Begin; Path[%s]; IncludeChildren[%s]", path, include_children ? "Yes" : "No");

    //skip the first /
    path_dupe = strdup(path + 1);

    //Get the root folder.
    file = myfs_file_query_name(myfs, NULL, 0, include_children);

    //Loop through each name part and get the child until we get to the last one.
    name = strtok_r(path_dupe, "/", &save);
    while (name != NULL) {
        parent_id = file->file_id;
        myfs_file_free(file);

        file = myfs_file_query_name(myfs, name, parent_id, include_children);
        name = strtok_r(NULL, "/", &save);
    }

    free(path_dupe);

    MYFS_LOG_TRACE("End");

    return file;
}

/******************************************************************************************************
 *                  FUSE CALLBACKS
 *****************************************************************************************************/

static int
myfs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]", path);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    file = myfs_file_get(myfs, path, false);
    if (file == NULL) {
        return -ENOENT;
    }

    //Simply copy the file's struct stat into the output buffer.
    memcpy(st, &file->st, sizeof(*st));
    myfs_file_free(file);

    MYFS_LOG_TRACE("End");

    return 0;
}

static int
myfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]; Size[%zu]", path, size);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Get the file from the open file table
    file = myfs->files[fi->fh];

    success = myfs_file_set_content_size(myfs, file->file_id, size);
    if (!success) {
        //Not really sure what to return here. If this doesn't succeed, it means the MariaDB query failed.
        return -EINVAL;
    }

    MYFS_LOG_TRACE("End");
    return 0;
}

static int
myfs_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]; atime[%ld]; mtime[%ld]", path, ts[0].tv_sec, ts[1].tv_sec);

    //TODO: file info always seems to be NULL? The file should be open though

    if (fi == NULL) {
        MYFS_LOG_TRACE("FileInfo is NULL");
    }
    else {
        myfs = (myfs_t *)fuse_get_context()->private_data;

        //Get the file from the open file table
        file = myfs->files[fi->fh];

        success = myfs_file_update_times(myfs, file->file_id, ts[0].tv_sec, ts[1].tv_sec);
        if (!success) {
            //Not really sure what to return here. If this doesn't succeed, it means the MariaDB query failed.
            return -EINVAL;
        }
    }

    MYFS_LOG_TRACE("End");
    return 0;
}

static int
myfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
    MYFS_LOG_TRACE("Begin; Path[%s]", path);

    //This callback needs to be implemented for FUSE but MyFS doesn't need it since all user/groups are inherited by the user/group running the file system.

    MYFS_LOG_TRACE("End");

    return 0;
}

static int
myfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    myfs_file_t *file;
    myfs_t *myfs;
    unsigned int i;

    MYFS_LOG_TRACE("Begin; Path[%s]; Offset[%zu]", path, offset);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //TODO: Implement opendir so we can use fi->fh instead of finding the file again
    file = myfs_file_get(myfs, path, true);
    if (file == NULL) {
        return -ENOENT;
    }

    //Always at the current and previous directory special files.
    filler(buffer, ".", NULL, 0, 0);
    filler(buffer, "..", NULL, 0, 0);

    //Add the files in the directory.
    for (i = 0; i < file->children_count; i++) {
        MYFS_LOG_TRACE("Adding [%s]", file->children[i]->name);
        filler(buffer, file->children[i]->name, NULL, 0, 0);
    }

    myfs_file_free(file);

    MYFS_LOG_TRACE("End");

    return 0;
}

static int
myfs_unlink(const char *path) {
    myfs_file_t *file;
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]", path);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    file = myfs_file_get(myfs, path, true);
    if (file == NULL) {
        return -ENOENT;
    }

    //Delete the file from MariaDB.
    //FUSE does the check already to see if the file being deleted is a regular file.
    success = myfs_file_delete(myfs, file->file_id);
    myfs_file_free(file);

    if (!success) {
        //Not really sure what to return here. If this doesn't succeed, it means the MariaDB query failed.
        return -EINVAL;
    }

    MYFS_LOG_TRACE("End");

    return 0;
}

static int
myfs_rmdir(const char *path) {
    myfs_file_t *file;
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]", path);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    file = myfs_file_get(myfs, path, true);
    if (file == NULL) {
        return -ENOENT;
    }

    if (file->children_count > 0) {
        //The directory is not empty. TODO: possibly allow recursive delete through configuration?
        return -ENOTEMPTY;
    }

    //Delete the directory from MariaDB.
    success = myfs_file_delete(myfs, file->file_id);
    myfs_file_free(file);

    if (!success) {
        //Not really sure what to return here. If this doesn't succeed, it means the MariaDB query failed.
        return -EINVAL;
    }

    MYFS_LOG_TRACE("End");
    return 0;
}

static int
myfs_mkdir(const char *path, mode_t mode) {
    char dir[MYFS_PATH_NAME_MAX_LEN + 1];
    char name[MYFS_FILE_NAME_MAX_LEN + 1];
    bool success;
    myfs_file_t *parent;
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]", path);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Get the path components.
    myfs_dirname(path, dir, sizeof(dir));
    myfs_basename(path, name, sizeof(name));

    MYFS_LOG_TRACE("Creating folder '%s' in '%s'", name, dir);

    //Get the MyFS file that represents the parent folder.
    parent = myfs_file_get(myfs, dir, false);
    if (parent == NULL) {
        return -ENOENT;
    }

    //Create the directory in MariaDB.
    success = myfs_file_create(myfs, name, MYFS_FILE_TYPE_DIRECTORY, parent->file_id);
    myfs_file_free(parent);

    if (!success) {
        //Not really sure what to return here. If this doesn't succeed, it means the MariaDB query failed.
        return -EINVAL;
    }

    MYFS_LOG_TRACE("End");

    return 0;
}

//TODO: This also is supposed to open the file once it's created so we can call most of myfs_open() somehow 
static int
myfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char dir[MYFS_PATH_NAME_MAX_LEN + 1];
    char name[MYFS_FILE_NAME_MAX_LEN + 1];
    bool success;
    uint64_t fh = 0;
    myfs_file_t *parent, *file;
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]", path);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Get the path components.
    myfs_dirname(path, dir, sizeof(dir));
    myfs_basename(path, name, sizeof(name));

    MYFS_LOG_TRACE("Creating file '%s' in '%s'", name, dir);

    //Get the MyFS file that represents the parent folder.
    parent = myfs_file_get(myfs, dir, false);
    if (parent == NULL) {
        return -ENOENT;
    }

    //Create the file in MariaDB.
    success = myfs_file_create(myfs, name, MYFS_FILE_TYPE_FILE, parent->file_id);
    myfs_file_free(parent);

    if (!success) {
        //Not really sure what to return here. If this doesn't succeed, it means the MariaDB query failed.
        return -EINVAL;
    }

    //Look for the first available file handle.
    for (fh = 0; fh < MYFS_FILES_OPEN_MAX; fh++) {
        if (myfs->files[fh] == NULL) {
            break;
        }
    }

    if (fh == MYFS_FILES_OPEN_MAX) {
        log_err(MODULE, "Error opening file '%s': Maximum number of files are open", path);
        return -EMFILE;
    }

    MYFS_LOG_TRACE("Got FileHandle[%zu]", fh);

    file = myfs_file_get(myfs, path, false);
    if (file == NULL) {
        return -ENOENT;
    }

    //Put the file into the open files table
    myfs->files[fh] = file;

    //Put the file handle into Fuse's file info struct so we can get it in other file operations
    fi->fh = fh;

    MYFS_LOG_TRACE("End");

    return 0;
}

static int
myfs_flush(const char *path, struct fuse_file_info *fi) {
    MYFS_LOG_TRACE("Begin; Path[%s]", path);
    MYFS_LOG_TRACE("End");

    return 0;
}

static int
myfs_open(const char *path, struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;
    uint64_t fh = 0;

    MYFS_LOG_TRACE("Begin; Path[%s]", path);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Look for the first available file handle.
    for (fh = 0; fh < MYFS_FILES_OPEN_MAX; fh++) {
        if (myfs->files[fh] == NULL) {
            break;
        }
    }

    if (fh == MYFS_FILES_OPEN_MAX) {
        log_err(MODULE, "Error opening file '%s': Maximum number of files are open", path);
        return -EMFILE;
    }

    MYFS_LOG_TRACE("Got FileHandle[%zu]", fh);

    file = myfs_file_get(myfs, path, false);
    if (file == NULL) {
        return -ENOENT;
    }

    //Put the file into the open files table
    myfs->files[fh] = file;

    //Put the file handle into Fuse's file info struct so we can get it in other file operations
    fi->fh = fh;

    MYFS_LOG_TRACE("End");

    return 0;
}

static int
myfs_release(const char *path, struct fuse_file_info *fi) {
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]; FileHandle[%zu]", path, fi->fh);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Free the file and make the file handle available again.
    myfs_file_free(myfs->files[fi->fh]);
    myfs->files[fi->fh] = NULL;

    MYFS_LOG_TRACE("End");

    return 0;
}

static int
myfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    MYSQL_RES *res;
    MYSQL_ROW row;
    myfs_file_t *file;
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]; Size[%zu]; Offset[%zu]; FileHandle[%zu]", path, size, offset, fi->fh);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Get the file from the open file table
    file = myfs->files[fi->fh];

    //`size` is usually 4kb so handle partial reads.
    //Also handle muliple reads if the file is bigger than 4k (eg. `offset` > 0)
    if (offset + size > file->st.st_size) {
        size = file->st.st_size - offset;
        MYFS_LOG_TRACE("New Size[%zu]", size);
    }

    //TODO: How to handle when there are multiple calls to read(). Do we need to look up the file each time or only once? Maybe using open()?
    //MariaDB SUBSTRING() indexes are 1 based.
    res = db_selectf(&myfs->db, "SELECT SUBSTRING(`content`,%zd,%zu)\n"
                                "FROM `files`\n"
                                "WHERE `file_id`=%u",
                                offset + 1, size,
                                file->file_id);

    if (res == NULL) {
        log_err(MODULE, "Error reading file '%s': %s", file->name, db_error(&myfs->db));
        myfs_file_free(file);
        return -ENOENT;
    }

    //Copy the MariaDB data into the output buffer
    row = mysql_fetch_row(res);
    memcpy(buffer, row[0], size);

    mysql_free_result(res);

    MYFS_LOG_TRACE("End");

    return size;
}

int
main(int argc, char **argv) {
    struct fuse_operations operations;
    int ret = 0;
    myfs_t myfs;

    log_init();
    config_init();
    mysql_library_init(0, NULL, NULL);

    memset(&myfs, 0, sizeof(myfs));

    config_set_error_func(config_error);

    //Set default config options.
    config_set_default("mariadb_database", "--mariadb_database", "mariadb_database", "myfs",      "The MariaDB database name.");
    config_set_default("mariadb_host",     "--mariadb_host",     "mariadb_host",     "127.0.0.1", "The MariaDB IP address or hostname.");
    config_set_default("mariadb_password", "--mariadb_password", "mariadb_password", NULL,        "The MariaDB user's password.");
    config_set_default("mariadb_port",     "--mariadb_port",     "mariadb_port",     "3306",      "The MariaDB port.");
    config_set_default("mariadb_user",     "--mariadb_user",     "mariadb_user",     "myfs",      "The MariaDB user.");
    config_set_default("mount",            "--mount",            "mount",            "/mnt/myfs", "The mount point for the file system.");

    if (!config_read(argc, argv, "/etc/myfs.d/myfs.conf")) {
        ret = 1;
        goto done;
    }

    log_info(MODULE, "Starting v%d.%.d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    if (ret == 0) {
        if (!myfs_connect(&myfs)) {
            ret = 2;
            goto done;
        }
    }

    if (ret == 0) {
        memset(&operations, 0, sizeof(operations));
        //operations.init = myfs_init;
        //operations.destroy = myfs_destroy;
        operations.getattr = myfs_getattr;
        //setxattr
        operations.truncate = myfs_truncate;
        operations.utimens = myfs_utimens;
        operations.chown = myfs_chown;
        operations.readdir = myfs_readdir;
        operations.unlink = myfs_unlink;
        operations.rmdir = myfs_rmdir;
        operations.mkdir = myfs_mkdir;
        operations.create = myfs_create;
        operations.flush = myfs_flush;
        operations.open = myfs_open;
        operations.release = myfs_release;
        operations.read = myfs_read;

        ret = fuse_main(argc, argv, &operations, &myfs);
    }

done:
    myfs_disconnect(&myfs);

    mysql_library_end();
    config_free();
    log_free();

    return ret;
}
