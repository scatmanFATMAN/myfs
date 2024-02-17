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
    MYSQL mysql;                    //!< The handle to MariaDB and libmysqlclient.
    int failed_query_retry_wait;    //!< Number of seconds to wait before re-trying a failed query.
    int failed_query_retry_count;   //!< The total number of failed queries to retry.
    char error[256];                //!< Any error text.
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
 * Sets options for what to do when a query fails.
 *
 * @param[in] db The database context.
 * @param[in] retry_wait Number of seconds to wait before retrying a failed query. -1 means do not retry.
 * @param[in] retry_count The total number of failed queries to retry. If `retry_wait` is -1, this option is ignored. -1 means retry forever.
 */
void db_set_failed_query_options(db_t *db, int retry_wait, int retry_count);

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
 * Gets the last inserted auto increment ID.
 *
 * @param[in] db The database context.
 * @return The last inserted auto increment ID.
 */
unsigned long db_insert_id(db_t *db);

/**
 * Determines if the database exists.
 *
 * @param[in] db The database context.
 * @param[in] name The name of the database.
 * @param[out] exists Set to `true` if the database exists, otherwise `false`.
 * @return `true` if the query success, or `false` if there was a query error. If `false` is returned,
 *         you cannot depend on the value of `exists`.
 */
bool db_database_exists(db_t *db, const char *name, bool *exists);

/**
 * Determines if the database user for the given host exists.
 *
 * @param[in] db The database context.
 * @param[in] user The name of the user.
 * @param[in] host The host for the user.
 * @param[out] exists Set to `true` if the user exists, otherwise `false`.
 * @return `true` if the query success, or `false` if there was a query error. If `false` is returned,
 *         you cannot depend on the value of `exists`.
 */
bool db_user_exists(db_t *db, const char *user, const char *host, bool *exists);

/**
 * Starts a MariaDB transaction.
 *
 * @param[in] db The database context.
 * @return `true` if the transaction was started, otherwise `false`.
 */
bool db_transaction_start(db_t *db);

/**
 * Commits or rolls back a MariaDB transaction based on `commit`.
 *
 * @param[in] db The database context.
 * @param[in[ commit `true` to commit the transaction, `false` to roll it back.
 * @return `true` if the operation was successful, otherwise `false`.
 */
bool db_transaction_stop(db_t *db, bool commit);

/**
 * Escapes a string that's safe to use in queries. The string must be free'd after use.
 *
 * @param[in] db The database context.
 * @param[in] str The string to escape.
 * @param[out] length The length of the escaped string. If NULL, this will be ignored.
 * @return The escaped string.
 */
char * db_escape(db_t *db, const char *str, unsigned int *length);

/**
 * Escapes a string that's safe to use in queries. The string must be free'd after use. This
 * should be used to escape binary data using the correct length.
 *
 * @param[in] db The database context.
 * @param[in] str The string to escape.
 * @param[in] length The length of the string to be escaped.
 * @return The escaped string.
 */
char * db_escape_len(db_t *db, const char *str, unsigned int length);
