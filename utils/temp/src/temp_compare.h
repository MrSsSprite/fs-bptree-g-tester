#ifndef TEMP_COMPARE_H
#define TEMP_COMPARE_H

#include <stddef.h>
#include <stdint.h>

/* Public, native-process ABI. Plugins include this header and export the
 * function below with external C linkage. Keep the module open while any of
 * its pointers are in use. The callback has the same signature as bptree.h.
 * Negative/zero/positive mean before/equivalent/after, respectively. */
#define TEMP_COMPARATOR_ABI_V1 1u
struct temp_comparator_descriptor_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *id;
    uint16_t key_size;
    uint16_t reserved;
    int (*compare)(const void *, const void *);
};

#ifdef __cplusplus
extern "C" {
#endif
const struct temp_comparator_descriptor_v1 *temp_comparator_v1(void);
#ifdef __cplusplus
}
#endif

struct temp_comparator_module {
    void *handle;
    const struct temp_comparator_descriptor_v1 *descriptor;
};

/* Outputs must be zero initialized. Loading never falls back to a builtin.
 * required_id may be NULL; key_size must match the descriptor. Returns 0 on
 * success, -1 with a diagnostic on failure. No core-library dependency. */
int temp_comparator_open(struct temp_comparator_module *module,
                         const char *path, uint16_t key_size,
                         const char *required_id, char *error, size_t error_size);
void temp_comparator_close(struct temp_comparator_module *module);

#endif
