#pragma once

/**
 * @file db.h
 *
 * A database module.
 */

#include <stdbool.h>
#include <mariadb/mysql.h>

/**
 * The database context.
 */
typedef struct {
    MYSQL mysql;        //!< The handle to MariaDB and libmysqlclient.
    char error[256];    //!< Any error text.
} db_t;

/**
 * Connects to MariaDB.
 *
 * @param[in] db The database context.
 * @param[in] host The MariaDB host to connect to.
 * @param[in] user The MariaDB user to connect as.
 * @param[in] password The MariaDB user's password.
 * @param[in] database The MariaDB database to use.
 * @param[in] port The MariaDB port to connect to.
 * @return `true` if the connection was successful, otherwise `false`.
 */
bool db_connect(db_t *db, const char *host, const char *user, const char *password, const char *database, unsigned int port);

/**
 * Diconnects from MariaDB.
 *
 * @param[in] db The database context.
 */
void db_disconnect(db_t *db);

/**
 * Returns the last error message. If no error has occurred, a blank string will be returned. The
 * error messages are not cleared out if an error occurs and then a successful operation occurs.
 *
 * @param[in] db The database context.
 * @return The last error message for the last error that occurred.
 */
const char * db_error(db_t *db);

/**
 * Runs a query. This query should not be a SELECT type query.
 *
 * @param[in] db The database context.
 * @param[in] query The database query.
 * @param[in] len The length of the database query.
 * @return `true` if the query was successful, otherwise `false`.
 */
bool db_query(db_t *db, const char *query, int len);

/**
 * Runs a query. This query should not be a SELECT type query.
 *
 * @param[in] db The database context.
 * @param[in[ fmt A printf formatted string for the query.
 * @return `true` if the query was successful, otherwise `false`.
 */
bool db_queryf(db_t *db, const char *fmt, ...);

/**
 * Runs a query. This query should be a SELECT type query.
 *
 * @param[in] db The database context.
 * @param[in] query The database query.
 * @param[in] len The length of the database query.
 * @return The MariaDB result or `NULL` of an error occurred.
 */
MYSQL_RES * db_select(db_t *db, const char *query, int len);

/**
 * Runs a query. This query should be a SELECT type query.
 *
 * @param[in] db The database context.
 * @param[in] fmt A printf formatted string for the query.
 * @return The MariaDB result or `NULL` of an error occurred.
 */
MYSQL_RES * db_selectf(db_t *db, const char *fmt, ...);

/**
 * Escapes a string that's safe to use in queries. The string must be free'd after use.
 *
 * @param[in] db The database context.
 * @param[in] str The string to escape.
 * @return The escaped string.
 */
char * db_escape(db_t *db, const char *str);
