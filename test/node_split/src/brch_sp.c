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
         test_sing_brch_split(test_matrix[m_it] + tp_it);
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
      _bptr_kv_ins_i64(node, temp->tools, i, i * 2, leaf_i);
   bptr->record_cnt += node->key_count;
   bptr->node_cnt++;

   par_n = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "bptr_node_new failure");
   par_n->prev = par_n->next = 0;
   node->parent = par_n->node_idx;
   temp->tools->node.val_ins_i64(par_n, node->node_idx, 0);
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
         _bptr_kv_ins_i64(node, temp->tools, i, i * 2, leaf_i);
      bptr->record_cnt += node->key_count;
      bptr->node_cnt++;

      _bptr_kv_ins_i64(par_n, temp->tools,
                       temp->tools->node.cast_i64(node->keys),
                       node->node_idx, brch_i);
    }
   node->next = 0;
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, par_n);

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "bptr_unload failure");
}
/*-------------------------- Private Utilities END ---------------------------*/
