/**
 * @file db.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "db.h"

bool
db_connect(db_t *db, const char *host, const char *user, const char *password, const char *database, unsigned int port) {
    my_bool reconnect = 1;

    mysql_init(&db->mysql);

    db->failed_query_retry_wait = -1;       //Do not retry.
    db->failed_query_retry_count = -1;      //Retry forever (if retry_count != -1).

    //Enable auto reconnect and auto retry if a query fails.
    mysql_optionsv(&db->mysql, MYSQL_OPT_RECONNECT, (void *)&reconnect);

    if (mysql_real_connect(&db->mysql, host, user, password, database, port, NULL, 10) == NULL) {
        snprintf(db->error, sizeof(db->error), "%s", mysql_error(&db->mysql));
        return false;
    }

    return true;
}

void
db_disconnect(db_t *db) {
    mysql_close(&db->mysql);
}

void
db_set_failed_query_options(db_t *db, int retry_wait, int retry_count) {
    db->failed_query_retry_wait = retry_wait;
    db->failed_query_retry_count = retry_count;
}

const char *
db_error(db_t *db) {
    return db->error;
}

/**
 * Run a query and optionally retry if the query fails if `failed_query_retry_wait`
 * and `failed_query_retry_count` are set.
 */
static bool
db_query_timed(db_t *db, const char *query, int len) {
    time_t next_try = 0;
    int count = 0, ret;

    while (true) {
        //If `next_try` > 0, then a previous query failed.
        //Check to see if it's time to try the query again. If no, sleep for 50ms and check the timer again.
        if (next_try > time(NULL)) {
            usleep(1000 * 50);
            continue;
        }

        next_try = 0;
        ret = mysql_real_query(&db->mysql, query, len);

        //If `ret` is 0, then the query succeeded. If so, reset the error in case a previous query failed.
        if (ret == 0) {
            db->error[0] = '\0';
            break;
        }

        //Error! Figure out what to do.

        //If `failed_query_retry_wait` is -1, do not retry again.
        if (db->failed_query_retry_wait == -1) {
            snprintf(db->error, sizeof(db->error), "%s", mysql_error(&db->mysql));
            break;
        }

        //See if the max number of failed queries has been acheieved. If so, do not retry again.
        //-1 means retry forever
        if (db->failed_query_retry_count != -1) {
            if (++count >= db->failed_query_retry_count) {
                snprintf(db->error, sizeof(db->error), "%s", mysql_error(&db->mysql));
                break;
            }
        }

        //Set the timer for when to retry next.
        next_try = time(NULL) + db->failed_query_retry_wait;
    }

    return ret == 0;
}

bool
db_query(db_t *db, const char *query, int len) {
    return db_query_timed(db, query, len);
}

bool
db_queryf(db_t *db, const char *fmt, ...) {
    va_list ap;
    char *query;
    int len;
    bool success;

    va_start(ap, fmt);
    len = vasprintf(&query, fmt, ap);
    va_end(ap);

    success = db_query(db, query, len);
    free(query);

    return success;
}

MYSQL_RES *
db_select(db_t *db, const char *query, int len) {
    MYSQL_RES *res;
    bool success;

    success = db_query_timed(db, query, len);
    if (!success) {
        return NULL;
    }

    res = mysql_store_result(&db->mysql);
    if (res == NULL) {
        snprintf(db->error, sizeof(db->error), "%s", mysql_error(&db->mysql));
        return NULL;
    }

    return res;
}

MYSQL_RES *
db_selectf(db_t *db, const char *fmt, ...) {
    va_list ap;
    char *query;
    int len;
    MYSQL_RES *res;

    va_start(ap, fmt);
    len = vasprintf(&query, fmt, ap);
    va_end(ap);

    res = db_select(db, query, len);
    free(query);

    return res;
}

bool
db_database_exists(db_t *db, const char *name, bool *exists) {
    MYSQL_RES *res;
    MYSQL_ROW row;

    res = db_selectf(db, "SHOW DATABASES LIKE '%s'",
                         name);
    if (res == NULL) {
        return false;
    }

    row = mysql_fetch_row(res);
    *exists = row != NULL;
    mysql_free_result(res);

    return true;
}

bool
db_user_exists(db_t *db, const char *user, const char *host, bool *exists) {
    MYSQL_RES *res;
    MYSQL_ROW row;

    res = db_selectf(db, "SELECT COUNT(*)\n"
                         "FROM `mysql`.`user`\n"
                         "WHERE `User`='%s'\n"
                         "AND `Host`='%s'",
                         user, host);

    if (res == NULL) {
        return false;
    }

    row = mysql_fetch_row(res);
    *exists = strtoul(row[0], NULL, 10) > 0;
    mysql_free_result(res);

    return true;
}

bool
db_transaction_start(db_t *db) {
    return db_query(db, "START TRANSACTION", 17);
}

bool
db_transaction_stop(db_t *db, bool commit) {
    bool success;

    if (commit) {
        success = db_query(db, "COMMIT", 6);
    }
    else {
        success = db_query(db, "ROLLBACK", 8);
    }

    return success;
}

char *
db_escape(db_t *db, const char *str, unsigned int *length) {
    char *escaped;
    unsigned long escaped_len;
    size_t len;

    len = strlen(str);

    //mysql_real_escape_string() requires the new buffer to be (len * 2 + 1) size
    escaped = malloc(len * 2 + 1);

    escaped_len = mysql_real_escape_string(&db->mysql, escaped, str, len);

    if (length != NULL) {
        *length = escaped_len;
    }

    return escaped;
}
