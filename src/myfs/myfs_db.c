#include <stdlib.h>
#include <stdio.h>
#include "../common/log.h"
#include "../common/config.h"
#include "../common/string.h"
#include "../common/db.h"
#include "util.h"
#include "myfs_db.h"

#define MODULE "MyFS DB"

#define MYFSDB_TRACE
#if defined(MYFSDB_TRACE)
# define MYFSDB_LOG_TRACE(fmt, ...)                 \
        do {                                        \
            printf("[%s] ", __FUNCTION__);          \
            printf(fmt, ##__VA_ARGS__);             \
            printf("\n");                           \
            fflush(stdout);                         \
        } while(0)
#else
# define MYFSDB_LOG_TRACE(fmt, ...)
#endif

/**
 * Takes a global offset and determines which block index the offset is in.
 *
 * @param[in] offset The global offset.
 * @return The block index.
 */
static size_t
myfs_db_file_block_index(size_t offset) {
    return offset / MYFS_FILE_BLOCK_SIZE;
}

/**
 * Takes a global offset and maps it to an offset within a block.
 *
 * Offset = 5    -> BlockOffset = 5
 * Offset = 4096 -> BlockOffset = 0
 * Offset = 4098 -> BlockOffset = 2
 *
 * @param[in] offset The global offset.
 * @return The offset within a block.
 */
static size_t
myfs_db_file_block_offset(off_t offset) {
    return offset % MYFS_FILE_BLOCK_SIZE;
}

/**
 * Takes length and determines how many blocks it spans.
 *
 * @param[in] len The length.
 * @return The number of blocks.
 */
static size_t
myfs_db_file_block_count(size_t len) {
    size_t count;

    count = len / MYFS_FILE_BLOCK_SIZE;
    if (len % MYFS_FILE_BLOCK_SIZE != 0) {
        count++;
    }

    return count;
}

unsigned int
myfs_db_file_create(myfs_t *myfs, const char *name, myfs_file_type_t type, unsigned int parent_id, mode_t mode) {
    char *name_esc, *user_esc, *group_esc;
    bool success;

    name_esc = db_escape(&myfs->db, name, NULL);
    user_esc = db_escape(&myfs->db, config_get("user"), NULL);
    group_esc = db_escape(&myfs->db, config_get("group"), NULL);

    switch (type) {
        case MYFS_FILE_TYPE_FILE:
            if (!(mode & S_IFREG)) {
                mode |= S_IFREG;
            }
            break;
        case MYFS_FILE_TYPE_DIRECTORY:
            if (!(mode & S_IFDIR)) {
                mode |= S_IFDIR;
            }
            break;
        case MYFS_FILE_TYPE_SOFT_LINK:
            if (!(mode & S_IFLNK)) {
                mode |= S_IFLNK;
            }
            break;
        case MYFS_FILE_TYPE_INVALID:
            break;
    }

    success = db_queryf(&myfs->db, "INSERT INTO `files` (`parent_id`,`name`,`type`,`user`,`group`,`mode`,`size`,`created_on`,`last_accessed_on`,`last_modified_on`,`last_status_changed_on`)\n"
                                   "VALUES (%u,'%s','%s','%s','%s',%u,0,UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP(),UNIX_TIMESTAMP())",
                                   parent_id, name_esc, myfs_file_type_str(type), user_esc, group_esc, mode);

    free(name_esc);
    free(user_esc);
    free(group_esc);

    if (!success) {
        log_err(MODULE, "Error creating file '%s' with Parent ID %u: %s", name, parent_id, db_error(&myfs->db));
        return 0;
    }

    return db_insert_id(&myfs->db);
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
        return false;
    }

    return true;
}

bool
myfs_db_file_write(myfs_t *myfs, unsigned int file_id, const char *data, size_t len, off_t offset) {
    unsigned int file_data_id, file_data_length, index, limit;
    size_t left, write_size, written, page_offset;
    char *data_esc;
    bool success;
    MYSQL_RES *res;
    MYSQL_ROW row;

    MYFSDB_LOG_TRACE("Begin");
    MYFSDB_LOG_TRACE("  FileID[%u]; Len[%zu]; Offset[%zd]", file_id, len, offset);

    index = myfs_db_file_block_index(offset);
    page_offset = myfs_db_file_block_offset(offset);
    limit = myfs_db_file_block_count(len);

    MYFSDB_LOG_TRACE("  Index[%u]; PageOffset[%zu]; Limit[%u]", index, page_offset, limit);

    success = db_transaction_start(&myfs->db);
    if (!success) {
        log_err(MODULE, "Error adding data for File ID %u: Failed to start transaction: %s", file_id, db_error(&myfs->db));
        return false;
    }

    //Get the block to write to.
    res = db_selectf(&myfs->db, "SELECT `file_data_id`,`index`,LENGTH(`data`)\n"
                                "FROM `file_data`\n"
                                "WHERE `file_id`=%u\n"
                                "AND `index`>=%u\n"
                                "ORDER BY `index` ASC\n"
                                "LIMIT %u",
                                file_id,
                                index,
                                limit);

    if (res == NULL) {
        log_err(MODULE, "Error writing data for File ID %u: Failed getting block %u: %s", file_id, index, db_error(&myfs->db));
        success = false;
        goto done;
    }

    MYFSDB_LOG_TRACE("  Found %lld blocks to update", mysql_num_rows(res));

    file_data_id = 0;
    file_data_length = 0;
    written = 0;
    left = len;

    //Loop through the blocks that are being overwritten.
    while (left > 0 && (row = mysql_fetch_row(res)) != NULL) {
        file_data_id = strtoul(row[0], NULL, 10);
        index = strtoul(row[1], NULL, 10);
        file_data_length = strtoul(row[2], NULL, 10);

        //The first write may start at any offset inside the block.
        if (written == 0) {
            write_size = left;
            //if (write_size > MYFS_FILE_BLOCK_SIZE - file_data_length) {
            //    write_size = MYFS_FILE_BLOCK_SIZE - file_data_length;
            //}
            if (write_size > MYFS_FILE_BLOCK_SIZE - page_offset) {
                write_size = MYFS_FILE_BLOCK_SIZE - page_offset;
            }
        }
        else {
            write_size = left;
            if (write_size > MYFS_FILE_BLOCK_SIZE) {
                write_size = MYFS_FILE_BLOCK_SIZE;
            }
        }

        MYFSDB_LOG_TRACE("  Updating Block; Index[%u]; FileDataID[%u]; FileDataLength[%u]; WriteSize[%zu]; Written[%zu]; Left[%zu]", index, file_data_id, file_data_length, write_size, written, left);

        data_esc = db_escape_len(&myfs->db, data + written, write_size);

        //MariaDB indexes start at 1 so page_offset+1 is necessary
        success = db_queryf(&myfs->db, "UPDATE `file_data`\n"
                                       "SET `data`=INSERT(`data`,%zu,%zu,'%s')\n"
                                       "WHERE `file_data_id`=%u",
                                       page_offset + 1, write_size, data_esc,
                                       file_data_id);
        free(data_esc);

        if (!success) {
            log_err(MODULE, "Error writing data for File ID %u: Failed writing to block %u: %s", file_id, index, db_error(&myfs->db));
            goto done;
        }

        left -= write_size;
        written += write_size;
        page_offset = 0;

        //Keep track of next block to write incase this is the last one and new blocks need to inserted.
        index++;
    }

    mysql_free_result(res);

    MYFSDB_LOG_TRACE("  Left[%zu]; Written[%zu]", left, written);

    //Write any new blocks that need to be written.
    if (left > 0) {
        MYFSDB_LOG_TRACE("  Adding block; Index[%u]", index);

        //If new blocks are being written, the file size will increase so do that now.
        success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                       "SET `size`=`size`+%zu\n"
                                       "WHERE `file_id`=%u",
                                       left,
                                       file_id);
        if (!success) {
            log_err(MODULE, "Error writing data for File ID %u: Failed updating file size: %s", file_id, db_error(&myfs->db));
            goto done;
        }

        while (left > 0) {
            write_size = left;
            if (write_size > MYFS_FILE_BLOCK_SIZE) {
                write_size = MYFS_FILE_BLOCK_SIZE;
            }
            
            data_esc = db_escape_len(&myfs->db, data + written, write_size);

            success = db_queryf(&myfs->db, "INSERT INTO `file_data` (`file_id`,`index`,`data`)\n"
                                           "VALUES (%u,%u,'%s')",
                                           file_id, index, data_esc);

            free(data_esc);

            if (!success) {
                log_err(MODULE, "Error writing data for File ID %u: Failed adding block %u: %s", file_id, index, db_error(&myfs->db));
                goto done;
            }

            left -= write_size;
            written += write_size;
            index++;
        }
    }

done:
    db_transaction_stop(&myfs->db, success);

    MYFSDB_LOG_TRACE("  Written[%zd]", written);
    MYFSDB_LOG_TRACE("End");

    return success;
}

bool
myfs_db_file_append(myfs_t *myfs, unsigned int file_id, const char *data, size_t len) {
    unsigned int file_data_id = 0, index = 0, file_data_length = 0;
    size_t write_size, written = 0, left;
    char *data_esc;
    bool success;
    MYSQL_RES *res;
    MYSQL_ROW row;

    MYFSDB_LOG_TRACE("Begin");
    MYFSDB_LOG_TRACE("  FileID[%u]; Len[%zu]", file_id, len);

    success = db_transaction_start(&myfs->db);
    if (!success) {
        log_err(MODULE, "Error appending data to File ID %u: Failed starting transaction: %s", file_id, db_error(&myfs->db));
        return false;
    }

    //Get the latest block if there is one
    res = db_selectf(&myfs->db, "SELECT `file_data_id`,`index`,LENGTH(`data`)\n"
                                "FROM `file_data`\n"
                                "WHERE `file_id`=%u\n"
                                "ORDER BY `index` DESC\n"
                                "LIMIT 1",
                                file_id);

    if (res == NULL) {
        log_err(MODULE, "Error appending data to File ID %u: Failed getting last block: %s", file_id, db_error(&myfs->db));
        success = false;
        goto done;
    }

    row = mysql_fetch_row(res);
    if (row != NULL) {
        file_data_id = strtoul(row[0], NULL, 10);
        index = strtoul(row[1], NULL, 10);
        file_data_length = strtoul(row[2], NULL, 10);
    }
    mysql_free_result(res);

    MYFSDB_LOG_TRACE("  FileDataID[%u]; Index[%u]; FileDataLength[%u]", file_data_id, index, file_data_length);

    //Update the file's size
    success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                   "SET `size`=`size`+%zu\n"
                                   "WHERE `file_id`=%u",
                                   len,
                                   file_id);

    if (!success) {
        log_err(MODULE, "Error appending data to File ID %u: Failed updating file size: %s", file_id, db_error(&myfs->db));
        goto done;
    }

    written = 0;
    left = len;

    //Update the last block if there is one and it's not full.
    if (file_data_id > 0) {
        if (file_data_length < MYFS_FILE_BLOCK_SIZE) {
            write_size = left;
            if (write_size > MYFS_FILE_BLOCK_SIZE - file_data_length) {
                write_size = MYFS_FILE_BLOCK_SIZE - file_data_length;
            }

            MYFSDB_LOG_TRACE("  Updating Last Block; Index[%u]; WriteSize[%zu]", index, write_size);

            data_esc = db_escape_len(&myfs->db, data, write_size);

            success = db_queryf(&myfs->db, "UPDATE `file_data`\n"
                                           "SET `data`=CONCAT(`data`,'%s')\n"
                                           "WHERE `file_data_id`=%u",
                                           data_esc,
                                           file_data_id);

            free(data_esc);

            if (!success) {
                log_err(MODULE, "Error appending data to File ID %u: Failed updating last block: %s", file_id, db_error(&myfs->db));
                goto done;
            }

            written += write_size;
            left -= write_size;
        }

        //Next block will be new, increase the index regardless of whether we were able to update the last block or not.
        index++;
    }

    //Add new blocks.
    while (left > 0) {
        write_size = left;
        if (write_size > MYFS_FILE_BLOCK_SIZE) {
            write_size = MYFS_FILE_BLOCK_SIZE;
        }

        MYFSDB_LOG_TRACE("  Adding Block; Index[%u]; WriteSize[%zu]; Written[%zu]", index, write_size, written);

        data_esc = db_escape_len(&myfs->db, data + written, write_size);

        success = db_queryf(&myfs->db, "INSERT INTO `file_data` (`file_id`,`index`,`data`)\n"
                                       "VALUES (%u,%u,'%s')",
                                       file_id, index, data_esc);

        free(data_esc);

        if (!success) {
            log_err(MODULE, "Error appending data to File ID %u: Failed adding block %u: %s", file_id, index, db_error(&myfs->db));
            goto done;
        }

        written += write_size;
        left -= write_size;
        index++;
    }

done:
    db_transaction_stop(&myfs->db, success);

    MYFSDB_LOG_TRACE("  Written[%zu]", written);
    MYFSDB_LOG_TRACE("End");

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
myfs_db_file_chown(myfs_t *myfs, unsigned int file_id, const char *user, const char *group) {
    char *user_esc = NULL, *group_esc = NULL;
    char query[2048];
    int len;
    bool success;

    //Escape the user/group.
    if (user != NULL && user[0] != '\0') {
        user_esc = db_escape(&myfs->db, user, NULL);
    }
    if (group != NULL && group[0] != '\0') {
        group_esc = db_escape(&myfs->db, group, NULL);
    }

    if (user_esc == NULL && group_esc == NULL) {
        return false;
    }

    //Build the SQL based on whether the user, group, or both are being set.
    len = strlcpy(query, "UPDATE `files`\nSET ", sizeof(query));
    if (user_esc != NULL && group_esc != NULL) {
        len += snprintf(query + len, sizeof(query) - len, "`user`='%s',`group`='%s'\n", user_esc, group_esc);
    }
    else if (user_esc != NULL) {
        len += snprintf(query + len, sizeof(query) - len, "`user`='%s'\n", user_esc);
    }
    else if (group_esc != NULL) {
        len += snprintf(query + len, sizeof(query) - len, "`group`='%s'\n", group_esc);
    }
    len += snprintf(query + len, sizeof(query) - len, "WHERE `file_id`=%u", file_id);

    //Run!
    success = db_query(&myfs->db, query, len);
    if (!success) {
        log_err(MODULE, "Error setting user[%s] and group[%s] on File ID %u: %s", user, group, file_id, db_error(&myfs->db));
    }

    if (user_esc != NULL) {
        free(user_esc);
    }
    if (group_esc != NULL) {
        free(group_esc);
    }

    return success;
}

bool
myfs_db_file_chmod(myfs_t *myfs, unsigned int file_id, mode_t mode) {
    bool success;

    success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                   "SET `mode`=%u\n"
                                   "WHERE `file_id`=%u",
                                   mode,
                                   file_id);

    if (!success) {
        log_err(MODULE, "Error setting mode[%u] on File ID %u: %s", mode, file_id, db_error(&myfs->db));
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

ssize_t
myfs_db_file_read(myfs_t *myfs, unsigned int file_id, char *buf, size_t size, off_t offset) {
    unsigned int index, data_len, limit, page_offset;
    ssize_t count;
    const char *data;
    MYSQL_RES *res;
    MYSQL_ROW row;

    MYFSDB_LOG_TRACE("Begin");
    MYFSDB_LOG_TRACE("  FileID[%u]; Size[%zu]; Offset[%zd]", file_id, size, offset);

    index = myfs_db_file_block_index(offset);
    page_offset = myfs_db_file_block_offset(offset);
    limit = myfs_db_file_block_count(size);

    MYFSDB_LOG_TRACE("  Index[%u]; PageOffset[%u]; Limit[%u]", index, page_offset, limit);

    res = db_selectf(&myfs->db, "SELECT `data`,LENGTH(`data`)\n"
                                "FROM `file_data`\n"
                                "WHERE `file_id`=%u\n"
                                "AND `index`>=%u\n"
                                "ORDER BY `index` ASC\n"
                                "LIMIT %u",
                                file_id,
                                index,
                                limit);

    if (res == NULL) {
        log_err(MODULE, "Error reading data for File ID %u: Failed getting block %u: %s", file_id, index, db_error(&myfs->db));
        return -1;
    }

    count = 0;
    while ((row = mysql_fetch_row(res)) != NULL) {
        data = row[0];
        data_len = strtoul(row[1], NULL, 10);

        //Adjust the amount of data to read if needed.
        if (data_len > size) {
            data_len = size;
        }

        MYFSDB_LOG_TRACE("  Reading; PageOffset[%u]; Count[%zd]; DataLen[%u]", page_offset, count, data_len);

        //Copy the data into the output buffer.
        memcpy(buf + count, data + page_offset, data_len);

        //If a second page has to be read, reset the page offset to 0.
        page_offset = 0;

        count += data_len;
        size -= count;
    }

    mysql_free_result(res);

    MYFSDB_LOG_TRACE("  Count[%zd]", count);
    MYFSDB_LOG_TRACE("Done");

    return count;
}

bool
myfs_db_file_truncate(myfs_t *myfs, unsigned int file_id, off_t size) {
    MYSQL_RES *res;
    MYSQL_ROW row;
    off_t current_size = -1, diff, write_size, file_data_length, left;
    unsigned int file_data_id, index;
    bool success;

    success = db_transaction_start(&myfs->db);
    if (!success) {
        log_err(MODULE, "Error truncating File ID %u: Failed to start transaction: %s", file_id, db_error(&myfs->db));
        return false;
    }

    //First, get the current file size so we know if we need to shrink, grow, or do nothing.
    res = db_selectf(&myfs->db, "SELECT `size`\n"
                                "FROM `files`\n"
                                "WHERE `file_id`=%u",
                                file_id);

    if (res == NULL) {
        log_err(MODULE, "Error truncating File ID %u: Error getting current file size: %s", file_id, db_error(&myfs->db));
        success = false;
        goto done;
    }

    row = mysql_fetch_row(res);
    if (row != NULL) {
        current_size = strtoul(row[0], NULL, 10);
    }

    mysql_free_result(res);

    if (current_size == -1) {
        log_err(MODULE, "Error truncating File ID %u: Not found", file_id);
        success = false;
        goto done;
    }

    diff = size - current_size;

    //Update the file's size
    if (diff != 0) {
        success = db_queryf(&myfs->db, "UPDATE `files`\n"
                                       "SET `size`=%zd\n"
                                       "WHERE `file_id`=%u",
                                       size,
                                       file_id);

        if (!success) {
            log_err(MODULE, "Error truncating File ID %u: Error setting new file size to %zd: %s", file_id, size, db_error(&myfs->db));
            goto done;
        }
    }

    //Grow or shrink now.
    if (diff > 0) {
        left = diff;

        //Get the last block, if there is one, and fill in any remaining space.
        res = db_selectf(&myfs->db, "SELECT `file_data_id`,`index`,LENGTH(`data`)\n"
                                    "FROM `file_data`\n"
                                    "WHERE `file_id`=%u\n"
                                    "ORDER BY `index` DESC\n"
                                    "LIMIT 1",
                                    file_id);

        if (res == NULL) {
            log_err(MODULE, "Error truncating File ID %u: Error getting last block: %s", file_id, db_error(&myfs->db));
            success = false;
            goto done;
        }

        file_data_id = 0;
        index = 0;
        file_data_length = 0;

        row = mysql_fetch_row(res);
        if (row != NULL) {
            file_data_id = strtoul(row[0], NULL, 10);
            index = strtoul(row[1], NULL, 10) + 1;
            file_data_length = strtoul(row[2], NULL, 10);
        }
        mysql_free_result(res);

        //If the last block has a data size smaller than the max block size, update this block.
        if (file_data_id > 0) {
            if (file_data_length < MYFS_FILE_BLOCK_SIZE) {
                write_size = left;
                if (write_size > MYFS_FILE_BLOCK_SIZE - file_data_length) {
                    write_size = MYFS_FILE_BLOCK_SIZE - file_data_length;
                }

                success = db_queryf(&myfs->db, "UPDATE `file_data`\n"
                                               "SET `data`=CONCAT(`data`,REPEAT(' ',%zd))\n"
                                               "WHERE `file_data_id`=%u",
                                               write_size,
                                               file_data_id);

                if (!success) {
                    log_err(MODULE, "Error truncating File ID %u: Error updating last block: %s", file_id, db_error(&myfs->db));
                    goto done;
                }

                left -= write_size;
            }
        }

        //Now add blocks until the entire size has been written.
        while (left > 0) {
            write_size = left;
            if (write_size > MYFS_FILE_BLOCK_SIZE) {
                write_size = MYFS_FILE_BLOCK_SIZE;
            }

            success = db_queryf(&myfs->db, "INSERT INTO `file_data` (`file_id`,`index`,`data`)\n"
                                           "VALUES (%u,%u,REPEAT(' ',%zd))",
                                           file_id, index, write_size);

            if (!success) {
                log_err(MODULE, "Error truncating File ID %u: Error adding block %u: %s", file_id, index, db_error(&myfs->db));
                goto done;
            }

            index++;
            left -= write_size;
        }
    }
    else if (diff < 0) {
        left = diff * -1;

        while (left > 0) {
            write_size = left;
            if (write_size > MYFS_FILE_BLOCK_SIZE) {
                write_size = MYFS_FILE_BLOCK_SIZE;
            }

            //Get the last block and its size to determine if the block can be deleted or shrunk.
            res = db_selectf(&myfs->db, "SELECT `file_data_id`,LENGTH(`data`)\n"
                                        "FROM `file_data`\n"
                                        "WHERE `file_id`=%u\n"
                                        "ORDER BY `index` DESC\n"
                                        "LIMIT 1",
                                        file_id);

            if (res == NULL) {
                log_err(MODULE, "Error truncating File ID %u: Error getting last block: %s", file_id, db_error(&myfs->db));
                success = false;
                goto done;
            }

            file_data_id = 0;
            file_data_length = 0;

            row = mysql_fetch_row(res);
            if (row == NULL) {
                log_warn(MODULE, "Error truncating File ID %u: Expected %zd more bytes to truncate but found no more blocks", file_id, left);
            }
            else {
                file_data_id = strtoul(row[0], NULL, 10);
                file_data_length = strtoul(row[1], NULL, 10);
            }
            mysql_free_result(res);

            if (file_data_id == 0) {
                success = false;
                goto done;
            }

            if (write_size >= file_data_length) {
                //This entire block can be deleted.
                success = db_queryf(&myfs->db, "DELETE FROM `file_data`\n"
                                               "WHERE `file_data_id`=%u",
                                               file_data_id);

                if (!success) {
                    log_err(MODULE, "Error truncating File ID %u: Failed to delete block: %s", file_id, db_error(&myfs->db));
                    goto done;
                }

                left -= file_data_length;
            }
            else {
                //The block needs to be shrunk.
                success = db_queryf(&myfs->db, "UPDATE `file_data`\n"
                                               "SET `data`=REPEAT(' ',%zd)\n"
                                               "WHERE `file_data_id`=%u",
                                               file_data_length - write_size,
                                               file_data_id);

                if (!success) {
                    log_err(MODULE, "Error truncating File ID %u: Failed to shrink block: %s", file_id, db_error(&myfs->db));
                    goto done;
                }

                left -= write_size;
            }
        }
    }

done:

    db_transaction_stop(&myfs->db, success);

    return success;
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
    int ret;

    fuse = fuse_get_context();

    res = db_selectf(&myfs->db, "SELECT `file_id`,`name`,`parent_id`,`type`,`user`,`group`,`mode`,`size`,`last_accessed_on`,`last_modified_on`,`last_status_changed_on`\n"
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
        file->st.st_mode = strtoul(row[6], NULL, 10);

        //Setup the struct stat.
        //TODO: I don't really need the `type` field anymore because I should be able to use st.st_mode to know what type it is. It's just kinda nice seeing the type in the database though. Easier to query too... so maybe it's a good idea to leave it.
        switch (file->type) {
            case MYFS_FILE_TYPE_FILE:
                file->st.st_nlink = 1;
                file->st.st_size = strtoul(row[7], NULL, 10);
                break;
            case MYFS_FILE_TYPE_DIRECTORY:
                file->st.st_nlink = 2;
                break;
            case MYFS_FILE_TYPE_SOFT_LINK:
                file->st.st_nlink = 1;
                file->st.st_size = strtoul(row[7], NULL, 10);
                break;
            case MYFS_FILE_TYPE_INVALID:
                break;
        }

        file->st.st_ino = file->file_id;
        ret = util_user_id(row[4], &file->st.st_uid);
        if (ret != 0) {
            //Did not find the user in the database, fallback to the configured user

            //TODO: Make a config option on how to handle this condition?
            log_err(MODULE, "Error getting user '%s' for File ID %u: %s", row[4], file->file_id, strerror(ret));
            log_err(MODULE, "Setting the user to the configured user '%s'", config_get("user"));

            ret = util_user_id(config_get("user"), &file->st.st_uid);
            if (ret != 0) {
                //Did not find the configured user, use the UID from FUSE

                log_err(MODULE, "Error getting user '%s' for File ID %u: %s", config_get("user"), file->file_id, strerror(ret));
                log_err(MODULE, "Setting the user to the program's UID %u", fuse->uid);
                file->st.st_uid = fuse->uid;
            }
        }
        ret = util_group_id(row[5], &file->st.st_gid);
        if (ret != 0) {
            //Did not find the group in the database, fallback to the configured group

            //TODO: Make a config option on how to handle this condition?
            log_err(MODULE, "Error getting group '%s' for File ID %u: %s", row[5], file->file_id, strerror(ret));
            log_err(MODULE, "Setting the group to the configured group '%s'", config_get("group"));

            ret = util_group_id(config_get("group"), &file->st.st_gid);
            if (ret != 0) {
                //Did not find the configured group, use the GID from FUSE

                log_err(MODULE, "Error getting group '%s' for File ID %u: %s", config_get("group"), file->file_id, strerror(ret));
                log_err(MODULE, "Setting the group to the configured group %u", fuse->gid);
                file->st.st_gid = fuse->gid;
            }
        }
        file->st.st_atime = strtoll(row[8], NULL, 10);
        file->st.st_mtime = strtoll(row[9], NULL, 10);
        file->st.st_ctime = strtoll(row[10], NULL, 10);
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
