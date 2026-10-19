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
static struct bptr *collision_tree;
static char collision_path[256];
static _Bool collision_owned;
/*-------------------------- Private Variables END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static int collision_compare(const void *lhs, const void *rhs)
{
   uint32_t a, b;
   memcpy(&a, lhs, sizeof a);
   memcpy(&b, rhs, sizeof b);
   return (a > b) - (a < b);
}


/** Prepare independent leaf blocks, then reopen with the cache under test. */
static void collision_prepare(const char *filename, _Bool is_lite,
                              bptr_node_t node_count, uint64_t capacity)
{
   _bptr_path(collision_path, sizeof collision_path, filename);
   collision_owned = 1;
   collision_tree = bptr_init(collision_path, is_lite, BPTR_NODE_BYTE_DEFAULT,
                              sizeof(uint32_t), sizeof(uint32_t),
                              32, collision_compare);
   TEST_ASSERT_NOT_NULL_MESSAGE(collision_tree, "cannot create collision fixture");

   for (bptr_node_t idx = 1; idx <= node_count; idx++)
    {
      struct bptr_node *node;
      uint32_t key = idx, value = idx * 2;

      /* No tree traversal is needed: each block is an independent leaf. */
      collision_tree->height = 0;
      node = bptr_node_new(collision_tree, 0);
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "cannot allocate collision fixture node");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(idx, node->node_idx,
                                       "collision fixture indices changed");
      node->prev = node->next = 0;
      node->checksum = 0;
      node->key_count = 1;
      memcpy(node->keys, &key, sizeof key);
      memcpy(node->vals, &value, sizeof value);
      TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS,
                                    bptr_node_flush(collision_tree, node),
                                    "cannot flush collision fixture node");
      bptr_node_unload(collision_tree, node);
    }

   TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, bptr_unload(collision_tree),
                                 "cannot close collision fixture");
   collision_tree = NULL;
   collision_tree = bptr_load(collision_path, capacity, collision_compare);
   TEST_ASSERT_NOT_NULL_MESSAGE(collision_tree, "cannot reopen collision fixture");
}


/** Distinguish a cache hit from a duplicate loaded from the flushed fixture. */
static void collision_mark(struct bptr_node *node)
{
   uint32_t key = 0xCA000000u + node->node_idx;
   uint32_t value = 0xFE000000u + node->node_idx;

   memcpy(node->keys, &key, sizeof key);
   memcpy(node->vals, &value, sizeof value);
   node->is_dirty = 1;
}


static void collision_assert_data(struct bptr_node *node, bptr_node_t idx)
{
   uint32_t key, value;

   TEST_ASSERT_EQUAL_UINT64_MESSAGE(idx, node->node_idx,
                                    "collision lookup returned another node");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "collision node is not a leaf");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, node->key_count,
                                    "collision lookup changed key count");
   memcpy(&key, node->keys, sizeof key);
   memcpy(&value, node->vals, sizeof value);
   TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xCA000000u + idx, key,
                                   "collision lookup lost the cached key");
   TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFE000000u + idx, value,
                                   "collision lookup lost the cached value");
}


/** All pool slots remain pinned, so a broken lookup cannot fall back to disk. */
static void collision_assert_hit(bptr_node_t idx, struct bptr_node *expected)
{
   struct bptr_node *node = bptr_node_fetch(collision_tree, idx);

   TEST_ASSERT_NOT_NULL_MESSAGE(node, "colliding resident node was not found");
   TEST_ASSERT_EQUAL_PTR_MESSAGE(expected, node,
                                 "collision lookup duplicated a resident node");
   collision_assert_data(node, idx);
   bptr_node_unload(collision_tree, node);
}


static void collision_assert_miss(bptr_node_t idx)
{
   bptr_errno = BPTR_E_SUCCESS;
   TEST_ASSERT_NULL_MESSAGE(bptr_node_fetch(collision_tree, idx),
                             "disk-only node unexpectedly hit the pinned cache");
   TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_CACHE_FULL, bptr_errno,
                                 "pinned cluster should prevent a disk load");
}
/*-------------------------- Private Functions END ---------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int cache_collisions_cleanup(void)
{
   int unload_status = BPTR_E_SUCCESS, remove_status = 0;

   if (collision_tree != NULL)
    {
      unload_status = bptr_unload(collision_tree);
      collision_tree = NULL;
    }
   if (collision_owned)
    {
      remove_status = remove(collision_path);
      collision_owned = 0;
    }
   return unload_status != BPTR_E_SUCCESS || remove_status != 0;
}


void test_cache_hash_collisions(void)
{
   /* Four pool slots give eight hash buckets with the current Fibonacci hash.
    * IDs 1, 9, 17 share bucket 4; ID 6 starts at bucket 5. In this order,
    * Robin Hood insertion moves 6 twice, ending with [1, 9, 17, 6] in 4..7. */
   const bptr_node_t indices[] = { 1, 6, 9, 17 };
   struct bptr_node *nodes[4];

   for (unsigned int lite = 0; lite < 2; lite++)
    {
      collision_prepare("cache_hash_collisions.bptr", (_Bool)lite, 17, 4);
      for (size_t i = 0; i < 4; i++)
       {
         nodes[i] = bptr_node_fetch(collision_tree, indices[i]);
         TEST_ASSERT_NOT_NULL_MESSAGE(nodes[i], "cannot load colliding node");
         collision_mark(nodes[i]);
       }

      for (size_t i = 4; i > 0; i--)
         collision_assert_hit(indices[i - 1], nodes[i - 1]);
      for (size_t i = 0; i < 4; i++)
         collision_assert_hit(indices[i], nodes[i]);

      for (size_t i = 0; i < 4; i++)
         bptr_node_unload(collision_tree, nodes[i]);
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, cache_collisions_cleanup(),
                                    "cannot clean collision fixture");
    }
}


void test_cache_hash_collision_deletion(void)
{
   /* Three pool slots give four hash buckets. IDs 3, 8 and 11 share bucket 3,
    * making a cluster across buckets 3, 0 and 1. ID 16 also hashes to 3. */
   const bptr_node_t indices[] = { 3, 8, 11 };
   const char *roles[] = { "head", "middle", "tail" };
   struct bptr_node *nodes[3], *replacement;

   for (unsigned int lite = 0; lite < 2; lite++)
      for (size_t victim = 0; victim < 3; victim++)
       {
         printf("\tWrapped collisions (%s, %s deletion)...\n",
                lite ? "lite" : "normal", roles[victim]);
         collision_prepare("cache_hash_deletion.bptr", (_Bool)lite, 16, 3);
         for (size_t i = 0; i < 3; i++)
          {
            nodes[i] = bptr_node_fetch(collision_tree, indices[i]);
            TEST_ASSERT_NOT_NULL_MESSAGE(nodes[i], "cannot load wrapped cluster");
            collision_mark(nodes[i]);
          }
         for (size_t i = 0; i < 3; i++)
            collision_assert_hit(indices[i], nodes[i]);

         /* A valid disk-only collider must miss without disturbing residents. */
         collision_assert_miss(16);

         /* Only this entry is evictable, independently of FIFO policy. */
         bptr_node_unload(collision_tree, nodes[victim]);
         replacement = bptr_node_fetch(collision_tree, 1);
         TEST_ASSERT_NOT_NULL_MESSAGE(replacement, "cannot evict cluster victim");
         TEST_ASSERT_EQUAL_PTR_MESSAGE(nodes[victim], replacement,
                                       "replacement did not reuse the victim slot");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(1, replacement->node_idx,
                                          "replacement has the wrong node ID");
         collision_mark(replacement);
         collision_assert_hit(1, replacement);
         for (size_t i = 0; i < 3; i++)
            if (i != victim)
               collision_assert_hit(indices[i], nodes[i]);
         collision_assert_miss(indices[victim]);

         /* ID 2 starts at bucket 0. Replacing 1 again exposes incorrect probe
          * distances left by backward shifts, including the zero-distance stop. */
         bptr_node_unload(collision_tree, replacement);
         replacement = bptr_node_fetch(collision_tree, 2);
         TEST_ASSERT_NOT_NULL_MESSAGE(replacement, "cannot replace eviction slot");
         TEST_ASSERT_EQUAL_PTR_MESSAGE(nodes[victim], replacement,
                                       "second replacement changed pool slots");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(2, replacement->node_idx,
                                          "second replacement has wrong node ID");
         collision_mark(replacement);
         collision_assert_hit(2, replacement);
         for (size_t i = 0; i < 3; i++)
            if (i != victim)
               collision_assert_hit(indices[i], nodes[i]);

         /* Re-fetch the deleted ID: a stale hash entry must not return node 2. */
         bptr_node_unload(collision_tree, replacement);
         nodes[victim] = bptr_node_fetch(collision_tree, indices[victim]);
         TEST_ASSERT_NOT_NULL_MESSAGE(nodes[victim], "cannot reload cluster victim");
         collision_assert_data(nodes[victim], indices[victim]);
         for (size_t i = 0; i < 3; i++)
            collision_assert_hit(indices[i], nodes[i]);

         for (size_t i = 0; i < 3; i++)
            bptr_node_unload(collision_tree, nodes[i]);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, cache_collisions_cleanup(),
                                       "cannot clean collision fixture");
       }
}
/*--------------------------- Public Functions END ---------------------------*/
