#ifndef TEMP_MANIFEST_H
#define TEMP_MANIFEST_H
#include "temp_image.h"

struct temp_manifest {
    struct temp_layout layout;
    struct temp_records records;
    struct temp_order order;
    struct temp_image_shape shape;
    struct temp_image_constraints constraints;
    _Bool has_shape;
    void *storage;
};
/* Zero initialize before load. Descriptors borrow owned storage until free.
 * Library override paths are relative to the working directory; manifest
 * library paths are relative to the manifest's directory. */
int temp_manifest_load(const char *path, const char *library_override,
                       struct temp_manifest *, char *error, size_t size);
void temp_manifest_free(struct temp_manifest *);
const char *temp_manifest_comparator_id(const struct temp_manifest *);
int temp_manifest_cli(int argc, char **argv, _Bool generate);
#endif
