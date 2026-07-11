/*----------------------------- Private Includes -----------------------------*/
#include "brch_sp_casc.h"
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
void test_casc_brch_split_end(struct bptr_temp *temp, const char *fnm);
void test_casc_brch_split_beg(struct bptr_temp *temp, const char *fnm);
void test_casc_brch_split_iter(struct bptr_temp *temp);
/*-------------------- Private Function Declarations END ---------------------*/


/*-------------------------------- Test Units --------------------------------*/
void test_casc_brch_split(void)
{
   // Use only the smallest-fanout configs: lite_128, norm_128.
   // Larger configs create massive trees that are too slow with
   // the current cache size.
   struct bptr_temp *test_matrix[] = { lite_temps_iu + 1, norm_temps_iu + 1 };
   size_t test_sz_matrix[] = { 1, 1 };
   puts("Test Unit: Cascading Internal Node Split (test_casc_brch_split)");

   // Create and verify template .bptr files
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         _bptr_full_brch_casc_create(test_matrix[m_it] + tp_it);
         _bptr_full_brch_casc_verify(test_matrix[m_it] + tp_it);
       }
    }

   // test_casc_brch_split_end
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         TEST_ASSERT_EQUAL_MESSAGE(0,
            temp_instantiate(test_matrix[m_it] + tp_it, "temp_casc",
                             path, sizeof(path)),
            "temp_instantiate failure");
         test_casc_brch_split_end(test_matrix[m_it] + tp_it, path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
            "failed to remove instantiated template");
       }
    }

   // test_casc_brch_split_beg
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         TEST_ASSERT_EQUAL_MESSAGE(0,
            temp_instantiate(test_matrix[m_it] + tp_it, "temp_casc",
                             path, sizeof(path)),
            "temp_instantiate failure");
         test_casc_brch_split_beg(test_matrix[m_it] + tp_it, path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
            "failed to remove instantiated template");
       }
    }

   // test_casc_brch_split_iter
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
         test_casc_brch_split_iter(test_matrix[m_it] + tp_it);
    }

   // Cleanup
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         snprintf(path, sizeof(path), "bptr_files/temp_casc/%s",
                  test_matrix[m_it][tp_it].fnm);
         TEST_ASSERT_EQUAL_MESSAGE(0, remove(path),
                                   "failed to remove template");
         {
            char dpath[256];
            snprintf(dpath, sizeof(dpath), "bptr_files/temp_casc_%s",
                     test_matrix[m_it][tp_it].fnm);
            rmdir(dpath);
         }
       }
    }
}
/*------------------------------ Test Units END ------------------------------*/


/*------------------------------ Test Processes ------------------------------*/
// Trigger a leaf split in a HEIGHT-3 tree that causes cascading splits:
// leaf -> L1 -> root all split. The spawned leaf will be the rightmost node.
void test_casc_brch_split_end(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                  temp->cache_cap, temp->cmp);
   struct bptr_node *par_n, *node, *root_n;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, root_n->key_count, "root not full");
   TEST_ASSERT_EQUAL_MESSAGE(2, root_n->level, "root level != 2");

   // Navigate to rightmost leaf: root last child -> L1 last child -> leaf
   par_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, brch_full));
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
      (int64_t)(brch_full + 1) * (int64_t)(brch_full + 1) * (int64_t)leaf_full;
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

   // After cascading split, height should be 4
   TEST_ASSERT_EQUAL_MESSAGE(4, bptr->height, "post-split height != 4");
   TEST_ASSERT_NOT_EQUAL_HEX64_MESSAGE(root_n->node_idx, bptr->root_idx,
                                        "old root is still root after cascading split");

   // Verify new root structure after cascading split
   struct bptr_node *new_root = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch new root");
   TEST_ASSERT_EQUAL_MESSAGE(3, new_root->level, "new root level != 3");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, new_root->key_count,
                                     "new root key_count != 1");
   TEST_ASSERT_FALSE_MESSAGE(new_root->is_leaf, "new root should not be leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->parent,
                                     "new root parent != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->prev,
                                     "new root prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->next,
                                     "new root next != 0");

   // Verify new root's two children are the split halves of old root
   struct bptr_node *left_brch =
      bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 0));
   struct bptr_node *right_brch =
      bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(left_brch, "failed to fetch left child of new root");
   TEST_ASSERT_NOT_NULL_MESSAGE(right_brch, "failed to fetch right child of new root");
   TEST_ASSERT_EQUAL_MESSAGE(2, left_brch->level, "left brch level != 2");
   TEST_ASSERT_EQUAL_MESSAGE(2, right_brch->level, "right brch level != 2");
   TEST_ASSERT_FALSE_MESSAGE(left_brch->is_leaf, "left brch should not be leaf");
   TEST_ASSERT_FALSE_MESSAGE(right_brch->is_leaf, "right brch should not be leaf");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, left_brch->flags,
                                  "left brch flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, left_brch->flags,
                                 "left brch flags has LEAF set");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, right_brch->flags,
                                  "right brch flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, right_brch->flags,
                                 "right brch flags has LEAF set");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      brch_full, left_brch->key_count + right_brch->key_count,
      "left + right brch key_count != brch_full");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, left_brch->parent,
                                     "left brch parent != new root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, right_brch->parent,
                                     "right brch parent != new root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, left_brch->prev,
                                     "left brch prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(right_brch->node_idx, left_brch->next,
                                     "left brch next != right brch");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(left_brch->node_idx, right_brch->prev,
                                     "right brch prev != left brch");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, right_brch->next,
                                     "right brch next != 0");

   bptr_node_unload(bptr, left_brch);
   bptr_node_unload(bptr, right_brch);
   bptr_node_unload(bptr, new_root);
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, par_n);
   bptr_node_unload(bptr, root_n);

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Trigger a leaf split in a HEIGHT-3 tree that causes cascading splits.
// The spawned leaf will be the leftmost node.
void test_casc_brch_split_beg(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                  temp->cache_cap, temp->cmp);
   struct bptr_node *root_n, *l1_n, *node;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, root_n->key_count, "root not full");

   // Navigate to leftmost leaf: root first child -> first L1 -> first leaf
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

   // After cascading split, height should be 4
   TEST_ASSERT_EQUAL_MESSAGE(4, bptr->height, "post-split height != 4");
   TEST_ASSERT_NOT_EQUAL_HEX64_MESSAGE(root_n->node_idx, bptr->root_idx,
                                        "old root is still root after cascading split");

   // Verify new root structure after cascading split
   struct bptr_node *new_root = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch new root");
   TEST_ASSERT_EQUAL_MESSAGE(3, new_root->level, "new root level != 3");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, new_root->key_count,
                                     "new root key_count != 1");
   TEST_ASSERT_FALSE_MESSAGE(new_root->is_leaf, "new root should not be leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->parent,
                                     "new root parent != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->prev,
                                     "new root prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, new_root->next,
                                     "new root next != 0");

   // Verify new root's two children are the split halves of old root
   struct bptr_node *left_brch =
      bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 0));
   struct bptr_node *right_brch =
      bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(left_brch, "failed to fetch left child of new root");
   TEST_ASSERT_NOT_NULL_MESSAGE(right_brch, "failed to fetch right child of new root");
   TEST_ASSERT_EQUAL_MESSAGE(2, left_brch->level, "left brch level != 2");
   TEST_ASSERT_EQUAL_MESSAGE(2, right_brch->level, "right brch level != 2");
   TEST_ASSERT_FALSE_MESSAGE(left_brch->is_leaf, "left brch should not be leaf");
   TEST_ASSERT_FALSE_MESSAGE(right_brch->is_leaf, "right brch should not be leaf");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, left_brch->flags,
                                  "left brch flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, left_brch->flags,
                                 "left brch flags has LEAF set");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, right_brch->flags,
                                  "right brch flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, right_brch->flags,
                                 "right brch flags has LEAF set");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      brch_full, left_brch->key_count + right_brch->key_count,
      "left + right brch key_count != brch_full");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, left_brch->parent,
                                     "left brch parent != new root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, right_brch->parent,
                                     "right brch parent != new root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, left_brch->prev,
                                     "left brch prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(right_brch->node_idx, left_brch->next,
                                     "left brch next != right brch");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(left_brch->node_idx, right_brch->prev,
                                     "right brch prev != left brch");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, right_brch->next,
                                     "right brch next != 0");

   bptr_node_unload(bptr, left_brch);
   bptr_node_unload(bptr, right_brch);
   bptr_node_unload(bptr, new_root);
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, l1_n);
   bptr_node_unload(bptr, root_n);

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Iterate over all insertion positions from before all keys (pos=0, key=1)
// to after all keys (pos=k, key=2k+1) in the leftmost leaf. Each insertion
// causes a 3-layer cascading split (leaf, L1, root).
void test_casc_brch_split_iter(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   uint32_t k;

   // Determine leaf capacity
   TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0,
      snprintf(path, sizeof(path), "bptr_files/_k_casc_%s", temp->fnm),
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

      // Defensively clean up any stale directory left from a prior run
      rmdir(path);
      TEST_ASSERT_EQUAL_MESSAGE(0,
         temp_instantiate(temp, "temp_casc", path, sizeof(path)),
         "temp_instantiate failure");

      bptr = bptr_load(path, temp->cache_cap, temp->cmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
      TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");
      TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

      uint32_t brch_full = bptr->node_bound.brch.up - 1;

      root_n = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");

      l1_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(l1_n, "failed to fetch leftmost L1");

      node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leftmost leaf");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(k, node->key_count,
                                        "leftmost leaf not full");

      // Split with key=pos*2+1 that inserts at position pos
      bptr_node_t n_idx =
         bptr_node_split(bptr, node,
                         temp->tools->node.key_wrapper_i64(pos * 2 + 1),
                         temp->tools->node.val_wrapper_i64(pos * 2 + 1));
      TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx,
                                            "bptr_node_split failure");

      // Verify cascading split: height goes from 3 to 4
      TEST_ASSERT_EQUAL_MESSAGE(4, bptr->height, "post-split height != 4");
      TEST_ASSERT_NOT_EQUAL_HEX64_MESSAGE(root_n->node_idx, bptr->root_idx,
                                           "old root is still root after cascading split");

      // Verify split leaf plus new leaf sum to leaf.up
      {
         struct bptr_node *next_n = bptr_node_fetch(bptr, n_idx);
         TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to load new node");
         TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            bptr->node_bound.leaf.up, node->key_count + next_n->key_count,
            "sum of key_count of node and next_n incorrect");
         bptr_node_unload(bptr, next_n);
      }

      // Verify new root structure
      struct bptr_node *new_root = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch new root");
      TEST_ASSERT_EQUAL_MESSAGE(3, new_root->level, "new root level != 3");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, new_root->key_count,
                                        "new root key_count != 1");
      TEST_ASSERT_FALSE_MESSAGE(new_root->is_leaf, "new root should not be leaf");

      struct bptr_node *left_brch =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 0));
      struct bptr_node *right_brch =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 1));
      TEST_ASSERT_NOT_NULL_MESSAGE(left_brch, "failed to fetch left brch");
      TEST_ASSERT_NOT_NULL_MESSAGE(right_brch, "failed to fetch right brch");
      TEST_ASSERT_EQUAL_MESSAGE(2, left_brch->level, "left brch level != 2");
      TEST_ASSERT_EQUAL_MESSAGE(2, right_brch->level, "right brch level != 2");
      TEST_ASSERT_FALSE_MESSAGE(left_brch->is_leaf, "left brch should not be leaf");
      TEST_ASSERT_FALSE_MESSAGE(right_brch->is_leaf, "right brch should not be leaf");
      TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, left_brch->flags,
                                     "left brch flags missing VALID");
      TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, left_brch->flags,
                                    "left brch flags has LEAF set");
      TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, right_brch->flags,
                                     "right brch flags missing VALID");
      TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, right_brch->flags,
                                    "right brch flags has LEAF set");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
         brch_full, left_brch->key_count + right_brch->key_count,
         "left + right brch key_count != brch_full");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, left_brch->parent,
                                        "left brch parent != new root");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, right_brch->parent,
                                        "right brch parent != new root");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, left_brch->prev,
                                        "left brch prev != 0");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(right_brch->node_idx, left_brch->next,
                                        "left brch next != right brch");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(left_brch->node_idx, right_brch->prev,
                                        "right brch prev != left brch");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, right_brch->next,
                                        "right brch next != 0");

      bptr_node_unload(bptr, left_brch);
      bptr_node_unload(bptr, right_brch);
      bptr_node_unload(bptr, new_root);
      bptr_node_unload(bptr, node);
      bptr_node_unload(bptr, l1_n);
      bptr_node_unload(bptr, root_n);

      TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr),
                                 "Failed to bptr_unload");
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
                                     "failed to remove instantiated template");
    }
}
/*---------------------------- Test Processes END ----------------------------*/
