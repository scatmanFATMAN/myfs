#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include "../common/log.h"
#include "../common/config.h"
#include "../common/db.h"
#include "myfs.h"
#include "create.h"
#include "version.h"

#define MODULE "Main"

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
    printf("USE `%s`;\n", "<myfs_database>");
    create_get_sql_database_table(sql, sizeof(sql));
    printf("%s\n", sql);
    create_get_sql_database_user_create(sql, sizeof(sql), "<myfs_user>", "<myfs_user_host>", "<myfs_user_password>");
    printf("%s\n", sql);
    create_get_sql_database_user_grant1(sql, sizeof(sql), "<myfs_user>", "<myfs_user_host>", "<myfs_database>");
    printf("%s\n", sql);
    create_get_sql_database_user_grant2(sql, sizeof(sql), "<myfs_user>", "<myfs_user_host>", "<myfs_database>");
    printf("%s\n", sql);
    printf("FLUSH PRIVILEGES;\n");

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

int
main(int argc, char **argv) {
    struct fuse_operations operations;
    int i, fargc, ret = 0;
    char **fargv;
    bool success;
    myfs_t myfs;

    log_init();
    config_init();
    mysql_library_init(0, NULL, NULL);

    memset(&myfs, 0, sizeof(myfs));

    config_set_error_func(config_error);
    config_set_description("%s v%d.%d.%d", VERSION_NAME, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    //Set default config options.
    config_set_default("config_file",           "--config-file",      NULL,               "/etc/myfs.d/myfs.conf",   NULL,                            "The MariaDB database name.");
    config_set_default_bool("create",           "--create",           NULL,               false,                     config_handle_create,            "Runs the process to create a new MyFS database and exits.");
    config_set_default_bool("log_stdout",       "--log-stdout",       "log_stdout",       true,                      config_handle_log_stdout,        "Whether or not to log to stdout.");
    config_set_default_bool("log_syslog",       "--log-syslog",       "log_syslog",       false,                     config_handle_log_syslog,        "Whether or not to log to syslog.");
    config_set_default("mariadb_database",      "--mariadb-database", "mariadb_database", "myfs",                    NULL,                            "The MariaDB database name.");
    config_set_default("mariadb_host",          "--mariadb-host",     "mariadb_host",     "127.0.0.1",               NULL,                            "The MariaDB IP address or hostname.");
    config_set_default("mariadb_password",      "--mariadb-password", "mariadb_password", NULL,                      NULL,                            "The MariaDB user's password.");
    config_set_default("mariadb_port",          "--mariadb-port",     "mariadb_port",     "3306",                    NULL,                            "The MariaDB port.");
    config_set_default("mariadb_user",          "--mariadb-user",     "mariadb_user",     "myfs",                    NULL,                            "The MariaDB user.");
    config_set_default("mount",                 "--mount",            "mount",            "/mnt/myfs",               NULL,                            "The mount point for the file system.");
    config_set_default_bool("print_create_sql", "--print-create-sql", NULL,               false,                     config_handle_print_create_sql,  "Prints the SQL statements needed to create a MyFS database and exits.");

    //These command line configs should be parsed before the config file.
    config_set_priority("config_file");
    config_set_priority("create");
    config_set_priority("print_create_sql");

    success = config_read_command_line(argc, argv, true) &&
              config_read_file(config_get("config_file")) &&
              config_read_command_line(argc, argv, false);

    if (!success) {
        ret = 1;
        goto done;
    }

    log_info(MODULE, "Starting v%d.%.d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    if (ret == 0) {
        if (!myfs_connect(&myfs)) {
            ret = 2;
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

        //Only need 3 arguments max
        fargv = calloc(3, sizeof(char *));

        //Start with argv[0], the program name
        fargc = 1;
        fargv[0] = strdup(argv[0]);

        //If --mount was passed in, translate it to FUSE's -f
        if (config_has("mount")) {
            fargv[fargc++] = strdup("-f");
            fargv[fargc++] = config_dupe("mount");
        }

        ret = fuse_main(fargc, fargv, &operations, &myfs);

        for (i = 0; i < fargc; i++) {
            free(fargv[i]);
        }
        free(fargv);

        log_info(MODULE, "Goodbye");
    }

done:
    myfs_disconnect(&myfs);

    mysql_library_end();
    config_free();
    log_free();

    return ret;
}
