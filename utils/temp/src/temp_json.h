#ifndef TEMP_JSON_H
#define TEMP_JSON_H
#include <stddef.h>

/* Small strict JSON reader for manifests. Owns its tree, rejects duplicate
 * object keys, embedded NUL strings and nesting deeper than 64. */
struct temp_json {
    char type; /* o object, a array, s string, n number, t/f boolean, z null */
    char *name, *text;
    struct temp_json *child, *next;
};
struct temp_json *temp_json_read(const char *path, char *error, size_t size);
const struct temp_json *temp_json_get(const struct temp_json *, const char *name);
void temp_json_free(struct temp_json *);
#endif
