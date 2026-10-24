#include "temp_compare.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int temp_comparator_open(struct temp_comparator_module *module,
                         const char *path, uint16_t key_size,
                         const char *required_id, char *error, size_t error_size)
{
    const struct temp_comparator_descriptor_v1 *(*entry)(void);
    const char *reason;
    const struct temp_comparator_descriptor_v1 *d;
    if (module == NULL || module->handle != NULL || path == NULL) {
        snprintf(error, error_size, "invalid comparator loader arguments");
        return -1;
    }
    module->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (module->handle == NULL) {
        snprintf(error, error_size, "cannot load comparator: %s", dlerror());
        return -1;
    }
    dlerror();
    /* POSIX specifies conversion of a dlsym result to a function pointer. */
    entry = (const struct temp_comparator_descriptor_v1 *(*)(void))
        dlsym(module->handle, "temp_comparator_v1");
    reason = dlerror();
    if (reason != NULL || entry == NULL) {
        snprintf(error, error_size, "missing temp_comparator_v1: %s",
                 reason != NULL ? reason : "null entry point");
        goto fail;
    }
    d = entry();
    if (d == NULL || d->abi_version != TEMP_COMPARATOR_ABI_V1 ||
        d->struct_size < sizeof *d) {
        snprintf(error, error_size, "incompatible comparator ABI");
        goto fail;
    }
    if (d->compare == NULL || d->id == NULL || d->id[0] == '\0' ||
        d->reserved != 0 || d->key_size != key_size) {
        snprintf(error, error_size, "invalid comparator descriptor or key size");
        goto fail;
    }
    if (required_id != NULL && strcmp(required_id, d->id) != 0) {
        snprintf(error, error_size, "comparator ID mismatch: expected %s, got %s",
                 required_id, d->id);
        goto fail;
    }
    module->descriptor = d;
    return 0;
fail:
    temp_comparator_close(module);
    return -1;
}

void temp_comparator_close(struct temp_comparator_module *module)
{
    if (module == NULL) return;
    if (module->handle != NULL) dlclose(module->handle);
    memset(module, 0, sizeof *module);
}
