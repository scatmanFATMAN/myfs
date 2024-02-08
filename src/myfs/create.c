#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../common/config.h"
#include "../common/db.h"
#include "../common/string.h"
#include "util.h"
#include "create.h"

typedef struct {
    char config_path[1024];
    char mariadb_host[512];
    char mariadb_user_root[128];
    char mariadb_user[128];
    char mariadb_password_root[128];
    char mariadb_password1[128];
    char mariadb_password2[128];
    char mariadb_user_host[512];
    char mariadb_database[128];
    char mariadb_port[8];
    char mount[1024];
    db_t db;
    bool create_database_user;
    bool config_created;
    bool database_created;
} create_params_t;

static bool
create_run_prompt(create_params_t *params) {
    char input[2048], dir[1024 - 128];
    bool success, exists;
    struct stat st;

    printf("Welcome to the MyFS utility to create and initialize a MyFS instance.\n");
    printf("\n");
    printf("You'll be prompted to enter a file path to put the config file, database credentials for a super user that can create a database, and database credentials for the MyFS database. The database host and port will be the same for both set of credentials.\n");
    printf("\n");
    printf("For each prompt, a default value is given in brackets and may be used by simply pressing 'Enter'. Passwords do not have a default value. For password prompts, you will not see the characters you type but the password is being captured.\n");

    //Prompt the user for the config file location
    while (true) {
        printf("\n");
        util_create_prompt(input, sizeof(input), "Config file [%s]", params->config_path);

        if (input[0] != '\0') {
            if (!str_ends_with(input, ".conf")) {
                printf("  Config file must end with .conf\n");
                continue;
            }

            strlcpy(params->config_path, input, sizeof(params->config_path));
            printf("  Config file path changed to %s.\n", params->config_path);
        }

        //Make sure the file doesn't already exist.
        if (access(params->config_path, F_OK) == 0) {
            printf("  %s already exists.\n", params->config_path);
            continue;
        }

        //Make sure the directory is writable.
        util_dirname(params->config_path, dir, sizeof(dir));
        if (access(dir, W_OK) != 0) {
            printf("  %s is not writable: %s.\n", dir, strerror(errno));
            continue;
        }

        break;
    }

    //Prompt for the mount point.
    while (true) {
        printf("\n");
        util_create_prompt(input, sizeof(input), "Mount Point [%s]", params->mount);
        if (input[0] != '\0') {
            strlcpy(params->mount, input, sizeof(params->mount));
            printf("  Mount changed to '%s'.\n", params->mount);
        }

        printf("\n");
        printf("Checking to see if '%s' exists.\n", params->mount);
        if (stat(params->mount, &st) == 0) {
            printf("  Mount point already exists.\n");

            if (!S_ISDIR(st.st_mode)) {
                printf("  Mount point is not a directory.\n");
                continue;
            }
        }
        else {
            printf("  Mount point does not exist, creating it.\n");
            if (mkdir(params->mount, 0750) != 0) {
                printf("  Error creating mount point: %s.\n", strerror(errno));
                continue;
            }

            printf("  Mount point created.\n");
        }

        break;
    }

    //Prompt the user for MariaDB credentials to create the database
    while (true) {
        printf("\n");
        util_create_prompt(input, sizeof(input), "MariaDB host[%s]", params->mariadb_host);
        if (input[0] != '\0') {
            strlcpy(params->mariadb_host, input, sizeof(params->mariadb_host));
            printf("  MariaDB host changed to %s.\n", params->mariadb_host);
        }

        printf("\n");
        util_create_prompt(input, sizeof(input), "MariaDB port[%s]", params->mariadb_port);
        if (input[0] != '\0') {
            strlcpy(params->mariadb_port, input, sizeof(params->mariadb_port));
            printf("  MariaDB port changed to %s.\n", params->mariadb_port);
        }

        printf("\n");
        util_create_prompt(input, sizeof(input), "MariaDB super user[%s]", params->mariadb_user_root);
        if (input[0] != '\0') {
            strlcpy(params->mariadb_user_root, input, sizeof(params->mariadb_user_root));
            printf("  MariaDB super user changed to %s.\n", params->mariadb_user_root);
        }

        params->mariadb_password_root[0] = '\0';
        printf("\n");
        while (params->mariadb_password_root[0] == '\0') {
            util_create_prompt_password(input, sizeof(input), "MariaDB super user password");
            strlcpy(params->mariadb_password_root, input, sizeof(params->mariadb_password_root));
        }
        printf("  MariaDB super user password accepted.\n");

        printf("\n");
        printf("Connecting to MariaDB at %s@%s:%s.\n", params->mariadb_user_root, params->mariadb_host, params->mariadb_port);
        success = db_connect(&params->db, params->mariadb_host, params->mariadb_user_root, params->mariadb_password_root, NULL, strtoul(params->mariadb_port, NULL, 10));
        if (!success) {
            printf("  Error connecting to MariaDB: %s.\n", db_error(&params->db));
            continue;
        }

        printf("  Connected.\n");
        break;
    }

    while (true) {
        //Ask if we need to create a special MariaDB user for the new database. It's perfectly fine to use an existing user.
        params->create_database_user = false;
        printf("\n");
        util_create_prompt(input, sizeof(input), "Do you need to create a new MariaDB user for MyFS[y/n]?");
        if (strcmp(input, "y") == 0) {
            params->create_database_user = true;
        }

        printf("\n");
        util_create_prompt(input, sizeof(input), "MariaDB MyFS user[%s]", params->mariadb_user);
        if (input[0] != '\0') {
            strlcpy(params->mariadb_user, input, sizeof(params->mariadb_user));
            printf("  MariaDB MyFS user changed to %s.\n", params->mariadb_user);
        }

        printf("\n");
        util_create_prompt(input, sizeof(input), "Host that you'll be connecting to MariaDB from[%s]", params->mariadb_user_host);
        if (input[0] != '\0') {
            strlcpy(params->mariadb_user_host, input, sizeof(params->mariadb_user_host));
            printf("  MariaDB user host changed to '%s'", params->mariadb_user_host);
        }

        printf("\n");
        printf("Checking to see if database user '%s'@'%s' exists.\n", params->mariadb_user, params->mariadb_user_host);

        success = db_user_exists(&params->db, params->mariadb_user, params->mariadb_user_host, &exists);
        if (!success) {
            printf("  Error checking to see if database user exists: %s.\n", db_error(&params->db));
            return false;
        }

        if (params->create_database_user) {
            if (exists) {
                printf("  That database user already exists.\n");
                continue;
            }

            printf("  That database user does not exist.\n");
        }
        else {
            if (!exists) {
                printf("  That database user does not exist.\n");
                continue;
            }

            printf("  That database user exists.\n");
        }

        break;
    }

    if (params->create_database_user) {
        printf("\n");
        while (true) {
            input[0] = '\0';
            while (input[0] == '\0') {
                util_create_prompt_password(input, sizeof(input), "MariaDB MyFS user password");
                strlcpy(params->mariadb_password1, input, sizeof(params->mariadb_password1));
            }

            input[0] = '\0';
            while (input[0] == '\0') {
                util_create_prompt_password(input, sizeof(input), "Confirm MariaDB MyFS user password");
                strlcpy(params->mariadb_password2, input, sizeof(params->mariadb_password2));
            }

            if (strcmp(params->mariadb_password1, params->mariadb_password2) != 0) {
                printf("  Passwords do not match, try again.\n");
                continue;
            }

            break;
        }
        printf("  MariaDB user password accepted.\n");
    }

    printf("\n");
    while (true) {
        util_create_prompt(input, sizeof(input), "MariaDB MyFS database[%s]", params->mariadb_database);
        if (input[0] != '\0') {
            strlcpy(params->mariadb_database, input, sizeof(params->mariadb_database));
            printf("  MariaDB MyFS database changed to %s.\n", params->mariadb_database);
        }

        printf("\n");
        printf("Checking to see if database '%s' exists.\n", params->mariadb_database);
        success = db_database_exists(&params->db, params->mariadb_database, &exists);
        if (!success) {
            printf("  Error checking to see if the database exists: %s.\n", db_error(&params->db));
            return false;
        }

        if (exists) {
            printf("  That database already exists.\n");
            continue;
        }

        printf("  That database does not exist.\n");
        break;
    }

    printf("\n");
    printf("Double check the settings below:\n");
    printf("The config file will created at %s.\n", params->config_path);
    printf("The MariaDB super user used to create the database and tables is %s@%s:%s.\n", params->mariadb_user_root, params->mariadb_host, params->mariadb_port);
    printf("The MariaDB MyFS user and database is %s@%s:%s/%s.\n", params->mariadb_user, params->mariadb_host, params->mariadb_port, params->mariadb_database);
    printf("\n");

    input[0] = '\0';
    while (input[0] == '\0') {
        util_create_prompt(input, sizeof(input), "Do you wish to continue[y/n]?");
    }

    return strcmp(input, "y") == 0;
}

static bool
create_run_create_config(create_params_t *params) {
    FILE *f;

    printf("\n");
    printf("Creating %s\n", params->config_path);

    f = fopen(params->config_path, "w");
    if (f == NULL) {
        printf("  Error opening %s for writing: %s\n", params->config_path, strerror(errno));
        return false;
    }

    fprintf(f, "Number of seconds to wait before re-trying a failed query. -1 means do not retry.\n");
    fprintf(f, "failed_query_retry_wait = %d\n", config_get_int("failed_query_retry_wait"));
    fprintf(f, "\n");
    fprintf(f, "# The total number of failed queries to retry. -1 means do not retry. 0 means retry forever.\n");
    fprintf(f, "failed_query_retry_count = %d\n", config_get_int("failed_query_retry_count"));
    fprintf(f, "\n");
    fprintf(f, "# Whether or not to log to the console.\n");
    fprintf(f, "log_stdout = true\n");
    fprintf(f, "\n");
    fprintf(f, "# Whether or not to log to syslog.\n");
    fprintf(f, "log_syslog = false\n");
    fprintf(f, "\n");
    fprintf(f, "# The MariaDB database name.\n");
    fprintf(f, "mariadb_database = %s\n", params->mariadb_database);
    fprintf(f, "\n");
    fprintf(f, "# The MariaDB IP address or hostname.\n");
    fprintf(f, "mariadb_host = %s\n", params->mariadb_host);
    fprintf(f, "\n");
    fprintf(f, "# The MariaDB user's password.\n");
    fprintf(f, "mariadb_password =\n");
    fprintf(f, "\n");
    fprintf(f, "# The MariaDB port.\n");
    fprintf(f, "mariadb_port = %s\n", params->mariadb_port);
    fprintf(f, "\n");
    fprintf(f, "# The MariaDB user.\n");
    fprintf(f, "mariadb_user = %s\n", params->mariadb_user);
    fprintf(f, "\n");
    fprintf(f, "# The mount point for the file system.\n");
    fprintf(f, "mount = %s\n", params->mount);
    fclose(f);

    params->config_created = true;

    return true;
}

static bool
create_run_create_database(create_params_t *params) {
    char sql[2048];
    bool success;

    printf("\n");
    printf("Creating database '%s'\n", params->mariadb_database);

    //Create the database.
    create_get_sql_database(sql, sizeof(sql), params->mariadb_database);
    success = db_queryf(&params->db, "%s", sql);
    if (!success) {
        printf("  Error creating database '%s': %s\n", params->mariadb_database, db_error(&params->db));
        return false;
    }

    params->database_created = true;

    //Select the database now.
    printf("Creating database tables\n");

    success = db_queryf(&params->db, "USE `%s`", params->mariadb_database);
    if (!success) {
        printf("  Error selecting database '%s': %s", params->mariadb_database, db_error(&params->db));
        return false;
    }

    //Create the `files` table.
    create_get_sql_database_table(sql, sizeof(sql));
    success = db_queryf(&params->db, "%s", sql);
    if (!success) {
        printf("  Error creating table 'files': %s\n", db_error(&params->db));
        return false;
    }

    //Create the users.
    if (params->create_database_user) {
        printf("Creating database user '%s'\n", params->mariadb_user);

        create_get_sql_database_user_create(sql, sizeof(sql), params->mariadb_user, params->mariadb_user_host, params->mariadb_password1);
        success = db_queryf(&params->db, "%s", sql);
        if (!success) {
            printf("  Error creating user '%s': %s\n", params->mariadb_user, db_error(&params->db));
            return false;
        }
    }

    printf("Granting privileges to database user '%s'\n", params->mariadb_user);

    create_get_sql_database_user_grant1(sql, sizeof(sql), params->mariadb_user, params->mariadb_host, params->mariadb_database);
    success = db_queryf(&params->db, "%s", sql);
    if (!success) {
        printf("  Error granting usage to user '%s': %s\n", params->mariadb_user, db_error(&params->db));
        return false;
    }

    create_get_sql_database_user_grant2(sql, sizeof(sql), params->mariadb_user, params->mariadb_host, params->mariadb_database);
    success = db_queryf(&params->db, "%s", sql);
    if (!success) {
        printf("  Error granting privileges to user '%s': %s\n", params->mariadb_user, db_error(&params->db));
        return false;
    }

    success = db_queryf(&params->db, "FLUSH PRIVILEGES");
    if (!success) {
        printf("  Error flushing privileges: %s\n", db_error(&params->db));
        printf("  You'll need to do this manually\n");
    }

    return true;
}

static void
create_cleanup(create_params_t *params, bool success) {
    if (!success) {
        //Do not  remove the config file if the user tried to create one where a file already existed!
        if (params->config_created) {
            if (unlink(params->config_path) != 0) {
                printf("  Error deleting config file %s: %s\n", params->config_path, strerror(errno));
            }
        }

        if (params->database_created) {
            if (!db_queryf(&params->db, "DROP DATABASE '%s'", params->mariadb_database)) {
                printf("  Error dropping database '%s': %s\n", params->mariadb_database, db_error(&params->db));
            }
        }
    }

    db_disconnect(&params->db);
}

void
create_run() {
    create_params_t params;
    bool success;

    memset(&params, 0, sizeof(params));
    strcpy(params.config_path, "/etc/myfs.d/myfs.conf");
    strcpy(params.mariadb_host, config_get("mariadb_host"));
    strcpy(params.mariadb_user_root, "root");
    strcpy(params.mariadb_user, config_get("mariadb_user"));
    strcpy(params.mariadb_user_host, "%");
    strcpy(params.mariadb_database, config_get("mariadb_database"));
    strcpy(params.mariadb_port, config_get("mariadb_port"));
    strcpy(params.mount, config_get("mount"));

    success = create_run_prompt(&params) &&
              create_run_create_config(&params) &&
              create_run_create_database(&params);

    create_cleanup(&params, success);

    if (success) {
        printf("\n");
        printf("MyFS has been setup.\n");
    }
}

void
create_get_sql_database(char *dst, size_t size, const char *name) {
    snprintf(dst, size, "CREATE DATABASE `%s`;", name);
}

void
create_get_sql_database_table(char *dst, size_t size) {
    strlcpy(dst, "CREATE TABLE `files` (\n"
                 "    `file_id` int(10) unsigned NOT NULL AUTO_INCREMENT,\n"
                 "    `parent_id` int(10) unsigned NOT NULL,\n"
                 "    `name` varchar(64) NOT NULL,\n"
                 "    `type` enum('File','Directory','Soft Link') NOT NULL,\n"
                 "    `content` longblob NOT NULL,\n"
                 "    `created_on` int(10) unsigned NOT NULL,\n"
                 "    `last_accessed_on` int(10) unsigned NOT NULL,\n"
                 "    `last_modified_on` int(10) unsigned NOT NULL,\n"
                 "    `last_status_changed_on` int(10) unsigned NOT NULL,\n"
                 "    PRIMARY KEY (`file_id`),\n"
                 "    KEY `fk_files_parentid` (`parent_id`),\n"
                 "    CONSTRAINT `fk_files_parentid` FOREIGN KEY (`parent_id`) REFERENCES `files` (`file_id`) ON DELETE CASCADE ON UPDATE CASCADE\n"
                 ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;", size);
}

void
create_get_sql_database_user_create(char *dst, size_t size, const char *user, const char *host, const char *password) {
    snprintf(dst, size, "CREATE USER '%s'@'%s' IDENTIFIED BY '%s';", user, host, password);
}

void
create_get_sql_database_user_grant1(char *dst, size_t size, const char *user, const char *host, const char *database) {
    snprintf(dst, size, "GRANT USAGE ON `%s`.* TO '%s'@'%s';", database, user, host);
}

void
create_get_sql_database_user_grant2(char *dst, size_t size, const char *user, const char *host, const char *database) {
    snprintf(dst, size, "GRANT ALL PRIVILEGES ON `%s`.* TO '%s'@'%s' WITH GRANT OPTION;", database, user, host);
}
