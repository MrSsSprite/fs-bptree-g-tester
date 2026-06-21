/*----------------------------- Private Includes -----------------------------*/
#include "brch_sp.h"
#include <stdio.h>
#include "unity.h"
#include "test_bptr_temp.h"
#include "test_bptr_setup.h"
#include "bptr_node.h"
#include "bptr_static.h"
/*--------------------------- Private Includes END ---------------------------*/


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
       }
    }
}
/*------------------------------ Test Units END ------------------------------*/


/*------------------------------ Test Processes ------------------------------*/
// Trigger a leaf split that causes its parent (an internal node) to be full
// and split. The spawned leaf will be the last node.
void test_sing_brch_split(struct bptr_temp *temp)
{
   struct bptr *bptr = _bptr_create(temp);
   struct bptr_node *node;
   int64_t i = 0;

   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");

   // Fill up the internal node at level==1
   node = bptr_node_new(bptr, 0);
   for (uint32_t elem_i = 0, elem_mx = bptr->node_bound.leaf.up - 1;
        elem_i < elem_mx; elem_i++)
    {
      _bptr_kv_ins_i64(node, temp->tools, i, i + 1, i);
      i++;
    }
   bptr->record_cnt = node->key_count;
   bptr->node_cnt++;
   TEST_ASSERT_MESSAGE(
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(i),
                      temp->tools->node.val_wrapper_i64(i + 1)),
      "Failed at Split");
   i++;
   bptr_node_unload(bptr, node);
   for (uint32_t leaf_i = 0, leaf_mx = bptr->node_bound.brch.up - 1;
        leaf_i < leaf_mx; leaf_i++)
    {
    }
}
/*---------------------------- Test Processes END ----------------------------*/
