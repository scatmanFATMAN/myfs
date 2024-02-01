#include <string.h>
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include "../common/log.h"
#include "../common/config.h"
#include "../common/db.h"
#include "myfs.h"
#include "version.h"

#define MODULE "Main"

static void
config_error(const char *message) {
    log_err(MODULE, "%s", message);
}

static bool
config_handle_log_to_stdout(const char *name, const char *value) {
    if (strcmp(value, "true") == 0) {
        log_to_stdout(true);
        config_set_bool(name, true);
    }
    else {
        log_to_stdout(false);
        config_set_bool(name, false);
    }

    return true;
}

static bool
config_handle_log_to_syslog(const char *name, const char *value) {
    if (strcmp(value, "true") == 0) {
        log_to_syslog(VERSION_NAME);
        config_set_bool(name, true);
    }
    else {
        log_to_syslog(NULL);
        config_set_bool(name, false);
    }

    return true;
}

int
main(int argc, char **argv) {
    struct fuse_operations operations;
    int ret = 0;
    myfs_t myfs;

    log_init();
    config_init();
    mysql_library_init(0, NULL, NULL);

    memset(&myfs, 0, sizeof(myfs));

    config_set_error_func(config_error);

    //Set default config options.
    config_set_default_bool("log_to_stdout", "--log-to-stdout",    "log_to_stdout",    true,        config_handle_log_to_stdout, "Whether or not to log to stdout.");
    config_set_default_bool("log_to_syslog", "--log-to-syslog",    "log_to_syslog",    false,       config_handle_log_to_syslog, "Whether or not to log to syslog.");
    config_set_default("mariadb_database",   "--mariadb-database", "mariadb_database", "myfs",      NULL,                        "The MariaDB database name.");
    config_set_default("mariadb_database",   "--mariadb-database", "mariadb_database", "myfs",      NULL,                        "The MariaDB database name.");
    config_set_default("mariadb_host",       "--mariadb-host",     "mariadb_host",     "127.0.0.1", NULL,                        "The MariaDB IP address or hostname.");
    config_set_default("mariadb_password",   "--mariadb-password", "mariadb_password", NULL,        NULL,                        "The MariaDB user's password.");
    config_set_default("mariadb_port",       "--mariadb-port",     "mariadb_port",     "3306",      NULL,                        "The MariaDB port.");
    config_set_default("mariadb_user",       "--mariadb-user",     "mariadb_user",     "myfs",      NULL,                        "The MariaDB user.");
    config_set_default("mount",              "--mount",            "mount",            "/mnt/myfs", NULL,                        "The mount point for the file system.");

    if (!config_read(argc, argv, "/etc/myfs.d/myfs.conf")) {
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

        ret = fuse_main(argc, argv, &operations, &myfs);

        log_info(MODULE, "Goodbye");
    }

done:
    myfs_disconnect(&myfs);

    mysql_library_end();
    config_free();
    log_free();

    return ret;
}
