#pragma once

#include <stdbool.h>
#include <mariadb/mysql.h>

typedef struct {
    MYSQL mysql;
    char error[256];
} db_t;

bool db_connect(db_t *db, const char *host, const char *user, const char *password, const char *database, unsigned int port);
void db_disconnect(db_t *db);

const char * db_error(db_t *db);

bool db_query(db_t *db, const char *query, int len);
bool db_queryf(db_t *db, const char *fmt, ...);

MYSQL_RES * db_select(db_t *db, const char *query, int len);
MYSQL_RES * db_selectf(db_t *db, const char *fmt, ...);
