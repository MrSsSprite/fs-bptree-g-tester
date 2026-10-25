#ifndef INSERT_HARNESS_H
#define INSERT_HARNESS_H

#include "oracle.h"
#include "bptr_node.h"
#include "temp_image.h"
#include <limits.h>
#include <stdio.h>

struct insert_config {
    struct temp_layout layout;
    uint64_t cache;
    int (*compare)(const void *, const void *);
    const char *profile;
    size_t prefix_bytes;
};

/* Teardown owns all resources, including refs acquired before an assertion. */
struct insert_state {
    struct bptr *tree;
    struct bptr_node *refs[8];
    struct insert_model model;
    struct insert_config config;
    unsigned char *input_copy;
    FILE *log;
    char name[160], dir[PATH_MAX], path[PATH_MAX], manifest[PATH_MAX];
    size_t operations, inserted, duplicates, live_checks, checkpoints, errors;
    _Bool failed;
};

extern struct insert_state insert_state;
extern uint64_t insert_seed;
extern const char *insert_work_dir;

void insert_require(_Bool ok, const char *format, ...);
void insert_begin(const char *name, const struct insert_config *config);
void insert_create(void);
void insert_seed_record(const void *key, const void *value);
void insert_generate(const struct temp_image_shape *shape);
void insert_apply(const void *key, const void *value);
void insert_expect_error(const void *key, const void *value, int expected,
                         const char *phase);
void insert_check_live(void);
void insert_close(void);
void insert_reopen(uint64_t cache);
void insert_checkpoint(_Bool reopen);
void insert_finish(void);
/* Intentional failed-resource images are discarded without content checking. */
void insert_discard(void);
void insert_cleanup(void);
void insert_summary(void);

#endif
