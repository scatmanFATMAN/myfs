#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
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
    char mariadb_database[128];
    char mariadb_port[8];
    char mount[1024];
    db_t db;
    bool config_created;
    bool database_created;
} create_params_t;

static void
create_prompt_helper(char *dst, int size, const char *fmt, va_list ap, bool no_echo) {
    struct termios t;
    char *ptr;

    vprintf(fmt, ap);

    printf(": ");
    fflush(stdout);

    //Turn off echo'ing to the console if needed.
    if (no_echo) {
        tcgetattr(STDIN_FILENO, &t);
        t.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }

    fgets(dst, size, stdin);

    //Turn back on echo'ing to the console if needed.
    if (no_echo) {
        tcgetattr(STDIN_FILENO, &t);
        t.c_lflag |= ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        printf("\n");
    }

    //Remove the newline.
    ptr = strchr(dst, '\n');
    if (ptr != NULL) {
        *ptr = '\0';
    }
}

static void
create_prompt(char *dst, int size, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    create_prompt_helper(dst, size, fmt, ap, false);
    va_end(ap);
}

static void
create_prompt_password(char *dst, int size, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    create_prompt_helper(dst, size, fmt, ap, true);
    va_end(ap);
}

static bool
create_run_prompt(create_params_t *params) {
    char input[2048];

    printf("Welcome to the MyFS utility to create and initialize a MyFS instance.\n");
    printf("\n");
    printf("You'll be prompted to enter a file path to put the config file, database credentials for a super user that can create a database, and database credentials for the MyFS database. The database host and port will be the same for both set of credentials.\n");
    printf("\n");
    printf("For each prompt, a default value is given in brackets and may be used by simply pressing 'Enter'. Passwords do not have a default value. For password prompts, you will not see the characters you type but the password is being captured.\n");
    printf("\n");

    //Prompt the user for the config file location
    while (true) {
        create_prompt(input, sizeof(input), "Config file [%s]", params->config_path);

        if (input[0] != '\0') {
            if (!str_ends_with(input, ".conf")) {
                printf("  Config file must end with .conf\n");
                continue;
            }

            strlcpy(params->config_path, input, sizeof(params->config_path));
            printf("  Config file path changed to %s.\n", params->config_path);
        }

        //Change or 'Enter' which means use the default.
        break;
    }

    //Prompt the user for MariaDB credentials to create the database
    create_prompt(input, sizeof(input), "MariaDB host[%s]", params->mariadb_host);
    if (input[0] != '\0') {
        strlcpy(params->mariadb_host, input, sizeof(params->mariadb_host));
        printf("  MariaDB host changed to %s.\n", params->mariadb_host);
    }

    create_prompt(input, sizeof(input), "MariaDB port[%s]", params->mariadb_port);
    if (input[0] != '\0') {
        strlcpy(params->mariadb_port, input, sizeof(params->mariadb_port));
        printf("  MariaDB port changed to %s.\n", params->mariadb_port);
    }

    create_prompt(input, sizeof(input), "MariaDB super user[%s]", params->mariadb_user_root);
    if (input[0] != '\0') {
        strlcpy(params->mariadb_user_root, input, sizeof(params->mariadb_user_root));
        printf("  MariaDB super user changed to %s.\n", params->mariadb_user_root);
    }

    while (params->mariadb_password_root[0] == '\0') {
        create_prompt_password(input, sizeof(input), "MariaDB super user password");
        strlcpy(params->mariadb_password_root, input, sizeof(params->mariadb_password_root));
    }
    printf("  MariaDB super user password accepted.\n");

    create_prompt(input, sizeof(input), "MariaDB MyFS user[%s]", params->mariadb_user);
    if (input[0] != '\0') {
        strlcpy(params->mariadb_user, input, sizeof(params->mariadb_user));
        printf("  MariaDB MyFS user changed to %s.\n", params->mariadb_user);
    }

    while (true) {
        input[0] = '\0';
        while (input[0] == '\0') {
            create_prompt_password(input, sizeof(input), "MariaDB MyFS user password");
            strlcpy(params->mariadb_password1, input, sizeof(params->mariadb_password1));
        }

        input[0] = '\0';
        while (input[0] == '\0') {
            create_prompt_password(input, sizeof(input), "Confirm MariaDB MyFS user password");
            strlcpy(params->mariadb_password2, input, sizeof(params->mariadb_password2));
        }

        if (strcmp(params->mariadb_password1, params->mariadb_password2) != 0) {
            printf("  Passwords do not match, try again.\n");
            continue;
        }

        break;
    }
    printf("  MariaDB user password accepted.\n");

    create_prompt(input, sizeof(input), "MariaDB MyFS database[%s]", params->mariadb_database);
    if (input[0] != '\0') {
        strlcpy(params->mariadb_database, input, sizeof(params->mariadb_database));
        printf("  MariaDB MyFS database changed to %s.\n", params->mariadb_database);
    }

    printf("\n");
    printf("Double check the settings below:\n");
    printf("The config file will created at %s.\n", params->config_path);
    printf("The MariaDB super user used to create the database and tables is %s@%s:%s.\n", params->mariadb_user_root, params->mariadb_host, params->mariadb_port);
    printf("The MariaDB MyFS user and database is %s@%s:%s/%s.\n", params->mariadb_user, params->mariadb_host, params->mariadb_port, params->mariadb_database);
    printf("\n");

    input[0] = '\0';
    while (input[0] == '\0') {
        create_prompt(input, sizeof(input), "Do you wish to continue[y/n]?");
    }

    return strcmp(input, "y") == 0;
}

static bool
create_run_validate(create_params_t *params) {
    char dir[1024 - 128];
    MYSQL_RES *res;
    MYSQL_ROW row;
    bool success;

    printf("\n");
    printf("Running validation checks.\n");
    printf("Checking config file %s.\n", params->config_path);

    util_dirname(params->config_path, dir, sizeof(dir));

    if (access(dir, W_OK) != 0) {
        printf("  %s is not writable: %s\n", dir, strerror(errno));
        return false;
    }

    if (access(params->config_path, F_OK) == 0) {
        printf("  %s already exists.\n", params->config_path);
        return false;
    }

    printf("  Config file is good.\n");
    printf("Connecting to MariaDB at %s@%s:%s.\n", params->mariadb_user_root, params->mariadb_host, params->mariadb_port);

    success = db_connect(&params->db, params->mariadb_host, params->mariadb_user_root, params->mariadb_password_root, NULL, strtoul(params->mariadb_port, NULL, 10));
    if (!success) {
        printf("  Error connecting to MariaDB: %s.\n", db_error(&params->db));
        return false;
    }

    printf("  MariaDB connection is good.\n");
    printf("Checking to make sure database '%s' does not exist.\n", params->mariadb_database);

    res = db_selectf(&params->db, "SHOW DATABASES LIKE '%s'\n",
                                  params->mariadb_database);
    if (res == NULL) {
        printf("  Error checking database: %s\n", db_error(&params->db));
        return false;
    }

    row = mysql_fetch_row(res);
    if (row != NULL) {
        printf("  That database already exists.\n");
        success = false;
    }
    else {
        printf("  The database does not exist.\n");
        success = true;
    }

    mysql_free_result(res);
    return success;
}

static bool
create_run_create_config(create_params_t *params) {
    FILE *f;

    printf("Creating %s\n", params->config_path);

    f = fopen(params->config_path, "w");
    if (f == NULL) {
        printf("  Error opening %s for writing: %s\n", params->config_path, strerror(errno));
        return false;
    }

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
    bool success;

    printf("Creating database '%s'\n", params->mariadb_database);

    //Create the database.
    success = db_queryf(&params->db, "CREATE DATABASE `%s`", params->mariadb_database);
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
    success = db_queryf(&params->db, "CREATE TABLE `files` (\n"
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
                                     ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci");

    if (!success) {
        printf("  Error creating table 'files': %s\n", db_error(&params->db));
        return false;
    }

    //Create the users.
    printf("Creating database user '%s'\n", params->mariadb_user);

    success = db_queryf(&params->db, "CREATE USER '%s'@'%s' IDENTIFIED BY '%s'", params->mariadb_user, params->mariadb_host, params->mariadb_password1);
    if (!success) {
        printf("  Error creating user '%s': %s\n", params->mariadb_user, db_error(&params->db));
        return false;
    }

    //TODO: need the IP of where its connecting from
    success = db_queryf(&params->db, "GRANT USAGE ON `%s`.* TO '%s'@'%s'", params->mariadb_database, params->mariadb_user, params->mariadb_host);
    if (!success) {
        printf("  Error granting usage to user '%s': %s\n", params->mariadb_user, db_error(&params->db));
        return false;
    }

    success = db_queryf(&params->db, "GRANT ALL PRIVILEGES ON `%s`.* TO '%s'@'%s' WITH GRANT OPTION", params->mariadb_database, params->mariadb_user, params->mariadb_host);
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
    strcpy(params.mariadb_database, config_get("mariadb_database"));
    strcpy(params.mariadb_port, config_get("mariadb_port"));

    success = create_run_prompt(&params) &&
              create_run_validate(&params) &&
              create_run_create_config(&params) &&
              create_run_create_database(&params);

    create_cleanup(&params, success);

    if (success) {
        printf("Config file and database installed!\n");
    }
}
