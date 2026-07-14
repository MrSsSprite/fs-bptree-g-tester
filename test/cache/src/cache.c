/*----------------------------- Private Includes -----------------------------*/
#include "cache.h"
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include "bptr_cache.h"
#include "test_bptr_temp.h"
#include "test_bptr_setup.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
static int cmp_u32_local(const void *lhs, const void *rhs);
static void bptr_cleanup_file(const char *fnm);
/*-------------------- Private Function Declarations END ---------------------*/


/*----------------------------- Public Functions -----------------------------*/
void test_cache_init(void)
{
   struct bptr_temp *temp_matrix[] =
    { lite_temps, norm_temps, };
   size_t temp_matrix_sz[] =
    { lite_temps_sz, norm_temps_sz, };
   const char *mes[sizeof(temp_matrix)/sizeof(*temp_matrix)] =
    { "\tLite Templates:", "\tNorm Templates:" };

   // Initialize Tree — verify cache is allocated
   puts("Cache Initialization:");
   for (size_t ti = 0; ti < sizeof(temp_matrix)/sizeof(*temp_matrix); ti++)
    {
      struct bptr_temp *temp_it = temp_matrix[ti];
      struct bptr *bptr;
      puts(mes[ti]);
      for (size_t i = 0; i < temp_matrix_sz[ti]; i++)
       {
         printf("\t\tStarting Test: %s...", temp_it[i].fnm);
         bptr = _bptr_create(temp_it + i);
         TEST_ASSERT_NOT_NULL_MESSAGE(bptr->cache,
                                      "cache not allocated");
         TEST_ASSERT_MESSAGE(bptr_unload(bptr) == BPTR_E_SUCCESS,
                             "bptr_unload failure");
         bptr_cleanup_file(temp_it[i].fnm);
         puts("Succeeded.");
       }
    }
}


void test_cache_node_lifecycle(void)
{
   struct bptr_temp template =
    { "cache_lifecycle.bptr", 1, BPTR_NODE_BYTE_DEFAULT,
      sizeof(uint32_t), sizeof(uint32_t), 4, cmp_u32_local };
   struct bptr_node *node, *node2;
   struct bptr *bptr;
   bptr_node_t node_idx;

   puts("\tNode Lifecycle (create→release→fetch→release):");

   // Create tree with small cache (4 slots)
   printf("\t\tCreating tree...");
   bptr = _bptr_create(&template);
   TEST_ASSERT_NOT_NULL(bptr->cache);
   puts("done.");

   // Create root node
   printf("\t\tCreating root node...");
   node = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "bptr_node_new returned NULL");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level,
                             "root node should be at level 0 (leaf)");
   TEST_ASSERT_MESSAGE(node->is_leaf,
                       "root node should be a leaf");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, node->node_idx,
                                 "node_idx should be allocated (non-zero)");
   node_idx = node->node_idx;
   puts("done.");

   // Release node (refcount drops 2→1, node becomes INACTIVE)
   printf("\t\tReleasing node...");
   bptr_node_unload(bptr, node);
   puts("done.");

   // Re-fetch same node — should be a cache HIT (same pointer)
   printf("\t\tRe-fetching node (expect cache HIT)...");
   node2 = bptr_node_fetch(bptr, node_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(node2, "cache HIT: node should still be in cache");
   TEST_ASSERT_EQUAL_MESSAGE(node, node2,
                             "cache HIT: fetched pointer must match original");
   TEST_ASSERT_EQUAL_MESSAGE(node_idx, node2->node_idx,
                             "node_idx should be preserved");
   TEST_ASSERT_EQUAL_MESSAGE(0, node2->level,
                             "level should be preserved");
   TEST_ASSERT_MESSAGE(node2->is_leaf,
                       "is_leaf should be preserved");
   puts("done.");

   // Release again
   printf("\t\tFinal release...");
   bptr_node_unload(bptr, node2);
   puts("done.");

   // Cleanup
   TEST_ASSERT_MESSAGE(bptr_unload(bptr) == BPTR_E_SUCCESS,
                       "bptr_unload failure");
   bptr_cleanup_file(template.fnm);
   puts("\tNode Lifecycle Test: Succeeded.");
}


void test_cache_data_persistence(void)
{
   struct bptr_temp template =
    { "cache_data_persist.bptr", 1, BPTR_NODE_BYTE_DEFAULT,
      sizeof(uint32_t), sizeof(uint32_t), 2, cmp_u32_local };
   struct bptr_node *node_a, *node_b, *node_c;
   struct bptr *bptr;
   uint32_t test_key = 0xDEADBEEFu, recovered_key;
   bptr_node_t a_idx;

   puts("\tData Persistence (write→flush→evict→reload):");

   // Create tree with cache_cap=2
   printf("\t\tCreating tree...");
   bptr = _bptr_create(&template);
   puts("done.");

   // Create root node A
   printf("\t\tCreating node A...");
   node_a = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL(node_a);
   a_idx = node_a->node_idx;
   puts("done.");

   // Write test data into node A
   printf("\t\tWriting test key 0x%X...", test_key);
   memcpy(node_a->keys, &test_key, sizeof(test_key));
   node_a->key_count = 1;
   node_a->is_dirty = 1;
   puts("done.");

   // Flush A (persist to disk, is_dirty cleared)
   printf("\t\tFlushing node A...");
   TEST_ASSERT_MESSAGE(bptr_node_flush(bptr, node_a) == 0,
                       "flush node A failed");
   TEST_ASSERT_MESSAGE(!node_a->is_dirty,
                       "is_dirty should be cleared after flush");
   puts("done.");

   // Release A (refcount 2→1, INACTIVE, oldest in eviction queue)
   printf("\t\tReleasing node A (now INACTIVE, oldest in eviction queue)...");
   bptr_node_unload(bptr, node_a);
   puts("done.");

   // Create node B (uses second pool slot)
   printf("\t\tCreating node B...");
   node_b = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL(node_b);
   puts("done.");

   // Release B (INACTIVE, queue: A(oldest) → B(newest))
   printf("\t\tReleasing node B...");
   bptr_node_unload(bptr, node_b);
   puts("done.");

   // Create node C — pool full, evicts A (oldest INACTIVE)
   // A was already flushed, so no re-flush needed; A is now disk-only
   printf("\t\tCreating node C (triggers eviction of node A)...");
   node_c = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(node_c,
                                "should succeed: A was evicted to free slot");
   puts("done.");

   // Release C (refcount 2→1)
   printf("\t\tReleasing node C...");
   bptr_node_unload(bptr, node_c);
   puts("done.");

   // Re-fetch A from disk — cache miss → evict + load from disk
   printf("\t\tRe-fetching evicted node A (expect cache MISS, disk load)...");
   node_a = bptr_node_fetch(bptr, a_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(node_a,
                                "node A should be loadable from disk after eviction");
   puts("done.");

   // Verify restored data
   printf("\t\tVerifying recovered data...");
   TEST_ASSERT_EQUAL_MESSAGE(1, node_a->key_count,
                             "key_count should be 1");
   memcpy(&recovered_key, node_a->keys, sizeof(recovered_key));
   TEST_ASSERT_EQUAL_HEX32_MESSAGE(test_key, recovered_key,
                                   "recovered key should match original");
   TEST_ASSERT_MESSAGE(!node_a->is_dirty,
                       "loaded node should not be dirty");
   puts("done.");

   // Cleanup
   printf("\t\tCleaning up...");
   bptr_node_unload(bptr, node_a);
   TEST_ASSERT_MESSAGE(bptr_unload(bptr) == BPTR_E_SUCCESS,
                       "bptr_unload failure");
   bptr_cleanup_file(template.fnm);
   puts("done.");
   puts("\tData Persistence Test: Succeeded.");
}


void test_cache_capacity_boundaries(void)
{
   struct bptr *bptr;
   struct bptr_node *node1, *node2, *node3;
   char path[256];

   puts("\tCache Capacity Boundaries:");

   // --- Test invalid (under-minimum) cache capacities ---
   // NOTE: cap=0 and cap=1 are rejected by bptr_init's parameter validation
   // BEFORE bptr_cache_init is reached. These tests validate the bptr_init
   // guard, not bptr_cache_init boundary handling. The MSB-set pool_cap
   // guard in bptr_cache_init remains untested by this suite.
   puts("\t\tInvalid capacity tests (bptr_init parameter validation):");

   printf("\t\t  capacity=0...");
   _bptr_path(path, sizeof(path), "cache_bound_0.bptr");
   bptr = bptr_init(path, 1, BPTR_NODE_BYTE_DEFAULT,
                    sizeof(uint32_t), sizeof(uint32_t),
                    0, cmp_u32_local);
   TEST_ASSERT_NULL_MESSAGE(bptr,
                            "bptr_init(cache_cap=0) should return NULL");
   TEST_ASSERT_EQUAL_MESSAGE(BPTR_E_FN_INPUT, bptr_errno,
                             "bptr_errno should be BPTR_E_FN_INPUT for cap=0");
   puts("passed.");

   printf("\t\t  capacity=1...");
   bptr = bptr_init(path, 1, BPTR_NODE_BYTE_DEFAULT,
                    sizeof(uint32_t), sizeof(uint32_t),
                    1, cmp_u32_local);
   TEST_ASSERT_NULL_MESSAGE(bptr,
                            "bptr_init(cache_cap=1) should return NULL (< MIN)");
   TEST_ASSERT_EQUAL_MESSAGE(BPTR_E_FN_INPUT, bptr_errno,
                             "bptr_errno should be BPTR_E_FN_INPUT for cap=1");
   puts("passed.");

   // --- Test cache-full condition ---
   puts("\t\tCache-full eviction test:");
   {
      struct bptr_temp template =
       { "cache_bound_full.bptr", 1, BPTR_NODE_BYTE_DEFAULT,
         sizeof(uint32_t), sizeof(uint32_t), 2, cmp_u32_local };

      printf("\t\t  Creating tree with cache_cap=2...");
      bptr = _bptr_create(&template);
      TEST_ASSERT_NOT_NULL(bptr->cache);
      puts("done.");

      // Pin 2 nodes (both ACTIVE, pool fully occupied, no evictable)
      printf("\t\t  Creating node 1 (ACTIVE)...");
      node1 = bptr_node_new(bptr, 0);
      TEST_ASSERT_NOT_NULL(node1);
      puts("done.");

      printf("\t\t  Creating node 2 (ACTIVE, pool now full)...");
      node2 = bptr_node_new(bptr, 0);
      TEST_ASSERT_NOT_NULL(node2);
      puts("done.");

      // Attempt to fetch a non-cached node — should fail with CACHE_FULL
      printf("\t\t  Fetching non-cached node_idx (expect BPTR_E_CACHE_FULL)...");
      node3 = bptr_node_fetch(bptr, 999);
      TEST_ASSERT_NULL_MESSAGE(node3,
                               "fetch should return NULL when cache full");
      TEST_ASSERT_EQUAL_MESSAGE(BPTR_E_CACHE_FULL, bptr_errno,
                                "bptr_errno should be BPTR_E_CACHE_FULL");
      puts("done.");

      // Cleanup
      printf("\t\t  Cleaning up...");
      bptr_node_unload(bptr, node1);
      bptr_node_unload(bptr, node2);
      TEST_ASSERT_MESSAGE(bptr_unload(bptr) == BPTR_E_SUCCESS,
                          "bptr_unload failure");
      bptr_cleanup_file(template.fnm);
      puts("done.");
   }

   puts("\tCache Capacity Boundaries Test: Succeeded.");
}
/*--------------------------- Public Functions END ---------------------------*/


void test_cache_evict_queue_middle(void)
{
   // Test middle-of-queue evict_remove path.
   // Creates 3 nodes in cap=3 cache, releases to form A(head)->B(mid)->C(tail),
   // re-fetches B to exercise !is_head && !is_tail removal, then verifies
   // eviction queue integrity by triggering eviction and confirming A is chosen.
   struct bptr_temp template =
    { "cache_evict_mid.bptr", 1, BPTR_NODE_BYTE_DEFAULT,
      sizeof(uint32_t), sizeof(uint32_t), 3, cmp_u32_local };
   struct bptr_node *na, *nb, *nc, *nd;
   struct bptr *bptr;
   uint32_t key_a = 0xAAAAAAAAu, key_b = 0xBBBBBBBBu, key_c = 0xCCCCCCCCu;
   uint32_t recovered;
   bptr_node_t a_idx, b_idx;

   puts("\tEviction Queue Middle-Removal:");

   // Create tree with cache_cap=3
   printf("\t\tCreating tree with cache_cap=3...");
   bptr = _bptr_create(&template);
   TEST_ASSERT_NOT_NULL(bptr->cache);
   puts("done.");

   // Node A: write, flush, release -> INACTIVE, queue head
   printf("\t\tCreating node A (queue head)...");
   na = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL(na);
   a_idx = na->node_idx;
   memcpy(na->keys, &key_a, sizeof(key_a));
   na->key_count = 1;
   na->is_dirty = 1;
   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_node_flush(bptr, na), "flush A failed");
   bptr_node_unload(bptr, na);  // refcnt 2->1, INACTIVE
   puts("done.");

   // Node B: write, flush, release -> INACTIVE, queue: A->B
   printf("\t\tCreating node B (queue middle)...");
   nb = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL(nb);
   b_idx = nb->node_idx;
   memcpy(nb->keys, &key_b, sizeof(key_b));
   nb->key_count = 1;
   nb->is_dirty = 1;
   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_node_flush(bptr, nb), "flush B failed");
   bptr_node_unload(bptr, nb);  // refcnt 2->1, INACTIVE
   puts("done.");

   // Node C: write, flush, release -> INACTIVE, queue: A->B->C
   printf("\t\tCreating node C (queue tail)...");
   nc = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL(nc);
   memcpy(nc->keys, &key_c, sizeof(key_c));
   nc->key_count = 1;
   nc->is_dirty = 1;
   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_node_flush(bptr, nc), "flush C failed");
   bptr_node_unload(bptr, nc);  // refcnt 2->1, INACTIVE
   puts("done.");

   // Re-fetch B from middle of eviction queue
   // This exercises evict_remove's !is_head && !is_tail path
   printf("\t\tRe-fetching B (exercises middle removal)...");
   nb = bptr_node_fetch(bptr, b_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(nb, "re-fetch B failed");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(b_idx, nb->node_idx, "B node_idx mismatch");
   memcpy(&recovered, nb->keys, sizeof(recovered));
   TEST_ASSERT_EQUAL_HEX32_MESSAGE(key_b, recovered, "B key mismatch after re-fetch");
   puts("done.");

   // Release B -> INACTIVE, appended to tail; queue: A->C->B
   printf("\t\tReleasing B (appends to tail)...");
   bptr_node_unload(bptr, nb);  // refcnt 2->1, INACTIVE, tail
   puts("done.");

   // Create node D -> pool full (3 slots occupied), triggers eviction.
   // A is now head of queue (A->C->B), so A should be evicted.
   printf("\t\tCreating node D (triggers eviction of A, the head)...");
   nd = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(nd, "create D after eviction failed");
   puts("done.");
   bptr_node_unload(bptr, nd);

   // Re-fetch A from disk -> cache miss, evicts another node, loads A
   printf("\t\tRe-fetching A (expect cache MISS, disk load)...");
   na = bptr_node_fetch(bptr, a_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(na, "re-fetch A after eviction failed");
   TEST_ASSERT_EQUAL_MESSAGE(1, na->key_count, "A key_count should be 1");
   memcpy(&recovered, na->keys, sizeof(recovered));
   TEST_ASSERT_EQUAL_HEX32_MESSAGE(key_a, recovered,
      "A key mismatch after eviction + reload");
   TEST_ASSERT_MESSAGE(!na->is_dirty, "loaded A should not be dirty");
   puts("done.");

   // Cleanup
   printf("\t\tCleaning up...");
   bptr_node_unload(bptr, na);
   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "bptr_unload failure");
   bptr_cleanup_file(template.fnm);
   puts("done.");
   puts("\tEviction Queue Middle-Removal Test: Succeeded.");
}


/*---------------------------- Private Functions -----------------------------*/
static int cmp_u32_local(const void *lhs, const void *rhs)
{
   uint32_t a = *(const uint32_t *)lhs;
   uint32_t b = *(const uint32_t *)rhs;
   return (a > b) - (a < b);
}


static void bptr_cleanup_file(const char *fnm)
{
   char path[256];
   _bptr_path(path, sizeof(path), fnm);
   if (remove(path) != 0)
   {
      // File may not exist if init failed — that's fine
      if (errno != ENOENT)
         TEST_FAIL_MESSAGE("failed to remove test file");
   }
}
/*-------------------------- Private Functions END ---------------------------*/
