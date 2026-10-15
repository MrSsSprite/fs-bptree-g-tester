#ifndef TEMP_SHAPE_H
#define TEMP_SHAPE_H

#include <stddef.h>
#include <stdint.h>

/* A zero initialized shape requests a full tree. Nodes are an opaque sparse
 * trie; only paths mentioned by overrides allocate storage. */
struct temp_shape_node;
struct temp_shape {
   struct temp_shape_node *root;
   size_t override_count;
};
struct temp_shape_counts { uint64_t records, leaves, nodes; };
/* Final image totals for a verification that expects one inserted record. */
struct temp_verify_expect { uint32_t height; uint64_t nodes, leaves; };

/* Releases owned trie storage and resets the shape to its empty state. */
void temp_shape_free(struct temp_shape *shape);
/* Syntax and duplicate errors return nonzero. Semantic occupancy/path checks
 * are deferred until the complete set is passed to temp_shape_count. */
int temp_shape_add(struct temp_shape *shape, const char *spec);
/* Returned nodes are borrowed and remain valid until shape_free. Missing
 * children are NULL and temp_shape_keys(NULL, full_count) returns full_count. */
const struct temp_shape_node *temp_shape_child(const struct temp_shape_node *node,
                                               uint32_t child);
uint32_t temp_shape_keys(const struct temp_shape_node *node, uint32_t full_count);
/* Validates levels (1..254), layout and every override, returning TEMP_FULL_OK
 * and uint64 totals, or the relevant TEMP_FULL_E_* status. Untouched subtrees
 * are aggregated arithmetically; active pointer/file-offset caps are checked
 * by generation, rather than by this shape calculation. */
int temp_shape_count(const struct temp_shape *shape, unsigned int levels,
                     _Bool is_lite, uint32_t node_size,
                     struct temp_shape_counts *counts);
/* Canonical name with snprintf return semantics, or -1 on formatting/length
 * arithmetic failure. Generation also rejects actual truncation and overlong
 * path components before writing. NULL/empty shapes preserve full-tree names. */
int temp_path(char *buf, size_t size, const char *dir, unsigned int levels,
               int64_t st, int64_t interval, const struct temp_shape *shape);
/* Caller supplies a zero initialized output shape. On failure it remains safe
 * to free. Returns 1 on success and 0 for an invalid name. The caller owns the
 * parsed shape and releases it with temp_shape_free. A name may have an
 * arbitrary directory prefix. */
int temp_parse_name(const char *path, unsigned int *levels, int64_t *st,
                     int64_t *interval, struct temp_shape *shape);

#endif
