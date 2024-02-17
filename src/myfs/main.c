#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include "../common/log.h"
#include "../common/config.h"
#include "../common/db.h"
#include "version.h"
#include "util.h"
#include "create.h"
#include "reclaimer.h"
#include "myfs.h"

#define MODULE "Main"

/** Return codes for main() */
#define MYFS_RETURN_SUCCESS  0
#define MYFS_RETURN_CONFIG   1
#define MYFS_RETURN_DATABASE 2

static void
config_error(const char *message) {
    log_err(MODULE, "%s", message);
}

static bool
config_handle_create(const char *name, const char *value) {
    create_run();
    return false;
}

static bool
config_handle_print_create_sql(const char *name, const char *value) {
    char sql[2048];

    create_get_sql_database(sql, sizeof(sql), "<myfs_database>");
    printf("%s\n", sql);
    printf("\n");
    printf("USE `%s`;\n", "<myfs_database>");
    printf("\n");
    create_get_sql_database_table1(sql, sizeof(sql));
    printf("%s\n", sql);
    printf("\n");
    create_get_sql_database_table2(sql, sizeof(sql));
    printf("%s\n", sql);
    printf("\n");
    create_get_sql_database_table3(sql, sizeof(sql));
    printf("%s\n", sql);
    printf("\n");
    create_get_sql_database_insert1(sql, sizeof(sql));
    printf("%s\n", sql);
    printf("\n");
    create_get_sql_database_insert2(sql, sizeof(sql), "<linux user>", "<linux group>");
    printf("%s\n", sql);
    printf("\n");
    create_get_sql_database_insert3(sql, sizeof(sql));
    printf("%s\n", sql);
    printf("\n");
    create_get_sql_database_user_create(sql, sizeof(sql), "<myfs_user>", "<myfs_user_host>", "<myfs_user_password>");
    printf("%s\n", sql);
    printf("\n");
    create_get_sql_database_user_grant1(sql, sizeof(sql), "<myfs_user>", "<myfs_user_host>", "<myfs_database>");
    printf("%s\n", sql);
    printf("\n");
    create_get_sql_database_user_grant2(sql, sizeof(sql), "<myfs_user>", "<myfs_user_host>", "<myfs_database>");
    printf("%s\n", sql);
    printf("\n");
    printf("FLUSH PRIVILEGES;\n");
    printf("\n");

    return false;
}

static bool
config_handle_log_stdout(const char *name, const char *value) {
    if (strcmp(value, "true") == 0) {
        log_stdout(true);
        config_set_bool(name, true);
    }
    else {
        log_stdout(false);
        config_set_bool(name, false);
    }

    return true;
}

static bool
config_handle_log_syslog(const char *name, const char *value) {
    if (strcmp(value, "true") == 0) {
        log_syslog(VERSION_NAME);
        config_set_bool(name, true);
    }
    else {
        log_syslog(NULL);
        config_set_bool(name, false);
    }

    return true;
}

static char **
fargs_get(const char *name, int *fargc) {
    int index = 0;
    char **fargv;

    //Space for the program name, just like argc[0]
    *fargc = 1;

    //If --mount is being used, then we need to replicate FUSE's -f option
    if (config_has("mount")) {
        *fargc += 2;
    }

    fargv = calloc(*fargc, sizeof(char *));

    //Start with argv[0], the program name
    fargv[index++] = strdup(name);

    //Replicate -f if needed
    if (config_has("mount")) {
        fargv[index++] = strdup("-f");
        fargv[index++] = config_dupe("mount");
    }

    return fargv;
}

static void
fargs_free(int fargc, char **fargv) {
    int i;

    for (i = 0; i < fargc; i++) {
        free(fargv[i]);
    }

    free(fargv);
}

static bool
check_config() {
    int failed_query_retry_wait, failed_query_retry_count;

    //failed_query_retry_wait:  -1 means do not retry.
    //failed_query_retry_count: -1 means retry forever.

    failed_query_retry_wait = config_get_int("failed_query_retry_wait");
    failed_query_retry_count = config_get_int("failed_query_retry_count");

    if (failed_query_retry_wait < -1) {
        log_err(MODULE, "Config error: failed_query_retry_wait[%d] cannot be less than -1", failed_query_retry_wait);
        return false;
    }

    if (failed_query_retry_count < -1) {
        log_err(MODULE, "Config error: failed_query_retry_count[%d] cannot be less than -1", failed_query_retry_count);
        return false;
    }

    return true;
}

static bool
confirm_config() {
    char input[8];

    //TODO: don't confirm if there's no console attached
    if (false) {
        return true;
    }

    printf("\n");
    printf("Database:                 %s@%s:%s/%s\n", config_get("mariadb_user"), config_get("mariadb_host"), config_get("mariadb_port"), config_get("mariadb_database"));
    printf("Mount point:              %s\n", config_get("mount"));
    printf("User:                     %s\n", config_get("user"));
    printf("Group:                    %s\n", config_get("group"));
    if (config_equals("failed_query_retry_wait", "-1")) {
        printf("Failed query retry wait:  Not retrying\n");
        printf("Failed query retry count: Not retrying\n");
    }
    else {
        printf("Failed query retry wait:  %s seconds\n", config_get("failed_query_retry_wait"));
        printf("Failed query retry count: %s\n", config_equals("failed_query_retry_count", "-1") ? "Retrying forever" : config_get("failed_query_retry_count"));
    }
    printf("\n");

    util_create_prompt(input, sizeof(input), "Confirm settings[y/n]?");

    if (strcmp(input, "y") == 0) {
        printf("\n");
        return true;
    }

    return false;
}

int
main(int argc, char **argv) {
    char user[MYFS_USER_NAME_MAX_LEN + 1], group[MYFS_GROUP_NAME_MAX_LEN + 1];
    struct fuse_operations operations;
    int fargc, ret = MYFS_RETURN_SUCCESS;
    char **fargv;
    bool success;
    myfs_t myfs;

    mysql_library_init(0, NULL, NULL);
    log_init();
    config_init();
    reclaimer_init();

    memset(&myfs, 0, sizeof(myfs));
    util_username(getuid(), user, sizeof(user));
    util_groupname(getgid(), group, sizeof(group));

    config_set_error_func(config_error);
    config_set_description("%s v%d.%d.%d", VERSION_NAME, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    //Set default config options.
    config_set_default("config_file",                   "--config-file",                NULL,                        "/etc/myfs.d/myfs.conf",   NULL,                            "The MariaDB database name.");
    config_set_default_bool("create",                   "--create",                     NULL,                        false,                     config_handle_create,            "Runs the process to create a new MyFS database and exits.");
    config_set_default_int("failed_query_retry_wait",   "--failed-query-retry-wait",    "failed_query_retry_wait",   -1,                        NULL,                            "Number of seconds to wait before retrying a failed query. -1 means do not retry.");
    config_set_default_int("failed_query_retry_count",  "--failed-query-retry-count",   "failed_query_retry_count",  -1,                        NULL,                            "The total number of failed queries to retry. If `retry_wait` is -1, this option is ignored. -1 means retry forever.");
    config_set_default("group",                         "--group",                      "group",                     group,                     NULL,                            "The Linux group to create files and directories with. If blank, the current group will be used.");
    config_set_default_bool("log_stdout",               "--log-stdout",                 "log_stdout",                true,                      config_handle_log_stdout,        "Whether or not to log to stdout.");
    config_set_default_bool("log_syslog",               "--log-syslog",                 "log_syslog",                false,                     config_handle_log_syslog,        "Whether or not to log to syslog.");
    config_set_default("mariadb_database",              "--mariadb-database",           "mariadb_database",          "myfs",                    NULL,                            "The MariaDB database name.");
    config_set_default("mariadb_host",                  "--mariadb-host",               "mariadb_host",              "127.0.0.1",               NULL,                            "The MariaDB IP address or hostname.");
    config_set_default("mariadb_password",              "--mariadb-password",           "mariadb_password",          NULL,                      NULL,                            "The MariaDB user's password.");
    config_set_default("mariadb_port",                  "--mariadb-port",               "mariadb_port",              "3306",                    NULL,                            "The MariaDB port.");
    config_set_default("mariadb_user",                  "--mariadb-user",               "mariadb_user",              "myfs",                    NULL,                            "The MariaDB user.");
    config_set_default("mount",                         "--mount",                      "mount",                     "/mnt/myfs",               NULL,                            "The mount point for the file system.");
    config_set_default_bool("print_create_sql",         "--print-create-sql",           NULL,                        false,                     config_handle_print_create_sql,  "Prints the SQL statements needed to create a MyFS database and exits.");
    config_set_default("user",                          "--user",                       "user",                      user,                      NULL,                            "The Linux user to create files and directories with. If blank, the current user will be used.");

    //These command line configs should be parsed before the config file.
    config_set_priority("config_file");
    config_set_priority("create");
    config_set_priority("print_create_sql");

    success = config_read_command_line(argc, argv, true) &&
              config_read_file(config_get("config_file")) &&
              config_read_command_line(argc, argv, false) &&
              check_config();

    if (!success) {
        ret = MYFS_RETURN_CONFIG;
        goto done;
    }

    log_info(MODULE, "Starting %s v%d.%.d.%d", VERSION_NAME, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    success = confirm_config();
    if (!success) {
        ret = MYFS_RETURN_CONFIG;
        goto done;
    }

    if (ret == 0) {
        if (!myfs_connect(&myfs)) {
            ret = MYFS_RETURN_DATABASE;
            goto done;
        }
    }

    if (ret == 0) {
        memset(&operations, 0, sizeof(operations));
        //operations.init = myfs_init;
        //operations.destroy = myfs_destroy;
        operations.statfs = myfs_statfs;
        operations.getattr = myfs_getattr;
        operations.access = myfs_access;
        //setxattr
        operations.truncate = myfs_truncate;
        operations.utimens = myfs_utimens;
        operations.chown = myfs_chown;
        operations.chmod = myfs_chmod;
        operations.opendir = myfs_opendir;
        operations.releasedir = myfs_releasedir;
        operations.readdir = myfs_readdir;
        operations.unlink = myfs_unlink;
        operations.rmdir = myfs_rmdir;
        operations.mkdir = myfs_mkdir;
        operations.create = myfs_create;
        operations.flush = myfs_flush;
        operations.open = myfs_open;
        operations.release = myfs_release;
        operations.read = myfs_read;
        operations.write = myfs_write;
        operations.rename = myfs_rename;
        operations.symlink = myfs_symlink;
        operations.readlink = myfs_readlink;

        //Since MyFS has its own command line arguments, create a new argc/argv duo for FUSE. If we don't,
        //FUSE will choke on MyFS's command line arguments. Likewise, MyFS will choke on FUSE arguments.
        //all we really care about is -f <mount point>
        fargv = fargs_get(argv[0], &fargc);

        log_info(MODULE, "Running");
        ret = fuse_main(fargc, fargv, &operations, &myfs);

        fargs_free(fargc, fargv);

        log_info(MODULE, "Goodbye");
    }

done:
    myfs_disconnect(&myfs);

    reclaimer_free();
    config_free();
    log_free();
    mysql_library_end();

    return ret;
}
