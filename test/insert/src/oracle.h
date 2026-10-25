#ifndef INSERT_ORACLE_H
#define INSERT_ORACLE_H

#include <stddef.h>
#include "bptree.h"

/* A deliberately simple, copied, first-insert-wins reference mapping. */
struct insert_model
{
   size_t key_size, value_size, count, capacity;
   unsigned char *records;
   int (*compare)(const void *, const void *);
};

void insert_model_init(struct insert_model *model, size_t key_size,
                       size_t value_size,
                       int (*compare)(const void *, const void *));
void insert_model_free(struct insert_model *model);
int insert_model_add(struct insert_model *model, const void *key,
                      const void *value);
const unsigned char *insert_model_key(const struct insert_model *model,
                                      size_t index);
const unsigned char *insert_model_value(const struct insert_model *model,
                                        size_t index);

/*
 * Inspect the current tree through cached node fetch/release, never lookup,
 * explicit flush, or tree unload. Return 0 on success, 1 on a failed invariant
 * or observation, and describe the failure in reason when space is provided.
 * Each fetched node is copied and released before another node is fetched.
 * Fetching may itself evict a dirty entry in a small cache; tests of completely
 * unflushed state therefore use a cache large enough for the entire fixture.
 */
int insert_live_verify(struct bptr *tree, const struct insert_model *model,
                        char *reason, size_t reason_size);

#endif
