#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>
#include <termios.h>
#include "../common/string.h"
#include "util.h"

const char *
util_basename(const char *path, char *dst, size_t size) {
    char *path_dupe, *name;

    //Duplicate path since banename() modifies the argument
    path_dupe = strdup(path);

    //Return a pointer to the modified name.
    name = basename(path_dupe);

    //Copy into the return buffer.
    strlcpy(dst, name, size);

    //Free the duplicated path.
    free(path_dupe);

    return dst;
}

const char *
util_dirname(const char *path, char *dst, size_t size) {
    char *path_dupe, *dir;

    //Duplicate path since dirname() modifies the argument
    path_dupe = strdup(path);

    //Return a pointer to the modified path (eg. a \0 is added at the last '/').
    dir = dirname(path_dupe);

    //Copy into the return buffer.
    strlcpy(dst, dir, size);

    //Free the duplicated path.
    free(path_dupe);

    return dst;
}

int
util_username(uid_t uid, char *dst, size_t size) {
    struct passwd pwd, *result;
    char unused[1024];
    int ret;

    ret = getpwuid_r(uid, &pwd, unused, sizeof(unused), &result);

    if (result == NULL) {
        if (ret == 0) {
            //Not found.
            return ENOENT;
        }

        return ret;
    }

    strlcpy(dst, pwd.pw_name, size);
    return 0;
}

int
util_user_id(const char *name, uid_t *uid) {
    struct passwd pwd, *result;
    char unused[1024];
    int ret;

    ret = getpwnam_r(name, &pwd, unused, sizeof(unused), &result);

    if (result == NULL) {
        if (ret == 0) {
            //Not found.
            return ENOENT;
        }

        return ret;
    }

    *uid = pwd.pw_uid;
    return 0;
}

int
util_groupname(gid_t gid, char *dst, size_t size) {
    struct group grp, *result;
    char unused[1024];
    int ret;

    ret = getgrgid_r(gid, &grp, unused, sizeof(unused), &result);

    if (result == NULL) {
        if (ret == 0) {
            //Not found.
            return ENOENT;
        }

        return ret;
    }

    strlcpy(dst, grp.gr_name, size);
    return 0;
}

int
util_group_id(const char *name, gid_t *gid) {
    struct group grp, *result;
    char unused[1024];
    int ret;

    ret = getgrnam_r(name, &grp, unused, sizeof(unused), &result);

    if (result == NULL) {
        if (ret == 0) {
            //Not found.
            return ENOENT;
        }

        return ret;
    }

    *gid = grp.gr_gid;
    return 0;
}

static void
util_create_prompt_helper(char *dst, int size, const char *fmt, va_list ap, bool no_echo) {
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

    while (fgets(dst, size, stdin) == NULL) {
        ;
    }

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

void
util_create_prompt(char *dst, int size, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    util_create_prompt_helper(dst, size, fmt, ap, false);
    va_end(ap);
}

void
util_create_prompt_password(char *dst, int size, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    util_create_prompt_helper(dst, size, fmt, ap, true);
    va_end(ap);
}
