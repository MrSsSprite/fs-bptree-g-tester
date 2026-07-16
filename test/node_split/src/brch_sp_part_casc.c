/*----------------------------- Private Includes -----------------------------*/
#include "brch_sp_part_casc.h"
#include <stdio.h>
#include <unistd.h>
#include "bptree.h"
#include "unity.h"
#include "test_bptr_temp.h"
#include "test_bptr_brch_sp.h"
#include "bptr_node.h"
#include "bptr_static.h"
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
void test_part_casc_brch_split_end(struct bptr_temp *temp, const char *fnm);
void test_part_casc_brch_split_beg(struct bptr_temp *temp, const char *fnm);
void test_part_casc_brch_split_iter(struct bptr_temp *temp);
/*-------------------- Private Function Declarations END ---------------------*/


/*-------------------------------- Test Units --------------------------------*/
void test_part_casc_brch_split(void)
{
   // Use only the smallest-fanout configs: lite_128, norm_128.
   struct bptr_temp *test_matrix[] = { lite_temps_iu + 1, norm_temps_iu + 1 };
   size_t test_sz_matrix[] = { 1, 1 };
   puts("Test Unit: Partial Cascading Internal Node Split"
        " (test_part_casc_brch_split)");

   // Create and verify template .bptr files
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         _bptr_part_full_brch_casc_create(test_matrix[m_it] + tp_it);
         _bptr_part_full_brch_casc_verify(test_matrix[m_it] + tp_it);
       }
    }

   // test_part_casc_brch_split_end
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         TEST_ASSERT_EQUAL_MESSAGE(0,
            temp_instantiate(test_matrix[m_it] + tp_it, "temp_part_casc",
                             path, sizeof(path)),
            "temp_instantiate failure");
         test_part_casc_brch_split_end(test_matrix[m_it] + tp_it, path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
            "failed to remove instantiated template");
       }
    }

   // test_part_casc_brch_split_beg
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         TEST_ASSERT_EQUAL_MESSAGE(0,
            temp_instantiate(test_matrix[m_it] + tp_it, "temp_part_casc",
                             path, sizeof(path)),
            "temp_instantiate failure");
         test_part_casc_brch_split_beg(test_matrix[m_it] + tp_it, path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
            "failed to remove instantiated template");
       }
    }

   // test_part_casc_brch_split_iter
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
         test_part_casc_brch_split_iter(test_matrix[m_it] + tp_it);
    }

   // Cleanup
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         snprintf(path, sizeof(path), "bptr_files/temp_part_casc/%s",
                  test_matrix[m_it][tp_it].fnm);
         TEST_ASSERT_EQUAL_MESSAGE(0, remove(path),
                                   "failed to remove template");
         {
            char dpath[256];
            snprintf(dpath, sizeof(dpath), "bptr_files/temp_part_casc_%s",
                     test_matrix[m_it][tp_it].fnm);
            rmdir(dpath);
         }
       }
    }
}
/*------------------------------ Test Units END ------------------------------*/


/*------------------------------ Test Processes ------------------------------*/
// Trigger a leaf split in a height-3 partially-full tree (root has room).
// The cascading split propagates leaf→L1 but stops at the root because the
// root is non-full. The spawned leaf will be the rightmost node.
void test_part_casc_brch_split_end(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                  temp->cache_cap, temp->cmp);
   struct bptr_node *par_n, *node, *root_n;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

   // ---- Pre-split: verify root is non-full (1 key, 2 children) ----
   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, root_n->key_count,
                                     "root not non-full (expected 1 key)");
   TEST_ASSERT_EQUAL_MESSAGE(2, root_n->level, "root level != 2");

   // Navigate to rightmost leaf: root->vals[1] = right L1,
   // then L1->vals[brch_full] = rightmost leaf
   par_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch rightmost L1");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, par_n->key_count, "rightmost L1 not full");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "rightmost L1 level != 1");

   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_full));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch rightmost leaf");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(leaf_full, node->key_count,
                                     "rightmost leaf not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "rightmost leaf not leaf");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "rightmost leaf level != 0");

   // Verify keys/values in rightmost leaf before split
   int64_t total_keys =
      2 * (int64_t)(brch_full + 1) * (int64_t)leaf_full;
   int64_t i = total_keys - leaf_full;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) before split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) before split");
    }

   // Split: insert key at end of rightmost leaf
   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(total_keys * 2 + 2),
                      temp->tools->node.val_wrapper_i64(total_keys * 3 + 3));
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx, "bptr_node_split failure");

   // Unload pre-split nodes — they may be stale after the cascading split
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, par_n);
   bptr_node_unload(bptr, root_n);
   node = par_n = root_n = NULL;

   // ---- Post-split: height must still be 3 (root did NOT split) ----
   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "post-split height != 3");


   // ===================================================================
   // Comprehensive post-split tree verification
   // ===================================================================

   // ---- 1. Verify root (now has 2 keys, 3 children; still level 2) ----
   struct bptr_node *new_root = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch root");
   TEST_ASSERT_EQUAL_MESSAGE(2, new_root->level, "root level != 2");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, new_root->key_count,
                                     "root key_count != 2 (should have absorbed one)");
   TEST_ASSERT_FALSE_MESSAGE(new_root->is_leaf, "root should not be leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->parent,
                                     "root parent != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->prev,
                                     "root prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->next,
                                     "root next != 0");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, new_root->flags,
                                  "root flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, new_root->flags,
                                 "root flags has LEAF set");

   // ---- 2. Verify L1 nodes: 3 total (was 2; rightmost L1 split) ----
   uint64_t l1_idx[3];
   struct bptr_node *l1_nodes[3];
   for (int li = 0; li < 3; li++)
    {
      l1_idx[li] = _node_brch_vals_get(bptr, new_root, li);
      l1_nodes[li] = bptr_node_fetch(bptr, l1_idx[li]);
      TEST_ASSERT_NOT_NULL_MESSAGE(l1_nodes[li], "failed to fetch L1");
    }

   // L1 structural invariants
   for (int l1_i = 0; l1_i < 3; l1_i++)
    {
      struct bptr_node *l1_n = l1_nodes[l1_i];
      TEST_ASSERT_EQUAL_MESSAGE(1, l1_n->level, "L1 level != 1");
      TEST_ASSERT_FALSE_MESSAGE(l1_n->is_leaf, "L1 should not be leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, l1_n->parent,
                                        "L1 parent != root");
      TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, l1_n->flags,
                                     "L1 flags missing VALID");
      TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, l1_n->flags,
                                    "L1 flags has LEAF set");
    }

   // L1 prev/next linkage
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, l1_nodes[0]->prev,
                                     "first L1 prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[1]->node_idx, l1_nodes[0]->next,
                                     "L1[0] next != L1[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[0]->node_idx, l1_nodes[1]->prev,
                                     "L1[1] prev != L1[0]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[2]->node_idx, l1_nodes[1]->next,
                                     "L1[1] next != L1[2]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[1]->node_idx, l1_nodes[2]->prev,
                                     "L1[2] prev != L1[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, l1_nodes[2]->next,
                                     "last L1 next != 0");

   // L1 key_count: the left L1 is unchanged (brch_full keys);
   // the two right L1s (split halves) together hold brch_full keys.
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(brch_full, l1_nodes[0]->key_count,
      "left L1 key_count changed (should be unchanged)");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      brch_full, l1_nodes[1]->key_count + l1_nodes[2]->key_count,
      "split L1 key_count sum != brch_full");

   // ---- 3. Verify root's separator keys ----
   // root->keys[0] = first key of L1[1]'s first leaf
   // root->keys[1] = first key of L1[2]'s first leaf
   for (int sep_i = 0; sep_i < 2; sep_i++)
    {
      struct bptr_node *sep_l1 = l1_nodes[sep_i + 1];
      struct bptr_node *sep_leaf = bptr_node_fetch(bptr,
         _node_brch_vals_get(bptr, sep_l1, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(sep_leaf,
         "failed to fetch first leaf for root separator check");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         temp->tools->node.cast_i64(new_root->keys + bptr->key_size * sep_i),
         temp->tools->node.cast_i64(sep_leaf->keys),
         "root separator key does not match first key of right subtree");
      bptr_node_unload(bptr, sep_leaf);
    }

   // ---- 4. Traverse every node: verify metadata, parent links, keys/vals ----
   uint64_t first_l1_idx = l1_idx[0];
   int total_l1_count = 3;
   int64_t global_i = 0;
   int64_t total_expected = total_keys + 1;

   for (int l1_i = 0; l1_i < 3; l1_i++)
    {
      struct bptr_node *l1_n = l1_nodes[l1_i];

      // L1 node_idx consistency
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         l1_idx[l1_i], l1_n->node_idx,
         "L1 node_idx does not match parent's vals entry");

      for (uint32_t leaf_i = 0; leaf_i <= l1_n->key_count; leaf_i++)
       {
         uint64_t leaf_idx = _node_brch_vals_get(bptr, l1_n, leaf_i);
         struct bptr_node *leaf = bptr_node_fetch(bptr, leaf_idx);
         TEST_ASSERT_NOT_NULL_MESSAGE(leaf, "failed to fetch leaf");

         // Leaf metadata
         TEST_ASSERT_EQUAL_MESSAGE(0, leaf->level, "leaf level != 0");
         TEST_ASSERT_TRUE_MESSAGE(leaf->is_leaf, "leaf should be leaf");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_n->node_idx, leaf->parent,
                                           "leaf parent != L1");
         TEST_ASSERT_BITS_HIGH_MESSAGE(
            BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
            leaf->flags, "leaf flags incorrect");

         // Leaf node_idx consistency
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            leaf_idx, leaf->node_idx,
            "leaf node_idx does not match L1 vals entry");

         // Verify L1 separator key matches first key of leaf child
         if (leaf_i > 0)
            TEST_ASSERT_EQUAL_INT64_MESSAGE(
               temp->tools->node.cast_i64(l1_n->keys + bptr->key_size * (leaf_i - 1)),
               temp->tools->node.cast_i64(leaf->keys),
               "L1 separator key does not match first key of leaf child");

         // Verify all key/value pairs in this leaf
         for (uint32_t k_i = 0; k_i < leaf->key_count; k_i++, global_i++)
          {
            int64_t actual_key = temp->tools->node.cast_i64(
               leaf->keys + bptr->key_size * k_i);
            int64_t actual_val = temp->tools->node.cast_i64(
               leaf->vals + bptr->value_size * k_i);
            // Insert at rightmost: keys/values continue the original sequence
            int64_t expected_key = global_i * 2 + 2;
            int64_t expected_val = global_i * 3 + 3;
            TEST_ASSERT_EQUAL_INT64_MESSAGE(expected_key, actual_key,
               "leaf key mismatch (tree traversal)");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(expected_val, actual_val,
               "leaf val mismatch (tree traversal)");
          }

         bptr_node_unload(bptr, leaf);
       }
    }

   // Verify total record count via parent→child traversal
   TEST_ASSERT_EQUAL_INT64_MESSAGE(total_expected, global_i,
      "total records via tree traversal != expected");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE((uint64_t)total_expected, bptr->record_cnt,
      "bptr->record_cnt mismatch after split");

   // ---- 5. Verify L1 linked list (end-to-end via prev/next) ----
   {
      int l1_chain_count = 0;
      uint64_t cur = first_l1_idx;
      uint64_t prev_idx = 0;
      while (cur != 0)
       {
         struct bptr_node *l1 = bptr_node_fetch(bptr, cur);
         TEST_ASSERT_NOT_NULL_MESSAGE(l1, "L1 linked list walk: fetch failed");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_idx, l1->prev,
            "L1 linked list: prev mismatch");
         TEST_ASSERT_EQUAL_MESSAGE(1, l1->level, "L1 linked list: level != 1");
         TEST_ASSERT_FALSE_MESSAGE(l1->is_leaf, "L1 linked list: should not be leaf");
         l1_chain_count++;
         prev_idx = cur;
         cur = l1->next;
         bptr_node_unload(bptr, l1);
       }
      TEST_ASSERT_EQUAL_INT_MESSAGE(total_l1_count, l1_chain_count,
         "L1 linked list count != total L1 found via tree traversal");
   }

   // ---- 6. Verify leaf linked-list prev/next continuity ----
   // Walk backwards from the split-created leaf to find the true chain head,
   // then walk forwards to verify the full chain.
   {
      uint64_t cur = n_idx;
      while (1)
       {
         struct bptr_node *leaf = bptr_node_fetch(bptr, cur);
         TEST_ASSERT_NOT_NULL_MESSAGE(leaf, "leaf chain reverse: fetch failed");
         if (leaf->prev == 0) { bptr_node_unload(bptr, leaf); break; }
         cur = leaf->prev;
         bptr_node_unload(bptr, leaf);
       }
      uint64_t first_leaf_idx = cur;
      int64_t chain_records = 0;
      uint64_t prev_idx = 0;
      while (cur != 0)
       {
         struct bptr_node *leaf = bptr_node_fetch(bptr, cur);
         TEST_ASSERT_NOT_NULL_MESSAGE(leaf, "leaf chain forward: fetch failed");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_idx, leaf->prev,
            "leaf linked list: prev mismatch");
         TEST_ASSERT_EQUAL_MESSAGE(0, leaf->level, "leaf chain: level != 0");
         TEST_ASSERT_TRUE_MESSAGE(leaf->is_leaf, "leaf chain: should be leaf");
         chain_records += leaf->key_count;
         prev_idx = cur;
         cur = leaf->next;
         bptr_node_unload(bptr, leaf);
       }
      // Cross-check: the global first leaf (via parent→child from
      // first_l1_idx) must match the chain head found by reverse-walk.
      {
         struct bptr_node *fl1 = bptr_node_fetch(bptr, first_l1_idx);
         TEST_ASSERT_NOT_NULL_MESSAGE(fl1,
            "leaf chain cross-check: failed to fetch first L1");
         uint64_t tree_first_leaf = _node_brch_vals_get(bptr, fl1, 0);
         bptr_node_unload(bptr, fl1);
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(first_leaf_idx, tree_first_leaf,
            "global first leaf != leaf chain head");
      }
      TEST_ASSERT_EQUAL_INT64_MESSAGE(total_expected, chain_records,
         "leaf chain record count != total expected");
   }

   // Cleanup
   for (int l1_i = 0; l1_i < 3; l1_i++)
      bptr_node_unload(bptr, l1_nodes[l1_i]);
   bptr_node_unload(bptr, new_root);

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Trigger a leaf split in a height-3 partially-full tree (root has room).
// The spawned leaf will be the leftmost node.
void test_part_casc_brch_split_beg(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                  temp->cache_cap, temp->cmp);
   struct bptr_node *root_n, *l1_n, *node;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

   // ---- Pre-split verification ----
   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, root_n->key_count,
                                     "root key_count != 1 (should be non-full)");

   // Navigate to leftmost leaf: root->vals[0] = left L1,
   // then L1->vals[0] = leftmost leaf
   l1_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(l1_n, "failed to fetch leftmost L1");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, l1_n->key_count, "leftmost L1 not full");

   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leftmost leaf");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(leaf_full, node->key_count,
                                     "leftmost leaf not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "leftmost leaf not leaf");

   // Verify keys/values in leftmost leaf before split
   {
      int64_t j = 0;
      for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, j++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            j * 2 + 2,
            temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "Invalid node (key) before split");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            j * 3 + 3,
            temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
            "Invalid node (value) before split");
       }
   }

   // Split with key=0, val=0 (inserts before all existing keys)
   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(0),
                      temp->tools->node.val_wrapper_i64(0));
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx, "bptr_node_split failure");

   // Unload pre-split nodes — they may be stale after the cascading split
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, l1_n);
   bptr_node_unload(bptr, root_n);
   node = l1_n = root_n = NULL;

   // After split, height must still be 3 (root did NOT split)
   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "post-split height != 3");


   // ===================================================================
   // Comprehensive post-split tree verification
   // ===================================================================

   // ---- 1. Verify root ----
   struct bptr_node *new_root = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch root");
   TEST_ASSERT_EQUAL_MESSAGE(2, new_root->level, "root level != 2");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, new_root->key_count,
                                     "root key_count != 2");
   TEST_ASSERT_FALSE_MESSAGE(new_root->is_leaf, "root should not be leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->parent,
                                     "root parent != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->prev,
                                     "root prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->next,
                                     "root next != 0");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, new_root->flags,
                                  "root flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, new_root->flags,
                                 "root flags has LEAF set");

   // ---- 2. Verify L1 nodes: 3 total (leftmost L1 split) ----
   uint64_t l1_idx[3];
   struct bptr_node *l1_nodes[3];
   for (int li = 0; li < 3; li++)
    {
      l1_idx[li] = _node_brch_vals_get(bptr, new_root, li);
      l1_nodes[li] = bptr_node_fetch(bptr, l1_idx[li]);
      TEST_ASSERT_NOT_NULL_MESSAGE(l1_nodes[li], "failed to fetch L1");
    }

   for (int l1_i = 0; l1_i < 3; l1_i++)
    {
      struct bptr_node *l1_n = l1_nodes[l1_i];
      TEST_ASSERT_EQUAL_MESSAGE(1, l1_n->level, "L1 level != 1");
      TEST_ASSERT_FALSE_MESSAGE(l1_n->is_leaf, "L1 should not be leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, l1_n->parent,
                                        "L1 parent != root");
      TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, l1_n->flags,
                                     "L1 flags missing VALID");
      TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, l1_n->flags,
                                    "L1 flags has LEAF set");
    }

   // L1 prev/next linkage
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, l1_nodes[0]->prev,
                                     "first L1 prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[1]->node_idx, l1_nodes[0]->next,
                                     "L1[0] next != L1[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[0]->node_idx, l1_nodes[1]->prev,
                                     "L1[1] prev != L1[0]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[2]->node_idx, l1_nodes[1]->next,
                                     "L1[1] next != L1[2]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[1]->node_idx, l1_nodes[2]->prev,
                                     "L1[2] prev != L1[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, l1_nodes[2]->next,
                                     "last L1 next != 0");

   // L1 key_count: the two left L1s (split halves) together hold brch_full
   // keys; the right L1 is unchanged.
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      brch_full, l1_nodes[0]->key_count + l1_nodes[1]->key_count,
      "split L1 key_count sum != brch_full");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(brch_full, l1_nodes[2]->key_count,
      "right L1 key_count changed (should be unchanged)");

   // ---- 3. Verify root's separator keys ----
   for (int sep_i = 0; sep_i < 2; sep_i++)
    {
      struct bptr_node *sep_l1 = l1_nodes[sep_i + 1];
      struct bptr_node *sep_leaf = bptr_node_fetch(bptr,
         _node_brch_vals_get(bptr, sep_l1, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(sep_leaf,
         "failed to fetch first leaf for root separator check");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         temp->tools->node.cast_i64(new_root->keys + bptr->key_size * sep_i),
         temp->tools->node.cast_i64(sep_leaf->keys),
         "root separator key does not match first key of right subtree");
      bptr_node_unload(bptr, sep_leaf);
    }

   // ---- 4. Traverse every node: verify metadata, parent links, keys/vals ----
   uint64_t first_l1_idx = l1_idx[0];
   int total_l1_count = 3;
   int64_t total_keys =
      2 * (int64_t)(brch_full + 1) * (int64_t)leaf_full;
   int64_t global_i = 0;
   int64_t total_expected = total_keys + 1;

   for (int l1_i = 0; l1_i < 3; l1_i++)
    {
      struct bptr_node *l1_n = l1_nodes[l1_i];

      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         l1_idx[l1_i], l1_n->node_idx,
         "L1 node_idx does not match parent's vals entry");

      for (uint32_t leaf_i = 0; leaf_i <= l1_n->key_count; leaf_i++)
       {
         uint64_t leaf_idx = _node_brch_vals_get(bptr, l1_n, leaf_i);
         struct bptr_node *leaf = bptr_node_fetch(bptr, leaf_idx);
         TEST_ASSERT_NOT_NULL_MESSAGE(leaf, "failed to fetch leaf");

         TEST_ASSERT_EQUAL_MESSAGE(0, leaf->level, "leaf level != 0");
         TEST_ASSERT_TRUE_MESSAGE(leaf->is_leaf, "leaf should be leaf");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_n->node_idx, leaf->parent,
                                           "leaf parent != L1");
         TEST_ASSERT_BITS_HIGH_MESSAGE(
            BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
            leaf->flags, "leaf flags incorrect");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            leaf_idx, leaf->node_idx,
            "leaf node_idx does not match L1 vals entry");

         if (leaf_i > 0)
            TEST_ASSERT_EQUAL_INT64_MESSAGE(
               temp->tools->node.cast_i64(l1_n->keys + bptr->key_size * (leaf_i - 1)),
               temp->tools->node.cast_i64(leaf->keys),
               "L1 separator key does not match first key of leaf child");

         // Verify key/value integrity.
         // After inserting key=0 at pos 0, the global key sequence shifts by 1.
         for (uint32_t k_i = 0; k_i < leaf->key_count; k_i++, global_i++)
          {
            int64_t actual_key = temp->tools->node.cast_i64(
               leaf->keys + bptr->key_size * k_i);
            int64_t actual_val = temp->tools->node.cast_i64(
               leaf->vals + bptr->value_size * k_i);
            int64_t expected_val;

            // Key must be even (original) or 0 (inserted)
            TEST_ASSERT_TRUE_MESSAGE(
               actual_key == 0 || actual_key % 2 == 0,
               "leaf key not even (expected 0 or even for _beg test)");

            if (actual_key == 0)
               expected_val = 0;
            else
               expected_val = actual_key * 3 / 2;

            TEST_ASSERT_EQUAL_INT64_MESSAGE(expected_val, actual_val,
               "leaf val mismatch (tree traversal)");

            // Key monotonicity within leaf
            if (k_i > 0) {
               int64_t prev = temp->tools->node.cast_i64(
                  leaf->keys + bptr->key_size * (k_i - 1));
               TEST_ASSERT_GREATER_THAN_INT64_MESSAGE(prev, actual_key,
                  "leaf keys not increasing within leaf");
            }
          }

         bptr_node_unload(bptr, leaf);
       }
    }

   TEST_ASSERT_EQUAL_INT64_MESSAGE(total_expected, global_i,
      "total records via tree traversal != expected");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE((uint64_t)total_expected, bptr->record_cnt,
      "bptr->record_cnt mismatch after split");

   // ---- 5. Verify L1 linked list ----
   {
      int l1_chain_count = 0;
      uint64_t cur = first_l1_idx;
      uint64_t prev_idx = 0;
      while (cur != 0)
       {
         struct bptr_node *l1 = bptr_node_fetch(bptr, cur);
         TEST_ASSERT_NOT_NULL_MESSAGE(l1, "L1 linked list walk: fetch failed");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_idx, l1->prev,
            "L1 linked list: prev mismatch");
         TEST_ASSERT_EQUAL_MESSAGE(1, l1->level, "L1 linked list: level != 1");
         TEST_ASSERT_FALSE_MESSAGE(l1->is_leaf, "L1 linked list: should not be leaf");
         l1_chain_count++;
         prev_idx = cur;
         cur = l1->next;
         bptr_node_unload(bptr, l1);
       }
      TEST_ASSERT_EQUAL_INT_MESSAGE(total_l1_count, l1_chain_count,
         "L1 linked list count != total L1 found via tree traversal");
   }

   // ---- 6. Verify leaf linked-list prev/next continuity ----
   {
      struct bptr_node *fl1 = bptr_node_fetch(bptr, first_l1_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(fl1, "leaf chain: failed to fetch first L1");
      uint64_t leaf_idx = _node_brch_vals_get(bptr, fl1, 0);
      bptr_node_unload(bptr, fl1);

      int64_t chain_records = 0;
      uint64_t prev_idx = 0;

      while (leaf_idx != 0)
       {
         struct bptr_node *leaf = bptr_node_fetch(bptr, leaf_idx);
         TEST_ASSERT_NOT_NULL_MESSAGE(leaf, "leaf linked list walk: fetch failed");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_idx, leaf->prev,
            "leaf linked list: prev mismatch");
         chain_records += leaf->key_count;
         prev_idx = leaf_idx;
         leaf_idx = leaf->next;
         bptr_node_unload(bptr, leaf);
       }
      TEST_ASSERT_EQUAL_INT64_MESSAGE(total_expected, chain_records,
         "leaf chain record count != total expected");
   }

   // Cleanup
   for (int l1_i = 0; l1_i < 3; l1_i++)
      bptr_node_unload(bptr, l1_nodes[l1_i]);
   bptr_node_unload(bptr, new_root);

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Iterate over all insertion positions from before all keys (pos=0, key=1)
// to after all keys (pos=k, key=2k+1) in the leftmost leaf. Each insertion
// causes a cascading split (leaf, L1) that stops at the non-full root.
void test_part_casc_brch_split_iter(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   uint32_t k;

   // Determine leaf capacity
   TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0,
      snprintf(path, sizeof(path), "bptr_files/_k_part_casc_%s", temp->fnm),
      "snprintf path failure");
   rmdir(path);  // defensive
   {
      struct bptr *kbptr = bptr_init(path, temp->is_lite,
                                      temp->node_sz, temp->key_sz,
                                      temp->val_sz, temp->cache_cap,
                                      temp->cmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(kbptr, "bptr_init for k determination");
      k = kbptr->node_bound.leaf.up - 1;
      TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(kbptr), "unload k bptr");
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path), "remove k bptr");
   }

   for (uint32_t pos = 0; pos <= k; pos++)
    {
      struct bptr_node *root_n, *l1_n, *node;

      rmdir(path);
      TEST_ASSERT_EQUAL_MESSAGE(0,
         temp_instantiate(temp, "temp_part_casc", path, sizeof(path)),
         "temp_instantiate failure");

      bptr = bptr_load(path, temp->cache_cap, temp->cmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
      TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");
      TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

      uint32_t brch_full = bptr->node_bound.brch.up - 1;
      uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

      // Pre-split: total records in the partially-full height-3 tree
      int64_t total_keys =
         2 * (int64_t)(brch_full + 1) * (int64_t)leaf_full;

      root_n = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, root_n->key_count,
         "root key_count != 1 (should be non-full)");

      l1_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(l1_n, "failed to fetch leftmost L1");

      node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leftmost leaf");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(k, node->key_count,
                                        "leftmost leaf not full");

      // Verify leftmost leaf keys/vals before split
      {
         int64_t j = 0;
         for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, j++)
          {
            TEST_ASSERT_EQUAL_INT64_MESSAGE(
               j * 2 + 2,
               temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
               "Invalid node (key) before split");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(
               j * 3 + 3,
               temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
               "Invalid node (value) before split");
          }
      }

      // Split with key=pos*2+1 that inserts at position pos
      bptr_node_t n_idx =
         bptr_node_split(bptr, node,
                         temp->tools->node.key_wrapper_i64(pos * 2 + 1),
                         temp->tools->node.val_wrapper_i64(pos * 2 + 1));
      TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx,
                                            "bptr_node_split failure");

      // Height must still be 3 (root did NOT split)
      TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height,
         "post-split height != 3 (root should not have split)");

      // Unload pre-split nodes
      bptr_node_unload(bptr, node);
      bptr_node_unload(bptr, l1_n);
      bptr_node_unload(bptr, root_n);
      node = l1_n = root_n = NULL;


      // ================================================================
      // Comprehensive post-split tree verification
      // ================================================================

      // ---- 1. Verify root ----
      struct bptr_node *new_root = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch root");
      TEST_ASSERT_EQUAL_MESSAGE(2, new_root->level, "root level != 2");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, new_root->key_count,
                                        "root key_count != 2");
      TEST_ASSERT_FALSE_MESSAGE(new_root->is_leaf, "root should not be leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->parent,
                                        "root parent != 0");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->prev,
                                        "root prev != 0");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->next,
                                        "root next != 0");
      TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, new_root->flags,
                                     "root flags missing VALID");
      TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, new_root->flags,
                                    "root flags has LEAF set");

      // ---- 2. Verify L1 nodes: 3 total ----
      uint64_t l1_idx[3];
      struct bptr_node *l1_nodes[3];
      for (int li = 0; li < 3; li++)
       {
         l1_idx[li] = _node_brch_vals_get(bptr, new_root, li);
         l1_nodes[li] = bptr_node_fetch(bptr, l1_idx[li]);
         TEST_ASSERT_NOT_NULL_MESSAGE(l1_nodes[li], "failed to fetch L1");
       }

      for (int l1_i = 0; l1_i < 3; l1_i++)
       {
         struct bptr_node *l1_n = l1_nodes[l1_i];
         TEST_ASSERT_EQUAL_MESSAGE(1, l1_n->level, "L1 level != 1");
         TEST_ASSERT_FALSE_MESSAGE(l1_n->is_leaf, "L1 should not be leaf");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, l1_n->parent,
                                           "L1 parent != root");
         TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, l1_n->flags,
                                        "L1 flags missing VALID");
         TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, l1_n->flags,
                                       "L1 flags has LEAF set");
       }

      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, l1_nodes[0]->prev,
                                        "first L1 prev != 0");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[1]->node_idx,
         l1_nodes[0]->next, "L1[0] next != L1[1]");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[0]->node_idx,
         l1_nodes[1]->prev, "L1[1] prev != L1[0]");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[2]->node_idx,
         l1_nodes[1]->next, "L1[1] next != L1[2]");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_nodes[1]->node_idx,
         l1_nodes[2]->prev, "L1[2] prev != L1[1]");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, l1_nodes[2]->next,
                                        "last L1 next != 0");

      // L1 key_count: the two left L1s (split halves) together hold
      // brch_full keys; the right L1 is unchanged.
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
         brch_full, l1_nodes[0]->key_count + l1_nodes[1]->key_count,
         "split L1 key_count sum != brch_full");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(brch_full, l1_nodes[2]->key_count,
         "right L1 key_count changed (should be unchanged)");

      // ---- 3. Verify root's separator keys ----
      for (int sep_i = 0; sep_i < 2; sep_i++)
       {
         struct bptr_node *sep_l1 = l1_nodes[sep_i + 1];
         struct bptr_node *sep_leaf = bptr_node_fetch(bptr,
            _node_brch_vals_get(bptr, sep_l1, 0));
         TEST_ASSERT_NOT_NULL_MESSAGE(sep_leaf,
            "failed to fetch first leaf for root separator check");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            temp->tools->node.cast_i64(new_root->keys + bptr->key_size * sep_i),
            temp->tools->node.cast_i64(sep_leaf->keys),
            "root separator key does not match first key of right subtree");
         bptr_node_unload(bptr, sep_leaf);
       }

      // ---- 4. Traverse every node: verify keys/vals ----
      uint64_t first_l1_idx = l1_idx[0];
      int total_l1_count = 3;
      int64_t global_i = 0;
      int64_t total_expected = total_keys + 1;
      _Bool found_inserted_key = 0;

      for (int l1_i = 0; l1_i < 3; l1_i++)
       {
         struct bptr_node *l1_n = l1_nodes[l1_i];

         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            l1_idx[l1_i], l1_n->node_idx,
            "L1 node_idx does not match parent's vals entry");

         for (uint32_t leaf_i = 0; leaf_i <= l1_n->key_count; leaf_i++)
          {
            uint64_t leaf_idx = _node_brch_vals_get(bptr, l1_n, leaf_i);
            struct bptr_node *leaf = bptr_node_fetch(bptr, leaf_idx);
            TEST_ASSERT_NOT_NULL_MESSAGE(leaf, "failed to fetch leaf");

            TEST_ASSERT_EQUAL_MESSAGE(0, leaf->level, "leaf level != 0");
            TEST_ASSERT_TRUE_MESSAGE(leaf->is_leaf, "leaf should be leaf");
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_n->node_idx, leaf->parent,
                                              "leaf parent != L1");
            TEST_ASSERT_BITS_HIGH_MESSAGE(
               BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
               leaf->flags, "leaf flags incorrect");
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(
               leaf_idx, leaf->node_idx,
               "leaf node_idx does not match L1 vals entry");

            if (leaf_i > 0)
               TEST_ASSERT_EQUAL_INT64_MESSAGE(
                  temp->tools->node.cast_i64(l1_n->keys + bptr->key_size * (leaf_i - 1)),
                  temp->tools->node.cast_i64(leaf->keys),
                  "L1 separator key does not match first key of leaf child");

            for (uint32_t k_i = 0; k_i < leaf->key_count; k_i++, global_i++)
             {
               int64_t actual_key = temp->tools->node.cast_i64(
                  leaf->keys + bptr->key_size * k_i);
               int64_t actual_val = temp->tools->node.cast_i64(
                  leaf->vals + bptr->value_size * k_i);

               int64_t expected_val;
               if (actual_key % 2 == 0)
                  expected_val = actual_key * 3 / 2;
               else {
                  expected_val = actual_key;
                  if (actual_key == (int64_t)pos * 2 + 1)
                     found_inserted_key = 1;
               }
               TEST_ASSERT_EQUAL_INT64_MESSAGE(expected_val, actual_val,
                  "leaf val mismatch (tree traversal)");

               if (k_i > 0) {
                  int64_t prev = temp->tools->node.cast_i64(
                     leaf->keys + bptr->key_size * (k_i - 1));
                  TEST_ASSERT_GREATER_THAN_INT64_MESSAGE(prev, actual_key,
                     "leaf keys not strictly increasing within leaf");
               }
             }

            bptr_node_unload(bptr, leaf);
          }
       }

      TEST_ASSERT_EQUAL_INT64_MESSAGE(total_expected, global_i,
         "total records via tree traversal != expected");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE((uint64_t)total_expected,
         bptr->record_cnt, "bptr->record_cnt mismatch after split");
      TEST_ASSERT_TRUE_MESSAGE(found_inserted_key,
         "inserted key not found in tree traversal");

      // ---- 5. Verify L1 linked list ----
      {
         int l1_chain_count = 0;
         uint64_t cur = first_l1_idx;
         uint64_t prev_idx = 0;
         while (cur != 0)
          {
            struct bptr_node *l1 = bptr_node_fetch(bptr, cur);
            TEST_ASSERT_NOT_NULL_MESSAGE(l1, "L1 linked list walk: fetch failed");
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_idx, l1->prev,
               "L1 linked list: prev mismatch");
            TEST_ASSERT_EQUAL_MESSAGE(1, l1->level, "L1 linked list: level != 1");
            TEST_ASSERT_FALSE_MESSAGE(l1->is_leaf, "L1 linked list: should not be leaf");
            l1_chain_count++;
            prev_idx = cur;
            cur = l1->next;
            bptr_node_unload(bptr, l1);
          }
         TEST_ASSERT_EQUAL_INT_MESSAGE(total_l1_count, l1_chain_count,
            "L1 linked list count != total L1 found via tree traversal");
      }

      // ---- 6. Verify leaf linked-list prev/next continuity ----
      {
         struct bptr_node *fl1 = bptr_node_fetch(bptr, first_l1_idx);
         TEST_ASSERT_NOT_NULL_MESSAGE(fl1, "leaf chain: failed to fetch first L1");
         uint64_t leaf_idx = _node_brch_vals_get(bptr, fl1, 0);
         bptr_node_unload(bptr, fl1);

         int64_t chain_records = 0;
         uint64_t prev_idx = 0;

         while (leaf_idx != 0)
          {
            struct bptr_node *leaf = bptr_node_fetch(bptr, leaf_idx);
            TEST_ASSERT_NOT_NULL_MESSAGE(leaf, "leaf linked list walk: fetch failed");
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_idx, leaf->prev,
               "leaf linked list: prev mismatch");
            chain_records += leaf->key_count;
            prev_idx = leaf_idx;
            leaf_idx = leaf->next;
            bptr_node_unload(bptr, leaf);
          }
         TEST_ASSERT_EQUAL_INT64_MESSAGE(total_expected, chain_records,
            "leaf chain record count != total expected");
      }

      // Cleanup for this iteration
      for (int l1_i = 0; l1_i < 3; l1_i++)
         bptr_node_unload(bptr, l1_nodes[l1_i]);
      bptr_node_unload(bptr, new_root);

      TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr),
                                 "Failed to bptr_unload");
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
                                     "failed to remove instantiated template");
    }
}
/*---------------------------- Test Processes END ----------------------------*/
