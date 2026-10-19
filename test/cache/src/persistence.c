/*----------------------------- Private Includes -----------------------------*/
#include "cache.h"
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include "bptr_cache.h"
#include "test_bptr_setup.h"
#include <string.h>
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------------- Private Variables -----------------------------*/
static struct bptr *persistence_tree;
static char persistence_path[256];
static _Bool persistence_owned;
static const uint32_t dirty_keys[] = { 0x12345678u, 0xABCDEF01u };
static const uint32_t dirty_values[] = { 0xDEADBEEFu, 0x76543210u };
/*-------------------------- Private Variables END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static int persistence_compare(const void *lhs, const void *rhs)
{
   uint32_t a, b;
   memcpy(&a, lhs, sizeof a);
   memcpy(&b, rhs, sizeof b);
   return (a > b) - (a < b);
}


static struct bptr_node *persistence_new_leaf(void)
{
   struct bptr_node *node;

   /* Independent leaf blocks suffice: these tests never traverse a tree. */
   persistence_tree->height = 0;
   node = bptr_node_new(persistence_tree, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "cannot allocate persistence fixture node");
   node->prev = node->next = 0;
   node->checksum = 0;
   return node;
}


/** Flush baseline records, then reopen with an empty two-slot cache. */
static void persistence_prepare(_Bool is_lite, bptr_node_t node_count)
{
   _bptr_path(persistence_path, sizeof persistence_path, "cache_data_persist.bptr");
   persistence_tree = bptr_init(persistence_path, is_lite, BPTR_NODE_BYTE_DEFAULT,
                                sizeof(uint32_t), sizeof(uint32_t),
                                2, persistence_compare);
   TEST_ASSERT_NOT_NULL_MESSAGE(persistence_tree, "cannot create persistence fixture");
   persistence_owned = 1;

   for (bptr_node_t idx = 1; idx <= node_count; idx++)
    {
      struct bptr_node *node = persistence_new_leaf();
      uint32_t keys[] = { idx * 10, idx * 10 + 1 };
      uint32_t values[] = { idx * 20, idx * 20 + 1 };

      TEST_ASSERT_EQUAL_UINT64_MESSAGE(idx, node->node_idx,
                                       "persistence fixture indices changed");
      node->key_count = 2;
      memcpy(node->keys, keys, sizeof keys);
      memcpy(node->vals, values, sizeof values);
      TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS,
                                    bptr_node_flush(persistence_tree, node),
                                    "cannot flush baseline records");
      TEST_ASSERT_FALSE_MESSAGE(node->is_dirty, "explicit flush did not clear dirty flag");
      bptr_node_unload(persistence_tree, node);
    }

   TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, bptr_unload(persistence_tree),
                                 "cannot close persistence fixture");
   persistence_tree = NULL;
   persistence_tree = bptr_load(persistence_path, 2, persistence_compare);
   TEST_ASSERT_NOT_NULL_MESSAGE(persistence_tree, "cannot reopen persistence fixture");
}


/** Pin B so A is the only eviction candidate, then modify A without flushing. */
static struct bptr_node *persistence_dirty_victim(struct bptr_node **pinned)
{
   struct bptr_node *victim = bptr_node_fetch(persistence_tree, 1);
   TEST_ASSERT_NOT_NULL_MESSAGE(victim, "cannot fetch persistence victim");
   *pinned = bptr_node_fetch(persistence_tree, 2);
   TEST_ASSERT_NOT_NULL_MESSAGE(*pinned, "cannot pin the other cache slot");

   memcpy(victim->keys, dirty_keys, sizeof dirty_keys);
   memcpy(victim->vals, dirty_values, sizeof dirty_values);
   victim->is_dirty = 1;
   bptr_node_unload(persistence_tree, victim);
   return victim;
}


/** Confirm slot reuse and reload A before closing can flush any cached data. */
static void persistence_verify_reload(struct bptr_node *victim_slot,
                                      struct bptr_node *replacement)
{
   struct bptr_node *reloaded;
   uint32_t keys[2], values[2];

   TEST_ASSERT_NOT_NULL_MESSAGE(replacement, "cannot evict dirty victim");
   TEST_ASSERT_EQUAL_PTR_MESSAGE(victim_slot, replacement,
                                 "replacement did not evict the dirty victim");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(3, replacement->node_idx,
                                    "replacement has the wrong node ID");

   /* Erase the victim's old bytes in the reused slot before reading it back. */
   memset(replacement->keys, 0, sizeof dirty_keys);
   memset(replacement->vals, 0, sizeof dirty_values);
   replacement->is_dirty = 1;
   bptr_node_unload(persistence_tree, replacement);

   /* B remains pinned; A must be read into the slot that C just occupied. */
   reloaded = bptr_node_fetch(persistence_tree, 1);
   TEST_ASSERT_NOT_NULL_MESSAGE(reloaded, "cannot reload evicted dirty victim");
   TEST_ASSERT_EQUAL_PTR_MESSAGE(replacement, reloaded,
                                 "reload did not reuse the only evictable slot");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, reloaded->node_idx,
                                    "reload returned another cached node");
   TEST_ASSERT_TRUE_MESSAGE(reloaded->is_leaf, "reloaded victim is not a leaf");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, reloaded->key_count,
                                    "dirty eviction changed record count");
   memcpy(keys, reloaded->keys, sizeof keys);
   memcpy(values, reloaded->vals, sizeof values);
   TEST_ASSERT_EQUAL_HEX32_ARRAY_MESSAGE(dirty_keys, keys, 2,
                                         "dirty eviction lost updated keys");
   TEST_ASSERT_EQUAL_HEX32_ARRAY_MESSAGE(dirty_values, values, 2,
                                         "dirty eviction lost updated values");
   TEST_ASSERT_FALSE_MESSAGE(reloaded->is_dirty, "disk-loaded victim is still dirty");
   bptr_node_unload(persistence_tree, reloaded);
}
/*-------------------------- Private Functions END ---------------------------*/


/*----------------------------- Public Functions -----------------------------*/
void cache_persistence_cleanup(void)
{
   int unload_status = BPTR_E_SUCCESS;

   if (persistence_tree != NULL)
    {
      unload_status = bptr_unload(persistence_tree);
      persistence_tree = NULL;
    }
   if (persistence_owned)
    {
      int remove_status = remove(persistence_path);
      persistence_owned = 0;
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove_status,
                                    "cannot remove persistence fixture");
    }
   TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, unload_status,
                                 "cannot unload persistence fixture");
}


void test_cache_data_persistence(void)
{
   for (unsigned int lite = 0; lite < 2; lite++)
    {
      struct bptr_node *pinned, *victim, *replacement;

      printf("\tDirty eviction through allocation (%s)...\n", lite ? "lite" : "normal");
      persistence_prepare((_Bool)lite, 2);
      victim = persistence_dirty_victim(&pinned);
      replacement = persistence_new_leaf();
      persistence_verify_reload(victim, replacement);
      bptr_node_unload(persistence_tree, pinned);
      cache_persistence_cleanup();
    }
}
/*--------------------------- Public Functions END ---------------------------*/
