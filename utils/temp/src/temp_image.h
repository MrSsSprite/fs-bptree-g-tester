#ifndef TEMP_IMAGE_H
#define TEMP_IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* Independent BPTR v0 native-byte-order image API; no core or Unity dependency.
 * All descriptors and callback data are borrowed for the duration of a call.
 * Records are fixed-width byte buffers, strictly ordered and comparator-unique.
 * get() writes exactly layout.key_size/value_size bytes and returns 0 on success.
 * Callbacks must be deterministic. compare may return any signed magnitude. */
struct temp_layout {
    uint32_t node_size;
    uint16_t key_size, value_size;
    _Bool is_lite;
};
struct temp_records {
    uint64_t count;
    void *context;
    int (*get)(void *, uint64_t, void *, void *);
};
struct temp_order {
    void *context;
    int (*compare)(const void *, const void *, void *);
};
struct temp_occupancy {
    const uint32_t *path; /* zero depth denotes the root */
    size_t depth;
    uint32_t keys;
};
struct temp_image_shape {
    uint32_t height; /* 0 explicitly requests an empty, rootless image */
    const struct temp_occupancy *overrides;
    size_t override_count;
};
struct temp_image_stats {
    uint32_t height;
    uint64_t records, nodes, leaves, free_nodes;
};
enum temp_image_status {
    TEMP_IMAGE_OK = 0, TEMP_IMAGE_INVALID = 1, TEMP_IMAGE_SPEC = 2,
    TEMP_IMAGE_IO = 3, TEMP_IMAGE_RESOURCE = 4
};
struct temp_image_result {
    enum temp_image_status status;
    uint64_t node, record;
    uint32_t slot;
    char invariant[64], detail[256];
    struct temp_image_stats actual;
    _Bool reused;
};
enum { TEMP_EXPECT_HEIGHT = 1, TEMP_EXPECT_NODES = 2, TEMP_EXPECT_LEAVES = 4 };
struct temp_image_constraints {
    unsigned mask;
    uint32_t height;
    uint64_t nodes, leaves;
};

/* Sparse geometry preflight. Unspecified nodes are full. Rejects nonexistent
 * paths, duplicate overrides, illegal occupancy and unrepresentable output. */
int temp_image_plan(const struct temp_layout *, const struct temp_image_shape *,
                    struct temp_image_stats *, struct temp_image_result *);

/* Always validates structure and allocation accounting. records == NULL skips
 * content comparison; exact_shape == NULL accepts any valid final shape.
 * The file is opened O_RDONLY, and never loaded/flushed through the library. */
int temp_image_verify(const char *path, const struct temp_layout *,
                      const struct temp_records *, const struct temp_order *,
                      const struct temp_image_shape *exact_shape,
                      const struct temp_image_constraints *,
                      struct temp_image_result *);

/* Builds without bptr_insert or any core library code. Existing files are reused
 * only after exact, read-only verification. Partial owned outputs are removed.
 * Parent directories are created after specification preflight. */
int temp_image_generate(const char *path, const struct temp_layout *,
                        const struct temp_image_shape *,
                        const struct temp_records *, const struct temp_order *,
                        struct temp_image_result *);
#endif
