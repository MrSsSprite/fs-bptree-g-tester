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
void test_simp_split_proc(struct bptr_temp *temp);
/*-------------------- Private Function Declarations END ---------------------*/


/*-------------------------------- Test Units --------------------------------*/
// Insert a record into a full leaf node
// only one node in the tree
void test_simp_split(void)
{
   puts("Test Unit: Simple Node Split (test_simp_split)");
}
/*------------------------------ Test Units END ------------------------------*/


/*----------------------------- Test Proccesses ------------------------------*/
// New element at the end (i.e., greater than all existing elements)
void test_simp_split_end(struct bptr_temp *temp)
{
   struct bptr *bptr = _bptr_create(temp);
   struct bptr_node *node;

   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");
   node = bptr_node_new(bptr, 1, 0);
   TEST_ASSERT_MESSAGE(node, "failed at bptr_node_new");
   bptr->root_idx = node->node_idx;

   // Fill node
   temp->tools->node.val_ins_i64(node, -1, 0);
   for (int64_t i = 0, mx = bptr->node_bound.leaf.up - 1; i < mx; i++)
      _bptr_kv_ins_i64(node, temp->tools, i, i * 2, i);

   // Split
   bptr_node_split(bptr, node, -2, -2);
}
/*--------------------------- Test Proccesses END ----------------------------*/
