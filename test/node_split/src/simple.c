/*----------------------------- Private Includes -----------------------------*/
#include "simple.h"
#include <stdio.h>
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"
#include "test_util.h"
#include "bptr_node.h"
#include "bptr_static.h"
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
void test_simp_split_end(struct bptr_temp *temp);
/*-------------------- Private Function Declarations END ---------------------*/


/*-------------------------------- Test Units --------------------------------*/
// Insert a record into a full leaf node
// only one node in the tree
void test_simp_split(void)
{
   struct bptr_temp *test_matrix[] = { lite_temps_iu, norm_temps_iu };
   size_t test_sz_matrix[] = { lite_temps_iu_sz, norm_temps_iu_sz };
   puts("Test Unit: Simple Node Split (test_simp_split)");

   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
         test_simp_split_end(test_matrix[m_it] + tp_it);
    }
}
/*------------------------------ Test Units END ------------------------------*/


/*----------------------------- Test Proccesses ------------------------------*/
// New element at the end (i.e., greater than all existing elements)
void test_simp_split_end(struct bptr_temp *temp)
{
   struct bptr *bptr = _bptr_create(temp);
   struct bptr_node *node;

   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");
   node = bptr_node_new(bptr, 0);
   TEST_ASSERT_MESSAGE(node, "failed at bptr_node_new");
   node->prev = node->next = 0;
   bptr->root_idx = node->node_idx;

   // Fill node
   temp->tools->node.val_ins_i64(node, 0, 0);
   for (int64_t i = 0, mx = bptr->node_bound.leaf.up - 1; i < mx; i++)
      _bptr_kv_ins_i64(node, temp->tools, i, i + 1, i);

   // Split
   TEST_ASSERT_MESSAGE(
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(0xFFFFFFFF),
                      temp->tools->node.val_wrapper_i64(0xFFFFFFFF)),
      "Failed at Split");
   TEST_ASSERT_EQUAL(0, bptr_node_unload(bptr, node));

   /*--------------------- Check Correctness after Split ---------------------*/
   // TODO: check stats members in bptr
   // Check Root
   node = bptr_node_load(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load root");
   TEST_ASSERT_FALSE_MESSAGE(node->is_leaf, "root after split is leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(1, node->level);
   TEST_ASSERT_EQUAL(0, node->parent);
   TEST_ASSERT_EQUAL(0, node->prev);
   TEST_ASSERT_EQUAL(0, node->next);
   TEST_ASSERT_BITS(0x3, 0x1, node->flags);
   TEST_ASSERT_EQUAL(1, node->key_count);

   TEST_ASSERT(bptr_node_unload(bptr, node));
   /*------------------- Check Correctness after Split END -------------------*/

   TEST_ASSERT_MESSAGE(bptr_unload(bptr) == 0,
                       "Failed to unload bptr");
}
/*--------------------------- Test Proccesses END ----------------------------*/
