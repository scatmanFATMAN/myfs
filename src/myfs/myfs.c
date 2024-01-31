#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>
#include "../common/log.h"
#include "../common/config.h"
#include "../common/string.h"
#include "myfs_db.h"
#include "myfs.h"

#define MODULE "MyFS"

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

void
myfs_file_init(myfs_file_t *file) {
    memset(file, 0, sizeof(*file));
}

void
myfs_file_free(myfs_file_t *file) {
    unsigned int i;

    if (file->parent != NULL) {
        myfs_file_free(file->parent);
    }

    if (file->children != NULL) {
        for (i = 0; i < file->children_count; i++) {
            myfs_file_free(file->children[i]);
        }

        free(file->children);
    }

    free(file);
}

myfs_file_type_t
myfs_file_type(const char *type) {
    if (strcmp(type, "File") == 0) {
        return MYFS_FILE_TYPE_FILE;
    }

    if (strcmp(type, "Directory") == 0) {
        return MYFS_FILE_TYPE_DIRECTORY;
    }

    if (strcmp(type, "Soft Link") == 0) {
        return MYFS_FILE_TYPE_SOFT_LINK;
    }

    return MYFS_FILE_TYPE_INVALID;
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
    file = myfs_db_file_query_name(myfs, NULL, 0, include_children);

    //Loop through each name part and get the child until we get to the last one.
    name = strtok_r(path_dupe, "/", &save);
    while (name != NULL) {
        parent_id = file->file_id;
        myfs_file_free(file);

        file = myfs_db_file_query_name(myfs, name, parent_id, include_children);
        name = strtok_r(NULL, "/", &save);
    }

    free(path_dupe);

    MYFS_LOG_TRACE("End");

    return file;
}

bool
myfs_connect(myfs_t *myfs) {
    bool success;

    success = db_connect(&myfs->db, config_get("mariadb_host"), config_get("mariadb_user"), config_get("mariadb_password"), config_get("mariadb_database"),config_get_uint("mariadb_port"));

    if (!success) {
        log_err(MODULE, "Error connecting to MariaDB: %s", db_error(&myfs->db));
    }

    return success;
}

void
myfs_disconnect(myfs_t *myfs) {
    uint64_t i;

    db_disconnect(&myfs->db);

    for (i = 0; i < MYFS_FILES_OPEN_MAX; i++) {
        if (myfs->files[i] != NULL) {
            myfs_file_free(myfs->files[i]);
        }
    }
}

/******************************************************************************************************
 *                  FUSE CALLBACK COMMON FUNCTIONS
 *****************************************************************************************************/

int
myfs_open_helper(const char *path, bool dir, bool truncate, struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;
    bool success;
    uint64_t fh = 0;

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

    //If a directory is being opened, get the children
    file = myfs_file_get(myfs, path, dir ? true : false);
    if (file == NULL) {
        return -ENOENT;
    }

    //If a file is being opened, truncate if asked.
    if (!dir && truncate) {
        success = myfs_db_file_set_content_size(myfs, file->file_id, 0);
        if (!success) {
            myfs_file_free(file);
            return -EIO;
        }

        file->st.st_size = 0;
    }

    //Put the file into the open files table
    myfs->files[fh] = file;

    //Put the file handle into Fuse's file info struct so we can get it in other file operations
    fi->fh = fh;

    return 0;
}

int
myfs_release_helper(const char *path, struct fuse_file_info *fi) {
    myfs_t *myfs;

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Free the file and make the file handle available again.
    myfs_file_free(myfs->files[fi->fh]);
    myfs->files[fi->fh] = NULL;

    return 0;
}

/******************************************************************************************************
 *                  FUSE CALLBACKS
 *****************************************************************************************************/

int
myfs_statfs(const char *path, struct statvfs *stv) {
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]", path);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    memset(stv, 0, sizeof(*stv));
    stv->f_bsize = 1;
    stv->f_frsize = 1;
    stv->f_namemax = MYFS_FILE_NAME_MAX_LEN;

    success = myfs_db_get_num_files(myfs, &stv->f_files) &&
              myfs_db_get_space_used(myfs, &stv->f_blocks);

    if (!success) {
        return -EIO;
    }

    MYFS_LOG_TRACE("End");

    return 0;
}

int
myfs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]; FI[%p]", path, fi);

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

int
myfs_access(const char *path, int mode) {
    myfs_file_t *file;
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]; Mode[%d]", path, mode);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    file = myfs_file_get(myfs, path, false);
    if (file == NULL) {
        return -ENOENT;
    }

    //TODO: Check permissions if we implement them.
    myfs_file_free(file);

    MYFS_LOG_TRACE("End");

    return 0;

}

int
myfs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]; Size[%zu]; FI[%p]", path, size, fi);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Get the file from the open file table
    file = myfs->files[fi->fh];

    success = myfs_db_file_set_content_size(myfs, file->file_id, size);
    if (!success) {
        return -EIO;
    }

    MYFS_LOG_TRACE("End");
    return 0;
}

int
myfs_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]; atime[%ld]; mtime[%ld]; FI[%p]", path, ts[0].tv_sec, ts[1].tv_sec, fi);

    //TODO: file info always seems to be NULL? The file should be open though

    if (fi == NULL) {
        fprintf(stderr, "BLAH\n");
        exit(1);
    }
    else {
        myfs = (myfs_t *)fuse_get_context()->private_data;

        //Get the file from the open file table
        file = myfs->files[fi->fh];

        success = myfs_db_file_set_times(myfs, file->file_id, ts[0].tv_sec, ts[1].tv_sec);
        if (!success) {
            return -EIO;
        }
    }

    MYFS_LOG_TRACE("End");
    return 0;
}

int
myfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
    MYFS_LOG_TRACE("Begin; Path[%s]; UID[%d]; GID[%d]; FI[%p]", path, uid, gid, fi);

    //This callback needs to be implemented for FUSE but MyFS doesn't need it since all user/groups are inherited by the user/group running the file system.

    MYFS_LOG_TRACE("End");

    return 0;
}

int
myfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
    MYFS_LOG_TRACE("Begin; Path[%s]; FI[%p]", path, fi);

    MYFS_LOG_TRACE("End");

    return 0;
}

int
myfs_opendir(const char *path, struct fuse_file_info *fi) {
    int ret;

    MYFS_LOG_TRACE("Begin; Path[%s]; FI[%p]", path, fi);

    ret = myfs_open_helper(path, true, false, fi);

    MYFS_LOG_TRACE("End");

    return ret;
}

int
myfs_releasedir(const char *path, struct fuse_file_info *fi) {
    int ret;

    MYFS_LOG_TRACE("Begin; Path[%s]; FileHandle[%zu]; FI[%p]", path, fi->fh, fi);

    ret = myfs_release_helper(path, fi);

    MYFS_LOG_TRACE("End");

    return ret;
}

int
myfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    myfs_file_t *file;
    myfs_t *myfs;
    unsigned int i;

    MYFS_LOG_TRACE("Begin; Path[%s]; Offset[%zu]; FI[%p]", path, offset, fi);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    file = myfs->files[fi->fh];

    //Always at the current and previous directory special files.
    filler(buffer, ".", NULL, 0, 0);
    filler(buffer, "..", NULL, 0, 0);

    //Add the files in the directory.
    for (i = 0; i < file->children_count; i++) {
        MYFS_LOG_TRACE("Adding [%s]", file->children[i]->name);
        filler(buffer, file->children[i]->name, NULL, 0, 0);
    }

    MYFS_LOG_TRACE("End");

    return 0;
}

int
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
    success = myfs_db_file_delete(myfs, file->file_id);
    myfs_file_free(file);

    if (!success) {
        return -EIO;
    }

    MYFS_LOG_TRACE("End");

    return 0;
}

int
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
    success = myfs_db_file_delete(myfs, file->file_id);
    myfs_file_free(file);

    if (!success) {
        return -EIO;
    }

    MYFS_LOG_TRACE("End");
    return 0;
}

int
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
    success = myfs_db_file_create(myfs, name, MYFS_FILE_TYPE_DIRECTORY, parent->file_id, NULL);
    myfs_file_free(parent);

    if (!success) {
        return -EIO;
    }

    MYFS_LOG_TRACE("End");

    return 0;
}

//TODO: This also is supposed to open the file once it's created so we can call most of myfs_open() somehow 
int
myfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char dir[MYFS_PATH_NAME_MAX_LEN + 1];
    char name[MYFS_FILE_NAME_MAX_LEN + 1];
    bool success;
    uint64_t fh = 0;
    myfs_file_t *parent, *file;
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]; FI[%p]", path, fi);

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
    success = myfs_db_file_create(myfs, name, MYFS_FILE_TYPE_FILE, parent->file_id, NULL);
    myfs_file_free(parent);

    if (!success) {
        return -EIO;
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

int
myfs_flush(const char *path, struct fuse_file_info *fi) {
    MYFS_LOG_TRACE("Begin; Path[%s]; FI[%p]", path, fi);
    MYFS_LOG_TRACE("End");

    return 0;
}

int
myfs_open(const char *path, struct fuse_file_info *fi) {
    int ret;

    MYFS_LOG_TRACE("Begin; Path[%s]; Truncate[%s]; FI[%p]", path, fi->flags & O_TRUNC ? "Yes" : "No", fi);

    ret = myfs_open_helper(path, false, fi->flags & O_TRUNC, fi);

    MYFS_LOG_TRACE("End");

    return ret;
}

int
myfs_release(const char *path, struct fuse_file_info *fi) {
    int ret;

    MYFS_LOG_TRACE("Begin; Path[%s]; FileHandle[%zu]; FI[%p]", path, fi->fh, fi);

    ret = myfs_release_helper(path, fi);

    MYFS_LOG_TRACE("End");

    return ret;
}

int
myfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    MYSQL_RES *res;
    MYSQL_ROW row;
    myfs_file_t *file;
    myfs_t *myfs;

    MYFS_LOG_TRACE("Begin; Path[%s]; Size[%zu]; Offset[%zu]; FileHandle[%zu]; FI[%p]", path, size, offset, fi->fh, fi);

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
    //TODO: put queries in their own functions at the top with the others?
    //MariaDB SUBSTRING() indexes are 1 based.
    res = db_selectf(&myfs->db, "SELECT SUBSTRING(`content`,%zd,%zu)\n"
                                "FROM `files`\n"
                                "WHERE `file_id`=%u",
                                offset + 1, size,
                                file->file_id);

    if (res == NULL) {
        log_err(MODULE, "Error reading file '%s': %s", file->name, db_error(&myfs->db));
        return -EIO;
    }

    //Copy the MariaDB data into the output buffer
    row = mysql_fetch_row(res);
    memcpy(buffer, row[0], size);

    mysql_free_result(res);

    MYFS_LOG_TRACE("End");

    return size;
}

int
myfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    myfs_file_t *file;
    myfs_t *myfs;
    unsigned int buffer_esc_len;
    char *buffer_esc;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]; Size[%zu]; Offset[%zu]; FileHandle[%zu]; Append[%s]; FI[%p]", path, size, offset, fi->fh, fi->flags & O_APPEND ? "Yes" : "No", fi);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Get the file from the open file table
    file = myfs->files[fi->fh];

    //Escape the buffer for MySQL.
    buffer_esc = db_escape(&myfs->db, buffer, &buffer_esc_len);

    //Update the file's content
    //TODO: put queries in their own functions at the top with the others?
    if (fi->flags & O_APPEND || file->st.st_size == offset) {
        //When O_APPEND is given or , file content is always written to the end, no matter what the offset is.
        //I don't actually think O_APPEND needs to be checked anymore. It appears FUSE will position the offset at the end
        //of the file.
        //So we need to see if the offset given by FUSE is the size of the file, if so then use MariaDB's CONCAT
        success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                       "SET `content`=CONCAT(`content`,'%s')\n"
                                       "WHERE `file_id`=%u",
                                       buffer_esc,
                                       file->file_id);
    }
    else {
        success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                       "SET `content`=INSERT(`content`,%zd,%zu,'%s')\n"
                                       "WHERE `file_id`=%u",
                                       offset + 1, buffer_esc_len, buffer_esc,
                                       file->file_id);
    }

    free(buffer_esc);

    if (!success) {
        log_err(MODULE, "Error writing to file '%s': %s", file->name, db_error(&myfs->db));
        return -EIO;
    }

    MYFS_LOG_TRACE("End");

    return size;
}

int
myfs_rename(const char *path_old, const char *path_new, unsigned int flags) {
    char path_new_dir[MYFS_PATH_NAME_MAX_LEN + 1], name_new[MYFS_FILE_NAME_MAX_LEN + 1];
    myfs_file_t *file, *dir;
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; OldPath[%s]; NewPath[%s]; Flags[%u]", path_old, path_new, flags);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Get the directoy and name of the new path name.
    myfs_dirname(path_new, path_new_dir, sizeof(path_new_dir));
    myfs_basename(path_new, name_new, sizeof(name_new));

    //Get the old file.
    file = myfs_file_get(myfs, path_old, false);
    if (file == NULL) {
        return -ENOENT;
    }

    printf("  %d %d\n", RENAME_EXCHANGE, RENAME_NOREPLACE);
    if (flags == RENAME_EXCHANGE) {
        printf("EXCHANGE\n");
    }
    if (flags == RENAME_NOREPLACE) {
        printf("NOREPLACE\n");
    }

    //Get the file for the directory.
    dir = myfs_file_get(myfs, path_new_dir, false);
    if (dir == NULL) {
        myfs_file_free(file);
        return -ENOENT;
    }

    success = myfs_db_file_rename(myfs, file->file_id, dir->file_id, name_new);
    myfs_file_free(file);
    myfs_file_free(dir);

    if (!success) {
        return -EIO;
    }

    MYFS_LOG_TRACE("End");

    return 0;
}

int
myfs_symlink(const char *target, const char *path) {
    char dir[MYFS_PATH_NAME_MAX_LEN + 1], name[MYFS_FILE_NAME_MAX_LEN + 1];
    myfs_file_t *parent;
    myfs_t *myfs;
    bool success;

    MYFS_LOG_TRACE("Begin; Path[%s]; Target[%s]", path, target);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    //Get the directory and name of the soft link.
    myfs_dirname(path, dir, sizeof(dir));
    myfs_basename(path, name, sizeof(name));

    //Get the soft link's directory (parent).
    parent = myfs_file_get(myfs, dir, false);
    if (parent == NULL) {
        return -ENOENT;
    }

    success = myfs_db_file_create(myfs, name, MYFS_FILE_TYPE_SOFT_LINK, parent->file_id, target);
    myfs_file_free(parent);

    if (!success) {
        return -EIO;
    }

    MYFS_LOG_TRACE("End");

    return 0;
}

int
myfs_readlink(const char *path, char *buf, size_t size) {
    myfs_file_t *file;
    myfs_t *myfs;
    char *content;
    size_t content_len;

    MYFS_LOG_TRACE("Begin; Path[%s]; Size[%zu]", path, size);

    myfs = (myfs_t *)fuse_get_context()->private_data;

    file = myfs_file_get(myfs, path, false);
    if (file == NULL) {
        return -ENOENT;
    }

    if (file->type != MYFS_FILE_TYPE_SOFT_LINK) {
        myfs_file_free(file);
        return -EINVAL;
    }

    content = myfs_db_file_get_content(myfs, file->file_id, &content_len);
    myfs_file_free(file);

    if (content == NULL) {
        return -EIO;
    }

    //Truncate the content if it's larger than the buffer size (which includes space for '\0').
    //Increase the content length by 1 to include the '\0' which makes math easier.
    content_len++;
    if (content_len > size) {
        content_len = size;
        content[content_len - 1] = '\0';
    }

    memcpy(buf, content, content_len);
    free(content);

    MYFS_LOG_TRACE("End");
    return 0;
}
