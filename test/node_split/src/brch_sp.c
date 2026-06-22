/*----------------------------- Private Includes -----------------------------*/
#include "brch_sp.h"
#include <stdio.h>
#include "bptree.h"
#include "unity.h"
#include "test_bptr_temp.h"
#include "test_bptr_setup.h"
#include "bptr_node.h"
#include "bptr_static.h"
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
void _bptr_full_brch_create(struct bptr_temp *temp);
void _bptr_full_brch_verify(struct bptr_temp *temp);
void test_sing_brch_split(struct bptr_temp *temp);
/*-------------------- Private Function Declarations END ---------------------*/


/*-------------------------------- Test Units --------------------------------*/
void test_brch_split(void)
{
   struct bptr_temp *test_matrix[] = { lite_temps_iu, norm_temps_iu };
   size_t test_sz_matrix[] = { lite_temps_iu_sz, norm_temps_iu_sz };
   puts("Test Unit: Internal Node Split (test_brch_split)");

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
}
/*------------------------------ Test Units END ------------------------------*/


/*------------------------------ Test Processes ------------------------------*/
// Trigger a leaf split that causes its parent (an internal node) to be full
// and split. The spawned leaf will be the last node.
void test_sing_brch_split(struct bptr_temp *temp)
{
}
/*---------------------------- Test Processes END ----------------------------*/


/*---------------------------- Private Utilities -----------------------------*/
void _bptr_full_brch_create(struct bptr_temp *temp)
{
   struct bptr *bptr = _bptr_create(temp);
   struct bptr_node *node, *par_n;
   int64_t i = 0;

   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");
   // Fill up the internal node at level==1
   // Since Redistribution is not available yet, manually create all the nodes.
   node = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "bptr_node_new failure");
   node->prev = 0;
   for (uint32_t leaf_i = 0, leaf_mx = bptr->node_bound.leaf.up - 1;
        leaf_i < leaf_mx; leaf_i++, i++)
      _bptr_kv_ins_i64(node, temp->tools, i, i * 2, leaf_i, bptr->is_lite);
   bptr->record_cnt += node->key_count;
   bptr->node_cnt++;

   par_n = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "bptr_node_new failure");
   par_n->prev = par_n->next = 0;
   node->parent = par_n->node_idx;
   _bptr_val_ins_ptr(par_n, node->node_idx, 0, bptr->is_lite);
   bptr->root_idx = par_n->node_idx;
   bptr->node_cnt++;

   for (uint32_t brch_i = 0, brch_mx = bptr->node_bound.brch.up - 1;
        brch_i < brch_mx; brch_i++)
    {
      struct bptr_node *next_n = bptr_node_new(bptr, par_n->node_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "bptr_node_new failure");
      node->next = next_n->node_idx;
      next_n->prev = node->node_idx;
      bptr_node_unload(bptr, node);
      node = next_n;

      for (uint32_t leaf_i = 0, leaf_mx = bptr->node_bound.leaf.up - 1;
           leaf_i < leaf_mx; leaf_i++, i++)
         _bptr_kv_ins_i64(node, temp->tools, i, i * 2, leaf_i, bptr->is_lite);
      bptr->record_cnt += node->key_count;
      bptr->node_cnt++;

      _bptr_kv_ins_i64(par_n, temp->tools,
                       temp->tools->node.cast_i64(node->keys),
                       node->node_idx, brch_i, bptr->is_lite);
    }
   node->next = 0;
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, par_n);

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "bptr_unload failure");
}


void _bptr_full_brch_verify(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   struct bptr_node *node, *par_n;
   int64_t i = 0;

   _bptr_path(path, sizeof(path), temp->fnm);
   bptr = bptr_load(path, 256, temp->cmp);
   TEST_ASSERT_MESSAGE(bptr, "failed at bptr_load");

   TEST_ASSERT_EQUAL_MESSAGE(2, bptr->height, "height != 2");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root index == 0");

   par_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch par_n");
   TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.brch.up - 1, par_n->key_count,
                             "par_n not full");

   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch first child node");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "child has incorrect parent");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev, "prev of first child not 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 1), node->next,
      "next of child does not match list in par_n");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      bptr->node_bound.leaf.up - 1, node->key_count, "child node not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child not leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "node index does not match par_n->vals");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "child level != 0");
   for (uint32_t leaf_i = 0, leaf_mx = node->key_count;
        leaf_i < leaf_mx; leaf_i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i, temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "child key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2, temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "child val not match");
      i++;
    }
   bptr_node_unload(bptr, node);
   for (uint32_t brch_i = 1, brch_mx = par_n->key_count + 1;
        brch_i < brch_mx; brch_i++)
    {
      node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_i));
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch child node");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         temp->tools->node.cast_i64(node->keys),
         temp->tools->node.cast_i64(par_n->keys + bptr->key_size * (brch_i - 1)),
         "Promoted key does not match");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                       "child has incorrect parent");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         _node_brch_vals_get(bptr, par_n, brch_i - 1), node->prev,
         "prev of first child not 0");
      if (brch_i < brch_mx - 1)
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            _node_brch_vals_get(bptr, par_n, brch_i + 1), node->next,
            "next of child does not match list in par_n");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         bptr->node_bound.leaf.up - 1, node->key_count, "child node not full");
      TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child not leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         _node_brch_vals_get(bptr, par_n, brch_i), node->node_idx,
         "node index does not match par_n->vals");
      TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "child level != 0");
      for (uint32_t leaf_i = 0, leaf_mx = node->key_count;
           leaf_i < leaf_mx; leaf_i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            i, temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "child key not match");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            i * 2,
            temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
            "child val not match");
         i++;
       }
      bptr_node_unload(bptr, node);
    }

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "bptr_unload failure");
}
/*-------------------------- Private Utilities END ---------------------------*/
