#pragma once

void create_run();

void create_get_sql_database(char *dst, size_t size, const char *name);
void create_get_sql_database_table1(char *dst, size_t size);
void create_get_sql_database_table2(char *dst, size_t size);
void create_get_sql_database_insert1(char *dst, size_t size);
void create_get_sql_database_insert2(char *dst, size_t size, const char *user, const char *group);
void create_get_sql_database_insert3(char *dst, size_t size);
void create_get_sql_database_user_create(char *dst, size_t size, const char *user, const char *host, const char *password);
void create_get_sql_database_user_grant1(char *dst, size_t size, const char *user, const char *host, const char *database);
void create_get_sql_database_user_grant2(char *dst, size_t size, const char *user, const char *host, const char *database);
