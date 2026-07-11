/*----------------------------- Private Includes -----------------------------*/
#include "brch_sp.h"
#include <stdio.h>
#include <unistd.h>
#include "bptree.h"
#include "unity.h"
#include "test_bptr_temp.h"
#include "test_bptr_setup.h"
#include "test_bptr_brch_sp.h"
#include "bptr_node.h"
#include "bptr_static.h"
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
void test_sing_brch_split_end(struct bptr_temp *temp, const char *fnm);
void test_sing_brch_split_beg(struct bptr_temp *temp, const char *fnm);
void test_sing_brch_split_iter(struct bptr_temp *temp);
/*-------------------- Private Function Declarations END ---------------------*/


/*-------------------------------- Test Units --------------------------------*/
void test_brch_split(void)
{
   struct bptr_temp *test_matrix[] = { lite_temps_iu, norm_temps_iu };
   size_t test_sz_matrix[] = { lite_temps_iu_sz, norm_temps_iu_sz };
   puts("Test Unit: Internal Node Split (test_brch_split)");

   // Create and verify template .bptr files
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         _bptr_full_brch_create(test_matrix[m_it] + tp_it);
         _bptr_full_brch_verify(test_matrix[m_it] + tp_it);
       }
    }

   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         TEST_ASSERT_EQUAL_MESSAGE(0,
            temp_instantiate(test_matrix[m_it] + tp_it, "temp",
                             path, sizeof(path)),
            "temp_instantiate failure");
         test_sing_brch_split_end(test_matrix[m_it] + tp_it, path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
            "failed to remove instantiated template");
       }
    }

   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         TEST_ASSERT_EQUAL_MESSAGE(0,
            temp_instantiate(test_matrix[m_it] + tp_it, "temp",
                             path, sizeof(path)),
            "temp_instantiate failure");
         test_sing_brch_split_beg(test_matrix[m_it] + tp_it, path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
            "failed to remove instantiated template");
       }
    }

   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
         test_sing_brch_split_iter(test_matrix[m_it] + tp_it);
    }

   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         _bptr_path_subdir(path, sizeof(path),
                           test_matrix[m_it][tp_it].fnm, "temp");
         TEST_ASSERT_EQUAL_MESSAGE(0, remove(path),
                                   "failed to remove template");
         /* Also clean up stale directory at instantiation dest path */
         {
            char dpath[256];
            snprintf(dpath, sizeof(dpath), "bptr_files/temp_%s",
                     test_matrix[m_it][tp_it].fnm);
            rmdir(dpath);
         }
       }
    }
}
/*------------------------------ Test Units END ------------------------------*/


/*------------------------------ Test Processes ------------------------------*/
// Trigger a leaf split that causes its parent (an internal node) to be full
// and split. The spawned leaf will be the right most node.
void test_sing_brch_split_end(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                 temp->cache_cap, temp->cmp);
   struct bptr_node *par_n, *node, *next_n, *root_n;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");
   par_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      bptr->node_bound.brch.up - 1, par_n->key_count, "root not full");

   node = bptr_node_fetch(bptr,
                          _node_brch_vals_get(bptr, par_n, par_n->key_count));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch child");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      bptr->node_bound.leaf.up - 1, node->key_count,
      "rightmost child not full");
   // check node correctness before split
   int64_t i = (par_n->key_count) * (bptr->node_bound.leaf.up - 1),
           i_before_split = i;
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

   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(i * 2 + 2),
                      temp->tools->node.val_wrapper_i64(i * 3 + 3));
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx, "`bptr_node_split failure'");

   // check node correctness after split
   next_n = bptr_node_fetch(bptr, n_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to load new node");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      bptr->node_bound.leaf.up, node->key_count + next_n->key_count,
      "sum of key_count of node and next_n incorrect");
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      node->key_count, next_n->key_count,
      "right child has more key than left child");
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      1, node->key_count - next_n->key_count,
      "(node->key_count - next_n->key_count)");
   i = i_before_split;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) after split");
    }
   for (uint32_t leaf_i = 0; leaf_i < next_n->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(next_n->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(next_n->vals + bptr->value_size * leaf_i),
         "Invalid node (value) after split");
    }
   TEST_ASSERT_NOT_EQUAL_HEX64_MESSAGE(par_n->node_idx, bptr->root_idx,
                                       "par_n is still root after split");
   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(root_n->key_count, 1, "root_n->key_count");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx,
      _node_brch_vals_get(bptr, root_n, 0),
      "par_n != first child of root_n");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(_node_brch_vals_get(bptr, root_n, 1),
      par_n->next,
      "par_n->next != (root_n->child)[1]");
   bptr_node_unload(bptr, par_n);
   par_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch (root_n->child)[1]");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, par_n->key_count,
                                               "par[1] has too few vals");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx,
      _node_brch_vals_get(bptr, par_n, par_n->key_count - 1),
      "node != second to last item of par[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx,
      _node_brch_vals_get(bptr, par_n, par_n->key_count),
      "next_n != last item of par[1]");
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, next_n);
   bptr_node_unload(bptr, par_n);
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch (root_n->child)[0]");
   next_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch (root_n->child)[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev,
                                    "(root_n->child)[0].prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
      "(root_n->child)[0].next != (root_n->child)[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, next_n->next,
                                    "(root_n->child)[1].next != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
      "(root_n->child)[1].prev != (root_n->child)[0]");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(bptr->node_bound.brch.up - 1,
      node->key_count + next_n->key_count,
      "(root_n->child)[0].key_count+(root_n->child)[1].key_count not full");
   bptr_node_unload(bptr, next_n);
   // check left internal node
   par_n = node;
   TEST_ASSERT_FALSE_MESSAGE(par_n->is_leaf, "par_n is_leaf should be false");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, root_n, 0), par_n->node_idx,
      "par_n->node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "par_n->level != 1");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, par_n->parent,
                                    "par_n->parent != root_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, par_n->flags,
                                 "par_n flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, par_n->flags,
                                "par_n flags has LEAF set");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, par_n->key_count,
                                               "par_n has too few keys");
   i = 0;
   // Leftmost leaf node
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leaf");
   TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, node->key_count,
                             "leaf node not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "first leaf is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "first leaf node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "first leaf level != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "first leaf parent != par_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      node->flags, "node flags incorrect");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev, "prev of first leaf not 0");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   for (uint32_t brch_i = 0; brch_i < par_n->key_count; brch_i++)
    {
      next_n =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_i + 1));
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
         "prev and par_n child[leaf_i] not match");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
         "next and par_n child[leaf_i] not match");
      bptr_node_unload(bptr, node);
      node = next_n;
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                       "node->parent != par idx");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
         bptr->node_bound.leaf.up - 1, node->key_count,
         "leaf node not full");
      TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         _node_brch_vals_get(bptr, par_n, brch_i + 1), node->node_idx,
         "node->node_idx incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
      TEST_ASSERT_BITS_HIGH_MESSAGE(
         BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
         node->flags, "node flags incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(
         temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
         temp->tools->node.cast_i64(node->keys),
         "incorrect internal node key");
      for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
            temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
            temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
            "leaf val not correct");
       }
    }
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, node->next,
                                 "next of last child of left internal node");
   next_n = bptr_node_fetch(bptr, node->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
      "prev of first child of right brch != last child if left brch");

   // Check right brch
   bptr_node_unload(bptr, node);
   node = bptr_node_fetch(bptr, par_n->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch right internal node");
   bptr_node_unload(bptr, par_n);
   par_n = node;
   TEST_ASSERT_FALSE_MESSAGE(par_n->is_leaf, "right brch is_leaf should be false");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, root_n, 1), par_n->node_idx,
      "right brch node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "right brch level != 1");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, par_n->parent,
                                    "right brch parent != root_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, par_n->flags,
                                 "right brch flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, par_n->flags,
                                "right brch flags has LEAF set");
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node,
                                "failed to fetch 0th child of right brch");
   TEST_ASSERT_EQUAL_PTR_MESSAGE(next_n, node,
      "next of last child of left brch != first child of right brch");
   bptr_node_unload(bptr, next_n);
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "node->parent != par idx");
   if (par_n->key_count > 1)  // spec. case: right brch has only 2 children
      TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, node->key_count,
                                "leaf node not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "node->node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      node->flags, "node flags incorrect");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   if (par_n->key_count > 2)
      for (uint32_t brch_i = 0; brch_i < par_n->key_count - 2; brch_i++)
       {
         next_n =
            bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_i + 1));
         TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
            "prev and par_n child[leaf_i] not match");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
            "next and par_n child[leaf_i] not match");
         bptr_node_unload(bptr, node);
         node = next_n;
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                          "node->parent != par idx");
         TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            bptr->node_bound.leaf.up - 1, node->key_count,
            "leaf node not full");
         TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            _node_brch_vals_get(bptr, par_n, brch_i + 1), node->node_idx,
            "node->node_idx incorrect");
         TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
         TEST_ASSERT_BITS_HIGH_MESSAGE(
            BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
            node->flags, "node flags incorrect");
         TEST_ASSERT_EQUAL_MESSAGE(
            temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
            temp->tools->node.cast_i64(node->keys),
            "incorrect internal node key");
         for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
          {
            TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
               temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
               "leaf key not correct");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
               temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
               "leaf val not correct");
          }
       }
   // Last two nodes
   if (par_n->key_count > 1)
    {
      next_n =
         bptr_node_fetch(bptr,
                         _node_brch_vals_get(bptr, par_n, par_n->key_count - 1));
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
         "prev and par_n child[leaf_i] not match");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
         "next and par_n child[leaf_i] not match");
      bptr_node_unload(bptr, node);
      node = next_n;
    }
   else  // else node is the first node that was checked and not yet unloaded
      i -= node->key_count;
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "node->parent != par idx");
   next_n = bptr_node_fetch(bptr, node->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
      "prev of last node");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->next, next_n->node_idx,
      "node_idx of last node");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, next_n->next, "next of last node");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(
      next_n->key_count, node->key_count,
      "node has less key than new_n after split");
   TEST_ASSERT_TRUE_MESSAGE(next_n->is_leaf, "last node is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, par_n->key_count), next_n->node_idx,
      "last node node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, next_n->level, "last node level != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, next_n->parent,
                                    "last node parent != par_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      next_n->flags, "last node flags incorrect");
   for (struct bptr_node *n_arr[2] = {node, next_n},
                         **it = n_arr, **ed = n_arr + 2;
        it < ed; it++)
    {
      for (uint32_t leaf_i = 0; leaf_i < (*it)->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
            temp->tools->node.cast_i64((*it)->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
            temp->tools->node.cast_i64((*it)->vals + bptr->value_size * leaf_i),
            "leaf val not correct");
       }
    }

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Trigger a leaf split that causes its parent (an internal node) to be full
// and split. The spawned leaf will be the left most node.
void test_sing_brch_split_beg(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                 temp->cache_cap, temp->cmp);
   struct bptr_node *par_n, *node, *next_n, *root_n;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");
   par_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      bptr->node_bound.brch.up - 1, par_n->key_count, "root not full");

   // Fetch leftmost child (first child of root)
   node = bptr_node_fetch(bptr,
                          _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch child");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      bptr->node_bound.leaf.up - 1, node->key_count,
      "leftmost child not full");
   // check node correctness before split
   int64_t i = 0;
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

   // Split with a key smaller than all existing keys (key=0 for i=-1)
   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(0),
                      temp->tools->node.val_wrapper_i64(0));
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx, "`bptr_node_split failure'");

   // check node correctness after split
   // next_n is the new node, always to the right of node
   next_n = bptr_node_fetch(bptr, n_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to load new node");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      bptr->node_bound.leaf.up, node->key_count + next_n->key_count,
      "sum of key_count of node and next_n incorrect");
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      node->key_count, next_n->key_count,
      "right child has more key than left child");
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      1, node->key_count - next_n->key_count,
      "(node->key_count - next_n->key_count)");
   // i starts at -1 because the new key at position 0 corresponds to i=-1
   i = -1;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) after split");
    }
   for (uint32_t leaf_i = 0; leaf_i < next_n->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(next_n->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(next_n->vals + bptr->value_size * leaf_i),
         "Invalid node (value) after split");
    }
   TEST_ASSERT_NOT_EQUAL_HEX64_MESSAGE(par_n->node_idx, bptr->root_idx,
                                       "par_n is still root after split");
   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(root_n->key_count, 1, "root_n->key_count");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx,
      _node_brch_vals_get(bptr, root_n, 0),
      "par_n != first child of root_n");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(_node_brch_vals_get(bptr, root_n, 1),
      par_n->next,
      "par_n->next != (root_n->child)[1]");
   // node and next_n are the first two children of the left internal node (par_n)
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx,
      _node_brch_vals_get(bptr, par_n, 0),
      "node != first item of par[0]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx,
      _node_brch_vals_get(bptr, par_n, 1),
      "next_n != second item of par[0]");
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, next_n);
   bptr_node_unload(bptr, par_n);
   // Load left internal node (par_n = first child of root)
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch (root_n->child)[0]");
   next_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch (root_n->child)[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev,
                                    "(root_n->child)[0].prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
      "(root_n->child)[0].next != (root_n->child)[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, next_n->next,
                                    "(root_n->child)[1].next != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
      "(root_n->child)[1].prev != (root_n->child)[0]");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(bptr->node_bound.brch.up - 1,
      node->key_count + next_n->key_count,
      "(root_n->child)[0].key_count+(root_n->child)[1].key_count not full");
   bptr_node_unload(bptr, next_n);
   // check left internal node (contains the split leaves)
   par_n = node;
   TEST_ASSERT_FALSE_MESSAGE(par_n->is_leaf, "par_n is_leaf should be false");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, root_n, 0), par_n->node_idx,
      "par_n->node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "par_n->level != 1");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, par_n->parent,
                                    "par_n->parent != root_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, par_n->flags,
                                 "par_n flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, par_n->flags,
                                "par_n flags has LEAF set");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, par_n->key_count,
                                               "par_n has too few keys");
   i = -1;
   // The first two children are the split leaves (node and next_n)
   // Verify node (first child of left internal node, part of split)
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leaf");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "first leaf is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "first leaf node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "first leaf level != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "first leaf parent != par_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      node->flags, "node flags incorrect");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev, "prev of first leaf not 0");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   // Verify next_n and remaining children of left internal node
   for (uint32_t brch_i = 0; brch_i < par_n->key_count; brch_i++)
    {
      next_n =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_i + 1));
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
         "prev and par_n child[leaf_i] not match");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
         "next and par_n child[leaf_i] not match");
      bptr_node_unload(bptr, node);
      node = next_n;
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                       "node->parent != par idx");
      if (brch_i > 0)  // first leaf (brch_i==0) was split; later ones are full
         TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            bptr->node_bound.leaf.up - 1, node->key_count,
            "leaf node not full");
      TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         _node_brch_vals_get(bptr, par_n, brch_i + 1), node->node_idx,
         "node->node_idx incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
      TEST_ASSERT_BITS_HIGH_MESSAGE(
         BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
         node->flags, "node flags incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(
         temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
         temp->tools->node.cast_i64(node->keys),
         "incorrect internal node key");
      for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
            temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
            temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
            "leaf val not correct");
       }
    }
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, node->next,
                                 "next of last child of left internal node");
   next_n = bptr_node_fetch(bptr, node->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
      "prev of first child of right brch != last child if left brch");

   // Check right brch (all children should be full)
   bptr_node_unload(bptr, node);
   node = bptr_node_fetch(bptr, par_n->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch right internal node");
   bptr_node_unload(bptr, par_n);
   par_n = node;
   TEST_ASSERT_FALSE_MESSAGE(par_n->is_leaf, "right brch is_leaf should be false");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, root_n, 1), par_n->node_idx,
      "right brch node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "right brch level != 1");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, par_n->parent,
                                    "right brch parent != root_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, par_n->flags,
                                 "right brch flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, par_n->flags,
                                "right brch flags has LEAF set");
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node,
                                "failed to fetch 0th child of right brch");
   TEST_ASSERT_EQUAL_PTR_MESSAGE(next_n, node,
      "next of last child of left brch != first child of right brch");
   bptr_node_unload(bptr, next_n);
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "node->parent != par idx");
   TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, node->key_count,
                             "leaf node not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "node->node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      node->flags, "node flags incorrect");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   if (par_n->key_count > 1)
      for (uint32_t brch_i = 0; brch_i < par_n->key_count - 1; brch_i++)
       {
         next_n =
            bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_i + 1));
         TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
            "prev and par_n child[leaf_i] not match");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
            "next and par_n child[leaf_i] not match");
         bptr_node_unload(bptr, node);
         node = next_n;
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                          "node->parent != par idx");
         TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            bptr->node_bound.leaf.up - 1, node->key_count,
            "leaf node not full");
         TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            _node_brch_vals_get(bptr, par_n, brch_i + 1), node->node_idx,
            "node->node_idx incorrect");
         TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
         TEST_ASSERT_BITS_HIGH_MESSAGE(
            BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
            node->flags, "node flags incorrect");
         TEST_ASSERT_EQUAL_MESSAGE(
            temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
            temp->tools->node.cast_i64(node->keys),
            "incorrect internal node key");
         for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
          {
            TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
               temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
               "leaf key not correct");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
               temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
               "leaf val not correct");
          }
       }
   // Last node of right internal node
   next_n = bptr_node_fetch(bptr, node->next);
   if (next_n)  // last node
    {
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
         "prev of last node");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->next, next_n->node_idx,
         "node_idx of last node");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, next_n->next, "next of last node");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, next_n->parent,
                                       "last node parent != par_n");
      TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, next_n->key_count,
                                "leaf node not full");
      TEST_ASSERT_TRUE_MESSAGE(next_n->is_leaf, "last node is_leaf not true");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         _node_brch_vals_get(bptr, par_n, par_n->key_count), next_n->node_idx,
         "last node node_idx incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(0, next_n->level, "last node level != 0");
      TEST_ASSERT_BITS_HIGH_MESSAGE(
         BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
         next_n->flags, "last node flags incorrect");
      for (uint32_t leaf_i = 0; leaf_i < next_n->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
            temp->tools->node.cast_i64(next_n->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
            temp->tools->node.cast_i64(next_n->vals + bptr->value_size * leaf_i),
            "leaf val not correct");
       }
      bptr_node_unload(bptr, next_n);
    }

   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, par_n);
   bptr_node_unload(bptr, root_n);
   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Iterate over all insertion positions from before all keys (pos=0, key=1)
// to after all keys in the leftmost leaf (pos=k, key=2k+1). For each position:
// instantiate, split the leftmost leaf, verify structural invariants, cleanup.
void test_sing_brch_split_iter(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   uint32_t k;

   // Determine leaf capacity.
   // Create a one-shot bptr to inspect node_bound, then discard.
   // Use a unique sub-path to avoid colliding with any stale artifacts.
   TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0,
      snprintf(path, sizeof(path), "bptr_files/_k_%s", temp->fnm),
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
      struct bptr_node *par_n, *node, *next_n;

      // Defensively clean up any stale directory left from a prior run
      rmdir(path);
      // (Re-)instantiate a fresh copy of the template
      TEST_ASSERT_EQUAL_MESSAGE(0,
         temp_instantiate(temp, "temp", path, sizeof(path)),
         "temp_instantiate failure");

      // Load, split, verify, cleanup in a tight scope
      bptr = bptr_load(path, temp->cache_cap, temp->cmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
      TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");

      par_n = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch root");

      // Fetch leftmost leaf
      node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
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

      // Verify split leaf nodes
      next_n = bptr_node_fetch(bptr, n_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to load new node");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
         bptr->node_bound.leaf.up, node->key_count + next_n->key_count,
         "sum of key_count incorrect");
      bptr_node_unload(bptr, next_n);
      bptr_node_unload(bptr, node);

      // Verify new root
      TEST_ASSERT_NOT_EQUAL_HEX64_MESSAGE(par_n->node_idx, bptr->root_idx,
                                          "par_n is still root after split");
      struct bptr_node *root_n = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch new root");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, root_n->key_count,
                                       "root_n->key_count != 1");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx,
         _node_brch_vals_get(bptr, root_n, 0),
         "par_n != first child of root_n");
      bptr_node_unload(bptr, root_n);
      bptr_node_unload(bptr, par_n);

      TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr),
                                "Failed to bptr_unload");
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
                                    "failed to remove instantiated template");
    }
}
/*---------------------------- Test Processes END ----------------------------*/
