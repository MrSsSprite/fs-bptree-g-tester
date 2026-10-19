/*----------------------------- Private Includes -----------------------------*/
#include "cache_checks.h"
#include "search.h"
#include "progress.h"
#include "bptr_node.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
/** Acquire before asserting, so Unity teardown owns even an incorrect result. */
static void acquire_first(size_t leaf, unsigned int slot)
{
   int64_t key = search_state.leaves[leaf].first;
   struct bptr_find_res result = bptr_find_node(search_state.tree, &key);
   int64_t actual_key = 0, actual_value = 0;

   search_state.refs[slot] = result.node;
   search_require(result.node != NULL,
                  "ownership query %" PRId64 ": expected leaf %" PRIuFAST64
                  " index 0, got NULL (bptr_errno=%d)",
                  key, search_state.leaves[leaf].node_idx, bptr_errno);
   search_require(result.node->is_leaf && result.is_found && result.idx == 0 &&
                  result.node->node_idx == search_state.leaves[leaf].node_idx,
                  "ownership query %" PRId64 ": expected leaf %" PRIuFAST64
                  " index 0 found 1; got leaf %" PRIuFAST64
                  " index %" PRIu32 " found %d (is_leaf=%d)",
                  key, search_state.leaves[leaf].node_idx,
                  result.node->node_idx, result.idx, result.is_found,
                  result.node->is_leaf);
   search_require(result.node->key_count == search_state.leaves[leaf].count,
                  "ownership query %" PRId64 ": leaf key count changed", key);
   memcpy(&actual_key, result.node->keys, sizeof actual_key);
   memcpy(&actual_value, result.node->vals, sizeof actual_value);
   search_require(actual_key == key && actual_value == key * 2,
                  "ownership query %" PRId64 ": expected key/value %" PRId64
                  "/%" PRId64 ", got %" PRId64 "/%" PRId64,
                  key, key, key * 2, actual_key, actual_value);
   search_state.hits++;
}

static void release_ref(unsigned int slot)
{
   bptr_node_unload(search_state.tree, search_state.refs[slot]);
   search_state.refs[slot] = NULL;
}

static void pin_node(bptr_node_t node_idx, unsigned int slot)
{
   search_state.refs[slot] = bptr_node_fetch(search_state.tree, node_idx);
   search_require(search_state.refs[slot] != NULL,
                  "pin leaf/node %" PRIuFAST64
                  ": fetch failed (bptr_errno=%d); possible leaked ancestor",
                  node_idx, bptr_errno);
   search_require(search_state.refs[slot]->node_idx == node_idx,
                  "pin leaf/node %" PRIuFAST64 ": got node %" PRIuFAST64,
                  node_idx, search_state.refs[slot]->node_idx);
}

/** On failure only node and errno are specified; never inspect idx/is_found. */
static void expect_error(int64_t key, int error, const char *phase)
{
   bptr_errno = 0;
   struct bptr_find_res result = bptr_find_node(search_state.tree, &key);
   search_state.refs[0] = result.node;
   search_require(result.node == NULL,
                  "%s query %" PRId64 ": expected NULL/error %d, got leaf %"
                  PRIuFAST64 " (bptr_errno=%d)", phase, key, error,
                  result.node == NULL ? (bptr_node_t)0 : result.node->node_idx,
                  bptr_errno);
   search_require(bptr_errno == error,
                  "%s query %" PRId64 ": expected error %d, got %d",
                  phase, key, error, bptr_errno);
}

/** Truncate a closed owned copy, retaining its complete header for cold load. */
static void truncate_before(bptr_node_t node_idx, uint_fast32_t node_size)
{
   uintmax_t bytes;
   off_t length;

   search_require(node_idx > 0 && node_size > 0 &&
                  node_idx <= UINTMAX_MAX / node_size,
                  "truncate node %" PRIuFAST64 ": invalid file offset", node_idx);
   bytes = (uintmax_t)node_idx * node_size;
   length = (off_t)bytes;
   search_require(length >= 0 && (uintmax_t)length == bytes,
                  "truncate node %" PRIuFAST64 ": file offset is not representable",
                  node_idx);
   search_require(truncate(search_state.path, length) == 0,
                  "truncate %s at node %" PRIuFAST64 ": %s",
                  search_state.path, node_idx, strerror(errno));
}
/*-------------------------- Private Functions END ---------------------------*/


/*----------------------------- Public Functions -----------------------------*/
void test_search_empty(void)
{
   static const int64_t keys[] = { INT64_MIN, 0, INT64_MAX };

   for (unsigned int lite = 0; lite < 2; lite++)
    {
      search_empty_prepare((_Bool)lite);
      for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
       {
         expect_error(keys[i], BPTR_E_EMPTY_TREE, "empty tree");
         search_require(search_state.tree->root_idx == 0 &&
                        search_state.tree->height == 0 &&
                        search_state.tree->record_cnt == 0 &&
                        search_state.tree->node_cnt == 0,
                        "empty query %" PRId64 ": search created tree metadata",
                        keys[i]);
       }
      search_discard();
    }
   puts("    empty trees: PASS (2 layouts, 6 queries)");
}

void test_search_ownership(void)
{
   unsigned int scenarios = 2;
   int64_t held_key = 0, held_value = 0;

   search_prepare();
   acquire_first(0, 0);
   acquire_first(0, 1);
   search_require(search_state.refs[0] == search_state.refs[1],
                  "same-leaf searches did not return aliases for leaf %"
                  PRIuFAST64, search_state.leaves[0].node_idx);
   release_ref(0);

   /* With only one live alias, traversal of a taller tree must still work in
    * the minimum two-slot cache. Repeating the walk forces inactive entries
    * through eviction while the retained leaf remains valid. */
   for (unsigned int pass = 0; pass < 2; pass++)
      for (size_t leaf = 1; leaf < search_state.leaf_count; leaf++)
       {
         progress_update("    ownership %s: pass %u/2 leaf %zu/%zu",
                         search_template->path, pass + 1, leaf + 1,
                         search_state.leaf_count);
         acquire_first(leaf, 0);
         release_ref(0);
       }
   if (search_state.leaf_count > 1) scenarios++;
   search_require(search_state.refs[1]->is_leaf &&
                  search_state.refs[1]->node_idx == search_state.leaves[0].node_idx &&
                  search_state.refs[1]->key_count == search_state.leaves[0].count,
                  "retained leaf alias changed during cache churn");
   memcpy(&held_key, search_state.refs[1]->keys, sizeof held_key);
   memcpy(&held_value, search_state.refs[1]->vals, sizeof held_value);
   search_require(held_key == search_state.leaves[0].first &&
                  held_value == search_state.leaves[0].first * 2,
                  "retained leaf alias key/value changed during cache churn");

   /* Success is defined by the returned node, even with a stale global error.
    * The implementation is allowed either to retain or clear that error. */
   acquire_first(0, 0);
   release_ref(0);
   bptr_errno = BPTR_E_EMPTY_TREE;
   acquire_first(0, 0);
   release_ref(0);
   release_ref(1);
   search_finish();
   progress_done("    ownership %s: PASS (%u scenarios)",
                 search_template->path, scenarios);
}

void test_search_cache_failures(void)
{
   unsigned int scenarios = 0;

   search_prepare();
   if (search_state.leaf_count > 1)
    {
      int64_t key = search_state.leaves[0].first;

      /* Neither occupied slot is the root, so the initial root fetch fails. */
      pin_node(search_state.leaves[0].node_idx, 1);
      pin_node(search_state.leaves[1].node_idx, 2);
      expect_error(key, BPTR_E_CACHE_FULL, "root cache fetch");
      release_ref(1);
      release_ref(2);
      search_check(key, 0, 0, 1);
      scenarios++;

      search_reopen();
      /* The root fetch succeeds from cache, but its next fetch has no free
       * slot: the external root reference and an unrelated leaf block both. */
      pin_node(search_state.tree->root_idx, 1);
      pin_node(search_state.leaves[1].node_idx, 2);
      expect_error(key, BPTR_E_CACHE_FULL, "descendant cache fetch");
      release_ref(1);
      release_ref(2);
      search_check(key, 0, 0, 1);
      scenarios++;

      /* Do not reopen: both slots must be reusable after the failure and its
       * recovery. A leaked reference to any traversed ancestor blocks this. */
      pin_node(search_state.leaves[0].node_idx, 1);
      pin_node(search_state.leaves[1].node_idx, 2);
      release_ref(1);
      release_ref(2);
      scenarios++;
    }
   search_finish();
   progress_done("    cache failures %s: PASS (%u scenarios)",
                 search_template->path, scenarios);
}

void test_search_io_failures(void)
{
   unsigned int scenarios = 1;
   _Bool has_descendant;
   bptr_node_t root;
   uint_fast32_t node_size;
   int64_t key;

   search_prepare();
   root = search_state.tree->root_idx;
   node_size = search_state.tree->node_size;
   key = search_state.leaves[0].first;
   has_descendant = search_state.leaf_count > 1;
   search_close();
   truncate_before(root, node_size);
   search_reopen();
   expect_error(key, BPTR_E_FACCESS, "truncated root fetch");
   search_discard();

   if (has_descendant)
    {
      size_t last;
      bptr_node_t leaf_idx;

      search_prepare();
      last = search_state.leaf_count - 1;
      leaf_idx = search_state.leaves[last].node_idx;
      node_size = search_state.tree->node_size;
      key = search_state.leaves[last].first;
      search_require(leaf_idx > search_state.tree->root_idx,
                     "last leaf %" PRIuFAST64
                     " must follow root %" PRIuFAST64 " for truncation probe",
                     leaf_idx, search_state.tree->root_idx);
      search_close();
      truncate_before(leaf_idx, node_size);
      search_reopen();
      expect_error(key, BPTR_E_FACCESS, "truncated descendant fetch");
      search_discard();
      scenarios++;
    }
   progress_done("    io failures %s: PASS (%u scenarios)",
                 search_template->path, scenarios);
}
/*--------------------------- Public Functions END ---------------------------*/
