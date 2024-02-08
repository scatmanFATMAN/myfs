#include <stdlib.h>
#include <stdio.h>
#include "../common/log.h"
#include "../common/config.h"
#include "../common/string.h"
#include "../common/db.h"
#include "myfs_db.h"

#define MODULE "MyFS DB"

bool
myfs_db_file_create(myfs_t *myfs, const char *name, myfs_file_type_t type, unsigned int parent_id, const char *content) {
    char *name_esc, *content_esc;
    bool success;

    name_esc = db_escape(&myfs->db, name, NULL);

    //Files get a blank string while directories and other file types get NULL
    if (type == MYFS_FILE_TYPE_DIRECTORY || content == NULL) {
        content_esc = strdup("");
    }
    else {
        content_esc = db_escape(&myfs->db, content, NULL);
    }

    success = db_queryf(&myfs->db, "INSERT INTO `files` (`parent_id`,`name`,`type`,`content`,`created_on`,`last_accessed_on`,`last_modified_on`,`last_status_changed_on`)\n"
                                   "VALUES (%u,'%s',%u,'%s',UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP())",
                                   parent_id, name_esc, type, content_esc);

    free(name_esc);
    free(content_esc);

    if (!success) {
        log_err(MODULE, "Error creating file '%s' with Parent ID %u: %s", name, parent_id, db_error(&myfs->db));
    }

    return success;
}

bool
myfs_db_file_delete(myfs_t *myfs, unsigned int file_id) {
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

bool
myfs_db_file_set_times(myfs_t *myfs, unsigned int file_id, time_t last_accessed_on, time_t last_modified_on) {
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

bool
myfs_db_file_swap(myfs_t *myfs, myfs_file_t *file1, myfs_file_t *file2) {
    unsigned int parent1_id = 0, parent2_id = 0;
    bool success;

    if (file1->parent != NULL) {
        parent1_id = file1->parent->file_id;
    }
    if (file2->parent != NULL) {
        parent2_id = file2->parent->file_id;
    }

    //Start a transaction since this must be done atomically.
    success = db_transaction_start(&myfs->db);
    if (!success) {
        return false;
    }

    //Update the first file.
    success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                   "SET `parent_id`=%u\n"
                                   "WHERE `file_id`=%u",
                                   parent2_id,
                                   file1->file_id);

    if (!success) {
        log_err(MODULE, "Error swaping file File ID %u with File ID %u (first update): %s", file1->file_id, file2->file_id, db_error(&myfs->db));
    }

    //Update the second file.
    if (success) {
        success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                       "SET `parent_id`=%u\n"
                                       "WHERE `file_id`=%u",
                                       parent1_id,
                                       file2->file_id);

        if (!success) {
            log_err(MODULE, "Error swaping file File ID %u with File ID %u (second update): %s", file1->file_id, file2->file_id, db_error(&myfs->db));
        }
    }

    //Commit or rollback the transaction.
    db_transaction_stop(&myfs->db, success);

    return success;
}

bool
myfs_db_file_rename(myfs_t *myfs, unsigned int file_id, unsigned int parent_id, const char *name) {
    char *name_esc;
    bool success;

    name_esc = db_escape(&myfs->db, name, NULL);

    success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                   "SET `parent_id`=%u,`name`='%s'\n"
                                   "WHERE `file_id`=%u",
                                   parent_id, name_esc,
                                   file_id);

    free(name_esc);

    if (!success) {
        log_err(MODULE, "Error updating Parent ID for File ID %u: %s", file_id, db_error(&myfs->db));
    }

    return success;
}

char *
myfs_db_file_get_content(myfs_t *myfs, unsigned int file_id, size_t *len) {
    MYSQL_RES *res;
    MYSQL_ROW row;
    char *content = NULL;

    res = db_selectf(&myfs->db, "SELECT `content`,LENGTH(`content`)\n"
                                "FROM `files`\n"
                                "WHERE `file_id`=%u",
                                file_id);

    if (res == NULL) {
        log_err(MODULE, "Error getting file content for File ID %u: %s", file_id, db_error(&myfs->db));
        return NULL;
    }

    row = mysql_fetch_row(res);
    if (row != NULL) {
        content = strdup(row[0]);
        if (len != NULL) {
            *len = strtoul(row[1], NULL, 10);
        }
    }

    mysql_free_result(res);
    return content;
}

bool
myfs_db_file_set_content_size(myfs_t *myfs, unsigned int file_id, off_t size) {
    MYSQL_RES *res;
    MYSQL_ROW row;
    off_t current_size = -1, diff;
    bool success;

    //First, get the current size of the content so we know if we need to shrink, grow, or do nothing.
    res = db_selectf(&myfs->db, "SELECT LENGTH(`content`)\n"
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
                                       "SET `content`=CONCAT(`content`,REPEAT(' ',%zd))\n"
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
                                       "SET `content`=SUBSTRING(`content`,0,%zd)\n"
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

/**
 * Queries MariaDB for a MyFS's file's children. The file must be a MYFS_FILE_TYPE_DIRECTORY.
 *
 * @param[in] myfs The MyFS context.
 * @param[in,out] file The MyFS file to get children for.
 */

static void
myfs_db_file_query_children(myfs_t *myfs, myfs_file_t *file) {
    unsigned int i = 0;
    MYSQL_RES *res;
    MYSQL_ROW row;

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

    while ((row = mysql_fetch_row(res)) != NULL) {
        file->children[i++] = myfs_db_file_query(myfs, strtoul(row[0], NULL, 10), false);
    }

    mysql_free_result(res);
}

myfs_file_t *
myfs_db_file_query(myfs_t *myfs, unsigned int file_id, bool include_children) {
    struct fuse_context *fuse;
    myfs_file_t *file = NULL;
    MYSQL_RES *res;
    MYSQL_ROW row;

    fuse = fuse_get_context();

    res = db_selectf(&myfs->db, "SELECT `file_id`,`name`,`parent_id`,`type`,`last_accessed_on`,`last_modified_on`,`last_status_changed_on`,LENGTH(`content`)\n"
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
            file->parent = myfs_db_file_query(myfs, strtoul(row[2], NULL, 10), false);
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
            case MYFS_FILE_TYPE_SOFT_LINK:
                file->st.st_mode = S_IFLNK | 0600;
                file->st.st_nlink = 1;
                file->st.st_size = strtoul(row[7], NULL, 10);
                break;
            case MYFS_FILE_TYPE_INVALID:
                break;
        }
        file->st.st_ino = file->file_id;
        file->st.st_uid = fuse->uid;
        file->st.st_gid = fuse->gid;
        file->st.st_atime = strtoll(row[4], NULL, 10);
        file->st.st_mtime = strtoll(row[5], NULL, 10);
        file->st.st_ctime = strtoll(row[6], NULL, 10);
    }

    mysql_free_result(res);

    if (file != NULL && include_children) {
        myfs_db_file_query_children(myfs, file);
    }

    return file;
}

myfs_file_t *
myfs_db_file_query_name(myfs_t *myfs, const char *name, unsigned int parent_id, bool include_children) {
    myfs_file_t *file = NULL;
    MYSQL_RES *res;
    MYSQL_ROW row;
    char *name_esc = NULL;

    //Escape the name if needed.
    if (name != NULL && name[0] != '\0') {
        name_esc = db_escape(&myfs->db, name, NULL);
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
        file = myfs_db_file_query(myfs, strtoul(row[0], NULL, 10), include_children);
    }

    mysql_free_result(res);

    return file;
}

bool
myfs_db_get_num_files(myfs_t *myfs, uint64_t *count) {
    MYSQL_RES *res;
    MYSQL_ROW row;
    bool success = false;

    res = db_selectf(&myfs->db, "SELECT COUNT(*)\n"
                                "FROM `files`");

    if (res == NULL) {
        log_err(MODULE, "Error getting number of files: %s", db_error(&myfs->db));
        return false;
    }

    row = mysql_fetch_row(res);
    if (row == NULL) {
        log_err(MODULE, "Error getting number of files: Not data returned");
    }
    else {
        *count = strtoul(row[0], NULL, 10);
        success = true;
    }

    mysql_free_result(res);

    return success;
}

bool
myfs_db_get_space_used(myfs_t *myfs, uint64_t *space) {
    MYSQL_RES *res;
    MYSQL_ROW row;
    bool success = false;

    res = db_selectf(&myfs->db, "SELECT `data_length`+`index_length`\n"
                                "FROM `information_schema`.`tables`\n"
                                "WHERE `table_schema`='%s'",
                                config_get("mariadb_database"));

    if (res == NULL) {
        log_err(MODULE, "Error getting used space: %s", db_error(&myfs->db));
        return false;
    }

    row = mysql_fetch_row(res);
    if (row == NULL) {
        log_err(MODULE, "Error getting used space: No data returned");
    }
    else {
        *space = strtoul(row[0], NULL, 10);
        success = true;
    }

    mysql_free_result(res);

    return success;
}
