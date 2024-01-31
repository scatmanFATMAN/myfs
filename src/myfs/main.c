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
    config_set_default("mariadb_database", "--mariadb_database", "mariadb_database", "myfs",      "The MariaDB database name.");
    config_set_default("mariadb_host",     "--mariadb_host",     "mariadb_host",     "127.0.0.1", "The MariaDB IP address or hostname.");
    config_set_default("mariadb_password", "--mariadb_password", "mariadb_password", NULL,        "The MariaDB user's password.");
    config_set_default("mariadb_port",     "--mariadb_port",     "mariadb_port",     "3306",      "The MariaDB port.");
    config_set_default("mariadb_user",     "--mariadb_user",     "mariadb_user",     "myfs",      "The MariaDB user.");
    config_set_default("mount",            "--mount",            "mount",            "/mnt/myfs", "The mount point for the file system.");

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
    }

done:
    myfs_disconnect(&myfs);

    mysql_library_end();
    config_free();
    log_free();

    return ret;
}
