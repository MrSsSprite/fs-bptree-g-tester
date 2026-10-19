/*----------------------------- Private Includes -----------------------------*/
#include "cache.h"
#include "fixtures.h"
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include "bptr_cache.h"
#include <string.h>
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static int policy_compare(const void *lhs, const void *rhs)
{
   uint32_t a, b;
   memcpy(&a, lhs, sizeof a);
   memcpy(&b, rhs, sizeof b);
   return (a > b) - (a < b);
}


static struct bptr_node *policy_new_leaf(struct bptr *tree)
{
   struct bptr_node *node;
   tree->height = 0;
   node = bptr_node_new(tree, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "cannot allocate policy fixture node");
   node->prev = node->next = 0;
   node->checksum = 0;
   return node;
}


/** Three independent disk blocks allow pressure in an initially empty cache. */
static struct bptr *policy_prepare(_Bool is_lite)
{
   struct bptr_temp template =
    { "cache_policy.bptr", is_lite, BPTR_NODE_BYTE_DEFAULT,
      sizeof(uint32_t), sizeof(uint32_t), 2, policy_compare };
   struct bptr *tree = cache_fixture_create(&template);

   for (bptr_node_t idx = 1; idx <= 3; idx++)
    {
      struct bptr_node *node = policy_new_leaf(tree);
      uint32_t key = idx * 10, value = idx * 20;
      TEST_ASSERT_EQUAL_UINT64(idx, node->node_idx);
      node->key_count = 1;
      memcpy(node->keys, &key, sizeof key);
      memcpy(node->vals, &value, sizeof value);
      TEST_ASSERT_EQUAL_INT(BPTR_E_SUCCESS, bptr_node_flush(tree, node));
      bptr_node_unload(tree, node);
    }
   TEST_ASSERT_EQUAL_INT(BPTR_E_SUCCESS, cache_fixture_unload());
   return cache_fixture_load(2, policy_compare);
}


static void policy_mark(struct bptr_node *node)
{
   uint32_t key = 0xAB000000u + node->node_idx;
   uint32_t value = 0xCD000000u + node->node_idx;
   memcpy(node->keys, &key, sizeof key);
   memcpy(node->vals, &value, sizeof value);
   node->is_dirty = 1;
}


static void policy_assert_node(struct bptr_node *node, bptr_node_t idx, _Bool dirty)
{
   uint32_t key, value;
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "policy node was not found");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(idx, node->node_idx, "policy node ID changed");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "policy node is not a leaf");
   TEST_ASSERT_EQUAL_UINT32(1, node->key_count);
   memcpy(&key, node->keys, sizeof key);
   memcpy(&value, node->vals, sizeof value);
   TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xAB000000u + idx, key, "policy node key changed");
   TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xCD000000u + idx, value, "policy node value changed");
   TEST_ASSERT_EQUAL_INT_MESSAGE(dirty, node->is_dirty, "unexpected disk reload or dirty flag");
}


static void policy_assert_full(struct bptr *tree, bptr_node_t idx)
{
   bptr_errno = BPTR_E_SUCCESS;
   TEST_ASSERT_NULL_MESSAGE(bptr_node_fetch(tree, idx), "pinned cache allowed a miss");
   TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_CACHE_FULL, bptr_errno,
                                 "cache miss did not report pinned capacity");
}
/*-------------------------- Private Functions END ---------------------------*/


/*----------------------------- Public Functions -----------------------------*/
void test_cache_fifo_eviction(void)
{
   for (unsigned int lite = 0; lite < 2; lite++)
      for (unsigned int allocate = 0; allocate < 2; allocate++)
         for (size_t reverse = 0; reverse < 2; reverse++)
            for (unsigned int refresh = 0; refresh < 2; refresh++)
             {
               struct bptr *tree = policy_prepare((_Bool)lite);
               struct bptr_node *nodes[2], *replacement, *survivor, *reloaded;
               size_t first = reverse, last = 1 - reverse;
               size_t victim = refresh ? last : first;

               printf("\tFIFO (%s, %s, first=%zu, refresh=%u)...\n",
                      lite ? "lite" : "normal", allocate ? "allocate" : "fetch",
                      first + 1, refresh);
               for (size_t i = 0; i < 2; i++)
                {
                  nodes[i] = bptr_node_fetch(tree, i + 1);
                  TEST_ASSERT_NOT_NULL(nodes[i]);
                  policy_mark(nodes[i]);
                }
               bptr_node_unload(tree, nodes[first]);
               bptr_node_unload(tree, nodes[last]);
               if (refresh)
                {
                  struct bptr_node *hit = bptr_node_fetch(tree, first + 1);
                  TEST_ASSERT_EQUAL_PTR(nodes[first], hit);
                  policy_assert_node(hit, first + 1, 1);
                  bptr_node_unload(tree, hit); /* Newly released node joins the tail. */
                }

               replacement = allocate ? policy_new_leaf(tree) : bptr_node_fetch(tree, 3);
               TEST_ASSERT_NOT_NULL(replacement);
               TEST_ASSERT_EQUAL_PTR_MESSAGE(nodes[victim], replacement,
                                             "FIFO did not evict the oldest released node");
               TEST_ASSERT_EQUAL_UINT64(allocate ? 4 : 3, replacement->node_idx);

               survivor = bptr_node_fetch(tree, 2 - victim);
               TEST_ASSERT_EQUAL_PTR_MESSAGE(nodes[1 - victim], survivor,
                                             "FIFO evicted the newer released node");
               policy_assert_node(survivor, 2 - victim, 1);
               policy_assert_full(tree, victim + 1); /* The victim is disk-only. */
               bptr_node_unload(tree, replacement);

               reloaded = bptr_node_fetch(tree, victim + 1);
               TEST_ASSERT_EQUAL_PTR(replacement, reloaded);
               policy_assert_node(reloaded, victim + 1, 0);
               bptr_node_unload(tree, reloaded);
               bptr_node_unload(tree, survivor);
               TEST_ASSERT_EQUAL_INT_MESSAGE(0, cache_fixture_discard(),
                                             "cannot clean FIFO fixture");
             }
}
/*--------------------------- Public Functions END ---------------------------*/
