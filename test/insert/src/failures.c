#define _POSIX_C_SOURCE 200809L

#include "failures.h"
#include "harness.h"
#include "scenarios.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* These fixtures are produced independently of bptr_insert. Resource failures
 * are armed only after setup, and no rollback promise is inferred from a
 * generic insertion error. Every probe owns a fresh disposable image. */
static int compare_i64(const void *left, const void *right)
{
    int64_t lhs, rhs;
    memcpy(&lhs, left, sizeof lhs);
    memcpy(&rhs, right, sizeof rhs);
    return (lhs > rhs) - (lhs < rhs);
}

static void prepare(const char *phase, _Bool lite)
{
    const struct insert_config config = {
        .layout = {128, 8, 8, lite},
        .cache = BPTR_CACHE_CAPACITY_MIN,
        .compare = compare_i64,
        .profile = "i64-asc"
    };
    const struct temp_image_shape shape = {2, NULL, 0};
    struct temp_image_stats stats;
    struct temp_image_result result;
    char name[160];
    int status;

    snprintf(name, sizeof name, "%s-%s", phase, lite ? "lite" : "normal");
    insert_begin(name, &config);
    status = temp_image_plan(&config.layout, &shape, &stats, &result);
    insert_require(status == TEMP_IMAGE_OK,
                   "failure fixture plan: %s: %s", result.invariant,
                   result.detail);
    insert_require(stats.records > 1 && stats.records < 1000,
                   "failure fixture has unexpected record total %" PRIu64,
                   stats.records);
    for (uint64_t i = 0; i < stats.records; i++) {
        int64_t key = -100 + (int64_t)i * 16;
        int64_t value = 1234567 - (int64_t)i * 73;
        insert_seed_record(&key, &value);
    }
    insert_generate(&shape);
}

static void pin(bptr_node_t index, size_t slot)
{
    insert_require(slot < sizeof insert_state.refs / sizeof *insert_state.refs &&
                   insert_state.refs[slot] == NULL,
                   "failure probe reference slot %zu is occupied", slot);
    insert_state.refs[slot] = bptr_node_fetch(insert_state.tree, index);
    insert_require(insert_state.refs[slot] != NULL,
                   "failure probe cannot pin node %" PRIuFAST64
                   " (error %d)", index, bptr_errno);
    insert_require(insert_state.refs[slot]->node_idx == index,
                   "failure probe fetched wrong node for %" PRIuFAST64, index);
}

static void release(size_t slot)
{
    if (insert_state.refs[slot] != NULL) {
        bptr_node_unload(insert_state.tree, insert_state.refs[slot]);
        insert_state.refs[slot] = NULL;
    }
}

static bptr_node_t child(const struct bptr_node *node, uint32_t slot)
{
    const unsigned char *values = node->vals;
    if (insert_state.tree->is_lite) {
        uint32_t index;
        memcpy(&index, values + slot * sizeof index, sizeof index);
        return index;
    } else {
        uint64_t index;
        memcpy(&index, values + slot * sizeof index, sizeof index);
        return index;
    }
}

/* Read child IDs before the probe, then reopen so collection cannot warm the
 * two-slot cache whose exhausted state is being tested. */
static void leaf_indices(bptr_node_t *first, bptr_node_t *last)
{
    pin(insert_state.tree->root_idx, 0);
    insert_require(!insert_state.refs[0]->is_leaf &&
                   insert_state.refs[0]->level == 1 &&
                   insert_state.refs[0]->key_count > 0,
                   "failure fixture must have a branch root and leaf children");
    *first = child(insert_state.refs[0], 0);
    *last = child(insert_state.refs[0], insert_state.refs[0]->key_count);
    insert_require(*first != 0 && *last != 0 && *first != *last &&
                   *first != insert_state.tree->root_idx &&
                   *last != insert_state.tree->root_idx,
                   "failure fixture requires distinct non-root leaf children");
    release(0);
    insert_reopen(BPTR_CACHE_CAPACITY_MIN);
}

static void require_reusable_slots(bptr_node_t first, bptr_node_t last)
{
    /* Holding both leaves at once requires both cache slots to be available.
     * This detects a reference leaked by the failed search, independently of
     * any undocumented rollback of record contents. */
    pin(first, 0);
    pin(last, 1);
    release(0);
    release(1);
}

void test_insert_cache_errors(void)
{
    const int64_t key = -100, replacement = INT64_C(-987654321);

    for (unsigned layout = 0; layout < 2; layout++) {
        if (insert_scenario_layout >= 0 &&
            layout != (unsigned)insert_scenario_layout) continue;
        bptr_node_t first, last;

        prepare("cache-root", (_Bool)layout);
        leaf_indices(&first, &last);
        pin(first, 0);
        pin(last, 1);
        insert_expect_error(&key, &replacement, BPTR_E_CACHE_FULL,
                            "root cache fetch");
        release(0);
        release(1);
        require_reusable_slots(first, last);
        insert_discard();

        prepare("cache-descendant", (_Bool)layout);
        leaf_indices(&first, &last);
        pin(insert_state.tree->root_idx, 0);
        pin(last, 1);
        insert_expect_error(&key, &replacement, BPTR_E_CACHE_FULL,
                            "descendant cache fetch");
        release(0);
        release(1);
        require_reusable_slots(first, last);
        insert_discard();

        /* Both search nodes are already pinned, so an absent-key search can
         * finish without a free slot. Inserting into this full target leaf
         * then needs an additional node while both slots remain occupied. */
        prepare("cache-split", (_Bool)layout);
        leaf_indices(&first, &last);
        pin(insert_state.tree->root_idx, 0);
        pin(first, 1);
        const int64_t fresh = -101;
        struct bptr_find_res position = bptr_find_node(insert_state.tree, &fresh);
        insert_state.refs[2] = position.node;
        insert_require(position.node != NULL,
                       "split failure setup cannot search pinned target (error %d)",
                       bptr_errno);
        insert_require(!position.is_found && position.idx == 0 &&
                       position.node->node_idx == first &&
                       position.node->key_count ==
                           insert_state.tree->node_bound.leaf.up - 1,
                       "split failure setup requires absent position in full target");
        release(2);
        insert_expect_error(&fresh, &replacement, BPTR_E_CACHE_FULL,
                            "full-leaf split allocation");
        release(0);
        release(1);
        insert_discard();
    }
}

static void truncate_before(bptr_node_t index, uint_fast32_t node_size)
{
    uintmax_t bytes;
    off_t length;

    insert_require(index > 0 && node_size > 0 &&
                   index <= UINTMAX_MAX / node_size,
                   "failure probe has unrepresentable node offset");
    bytes = (uintmax_t)index * node_size;
    length = (off_t)bytes;
    insert_require(length >= 0 && (uintmax_t)length == bytes,
                   "failure probe file offset is not representable");
    insert_require(truncate(insert_state.path, length) == 0,
                   "truncate failed for node %" PRIuFAST64 ": %s",
                   index, strerror(errno));
}

void test_insert_io_errors(void)
{
    const int64_t key = -100, replacement = INT64_C(-987654321);

    for (unsigned layout = 0; layout < 2; layout++) {
        if (insert_scenario_layout >= 0 &&
            layout != (unsigned)insert_scenario_layout) continue;
        bptr_node_t root, first, last;
        uint_fast32_t node_size;

        prepare("io-root", (_Bool)layout);
        root = insert_state.tree->root_idx;
        node_size = insert_state.tree->node_size;
        insert_close();
        truncate_before(root, node_size);
        insert_reopen(BPTR_CACHE_CAPACITY_MIN);
        insert_expect_error(&key, &replacement, BPTR_E_FACCESS,
                            "truncated root fetch");
        insert_discard();

        prepare("io-descendant", (_Bool)layout);
        leaf_indices(&first, &last);
        root = insert_state.tree->root_idx;
        node_size = insert_state.tree->node_size;
        /* This condition is a fixture requirement, never an insertion or
         * allocation-order assertion. Closing first avoids stdio read-ahead
         * from making removed descendant bytes appear readable. */
        insert_require(first > root,
                       "truncation fixture leaf %" PRIuFAST64
                       " must follow root %" PRIuFAST64, first, root);
        insert_close();
        truncate_before(first, node_size);
        insert_reopen(BPTR_CACHE_CAPACITY_MIN);
        insert_expect_error(&key, &replacement, BPTR_E_FACCESS,
                            "truncated descendant fetch");
        insert_discard();
    }
}
