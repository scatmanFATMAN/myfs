/**
 * @file db.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "db.h"

bool
db_connect(db_t *db, const char *host, const char *user, const char *password, const char *database, unsigned int port) {
    my_bool reconnect = 1;

    mysql_init(&db->mysql);

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

const char *
db_error(db_t *db) {
    return db->error;
}

bool
db_query(db_t *db, const char *query, int len) {
    int ret;

    ret = mysql_real_query(&db->mysql, query, len);
    if (ret != 0) {
        snprintf(db->error, sizeof(db->error), "%s", mysql_error(&db->mysql));
        return false;
    }

    return true;
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
    int ret;

    ret = mysql_real_query(&db->mysql, query, len);
    if (ret != 0) {
        snprintf(db->error, sizeof(db->error), "%s", mysql_error(&db->mysql));
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
