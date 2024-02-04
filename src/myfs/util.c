#include <stdlib.h>
#include <libgen.h>
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
