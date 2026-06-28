/*----------------------------- Private Includes -----------------------------*/
#include "simple.h"
#include <stdio.h>
#include <inttypes.h>
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"
#include "test_util.h"
#include "bptr_node.h"
#include "bptr_static.h"
#include "bptr_utils.h"
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
void test_simp_split_end(struct bptr_temp *temp);
void test_simp_split_beg(struct bptr_temp *temp);
void test_simp_split_mid(struct bptr_temp *temp);
void test_simp_split_itr(struct bptr_temp *temp);
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
       {
         test_simp_split_end(test_matrix[m_it] + tp_it);
         test_simp_split_beg(test_matrix[m_it] + tp_it);
         test_simp_split_mid(test_matrix[m_it] + tp_it);
         test_simp_split_itr(test_matrix[m_it] + tp_it);
       }
    }
}
/*------------------------------ Test Units END ------------------------------*/


/*----------------------------- Test Proccesses ------------------------------*/
// New element at the end (i.e., greater than all existing elements)
void test_simp_split_end(struct bptr_temp *temp)
{
   struct bptr *bptr = _bptr_create(temp);
   struct bptr_node *node;
   bptr_node_t child[2];

   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");
   node = bptr_node_new(bptr, 0);
   TEST_ASSERT_MESSAGE(node, "failed at bptr_node_new");
   node->prev = node->next = 0;
   bptr->root_idx = node->node_idx;

   // Fill node
   for (int64_t i = 0, mx = bptr->node_bound.leaf.up - 1; i < mx; i++)
      _bptr_kv_ins_i64(node, temp->tools, i, i + 1, i, bptr->is_lite);
   bptr->record_cnt = node->key_count;
   bptr->node_cnt++;

   // Split
   TEST_ASSERT_MESSAGE(
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(0xFFFFFFFF),
                      temp->tools->node.val_wrapper_i64(0xFFFFFFFF)),
      "Failed at Split");
   bptr_node_unload(bptr, node);

   /*--------------------- Check Correctness after Split ---------------------*/
   int64_t idx = 0, par_ki;
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(2, bptr->height, "bptr->height != 2 after split");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(bptr->node_bound.leaf.up, bptr->record_cnt,
                                    "record count not correct");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(3, bptr->node_cnt,
                                    "bptr->node_cnt != 3 after split");
   // Check Root
   node = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load root");
   TEST_ASSERT_FALSE_MESSAGE(node->is_leaf, "root after split is leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(1, node->level);
   TEST_ASSERT_EQUAL(0, node->parent);
   TEST_ASSERT_EQUAL(0, node->prev);
   TEST_ASSERT_EQUAL(0, node->next);
   TEST_ASSERT_BITS(0x3, 0x1, node->flags);
   TEST_ASSERT_EQUAL(1, node->key_count);

   child[0] = _node_brch_vals_get(bptr, node, 0);
   child[1] = _node_brch_vals_get(bptr, node, 1);
   par_ki = temp->tools->node.cast_i64(node->keys);
   bptr_node_unload(bptr, node);

   node = bptr_node_fetch(bptr, child[0]);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load child[0]");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child is not leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(0, node->level);
   TEST_ASSERT_EQUAL(bptr->root_idx, node->parent);
   TEST_ASSERT_EQUAL(0, node->prev);
   TEST_ASSERT_EQUAL(child[1], node->next);
   TEST_ASSERT_BITS(0x3, 0x3, node->flags);
   TEST_ASSERT_NOT_EQUAL(0, node->key_count);
   uint32_t child0_kc = node->key_count;
   for (uint32_t i = 0; i < node->key_count; i++)
    {
      int64_t key = temp->tools->node.cast_i64(node->keys + bptr->key_size * i),
              val =
                 temp->tools->node.cast_i64(node->vals + bptr->value_size * i);
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, key, "child[0] key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx + 1, val, "child[0] value not match");
      idx++;
    }
   bptr_node_unload(bptr, node);

   TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, par_ki, "child[0] key not match");

   node = bptr_node_fetch(bptr, child[1]);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load child[1]");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child is not leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(0, node->level);
   TEST_ASSERT_EQUAL(bptr->root_idx, node->parent);
   TEST_ASSERT_EQUAL(child[0], node->prev);
   TEST_ASSERT_EQUAL(0, node->next);
   TEST_ASSERT_BITS(0x3, 0x3, node->flags);
   TEST_ASSERT_NOT_EQUAL(0, node->key_count);
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      child0_kc, node->key_count, "Right child has more keys than left child");
   for (uint32_t i = 0; i < node->key_count - 1; i++)
    {
      int64_t key = temp->tools->node.cast_i64(node->keys + bptr->key_size * i),
              val =
                 temp->tools->node.cast_i64(node->vals + bptr->value_size * i);
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, key, "child[1] key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx + 1, val, "child[1] value not match");
      idx++;
    }
   int64_t key =
      temp->tools->node.cast_i64(node->keys +
                                 bptr->key_size * (node->key_count - 1));
   TEST_ASSERT_EQUAL_INT64_MESSAGE(0xFFFFFFFF, key, "child[1] key not match");
   bptr_node_unload(bptr, node);
   /*------------------- Check Correctness after Split END -------------------*/

   TEST_ASSERT_MESSAGE(bptr_unload(bptr) == 0,
                       "Failed to unload bptr");

   char path[256];
   _bptr_path(path, sizeof(path), temp->fnm);
   TEST_ASSERT_EQUAL(0, remove(path));
}


// New element at the beginning (i.e., greater than all existing elements)
void test_simp_split_beg(struct bptr_temp *temp)
{
   struct bptr *bptr = _bptr_create(temp);
   struct bptr_node *node;
   bptr_node_t child[2];

   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");
   node = bptr_node_new(bptr, 0);
   TEST_ASSERT_MESSAGE(node, "failed at bptr_node_new");
   node->prev = node->next = 0;
   bptr->root_idx = node->node_idx;

   // Fill node
   for (int64_t i = 0, mx = bptr->node_bound.leaf.up - 1; i < mx; i++)
      _bptr_kv_ins_i64(node, temp->tools, i + 1, i + 2, i, bptr->is_lite);
   bptr->record_cnt = node->key_count;
   bptr->node_cnt++;

   // Split
   TEST_ASSERT_MESSAGE(
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(0),
                      temp->tools->node.val_wrapper_i64(1)),
      "Failed at Split");
   bptr_node_unload(bptr, node);

   /*--------------------- Check Correctness after Split ---------------------*/
   int64_t idx = 0, par_ki;
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(2, bptr->height, "bptr->height != 2 after split");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(bptr->node_bound.leaf.up, bptr->record_cnt,
                                    "record count not correct");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(3, bptr->node_cnt,
                                    "bptr->node_cnt != 3 after split");
   // Check Root
   node = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load root");
   TEST_ASSERT_FALSE_MESSAGE(node->is_leaf, "root after split is leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(1, node->level);
   TEST_ASSERT_EQUAL(0, node->parent);
   TEST_ASSERT_EQUAL(0, node->prev);
   TEST_ASSERT_EQUAL(0, node->next);
   TEST_ASSERT_BITS(0x3, 0x1, node->flags);
   TEST_ASSERT_EQUAL(1, node->key_count);

   child[0] = _node_brch_vals_get(bptr, node, 0);
   child[1] = _node_brch_vals_get(bptr, node, 1);
   par_ki = temp->tools->node.cast_i64(node->keys);
   bptr_node_unload(bptr, node);

   node = bptr_node_fetch(bptr, child[0]);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load child[0]");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child is not leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(0, node->level);
   TEST_ASSERT_EQUAL(bptr->root_idx, node->parent);
   TEST_ASSERT_EQUAL(0, node->prev);
   TEST_ASSERT_EQUAL(child[1], node->next);
   TEST_ASSERT_BITS(0x3, 0x3, node->flags);
   TEST_ASSERT_NOT_EQUAL(0, node->key_count);
   uint32_t child0_kc = node->key_count;
   for (uint32_t i = 0; i < node->key_count; i++)
    {
      int64_t key = temp->tools->node.cast_i64(node->keys + bptr->key_size * i),
              val =
                 temp->tools->node.cast_i64(node->vals + bptr->value_size * i);
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, key, "child[0] key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx + 1, val, "child[0] value not match");
      idx++;
    }
   bptr_node_unload(bptr, node);

   TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, par_ki, "child[0] key not match");

   node = bptr_node_fetch(bptr, child[1]);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load child[1]");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child is not leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(0, node->level);
   TEST_ASSERT_EQUAL(bptr->root_idx, node->parent);
   TEST_ASSERT_EQUAL(child[0], node->prev);
   TEST_ASSERT_EQUAL(0, node->next);
   TEST_ASSERT_BITS(0x3, 0x3, node->flags);
   TEST_ASSERT_NOT_EQUAL(0, node->key_count);
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      child0_kc, node->key_count, "Right child has more keys than left child");
   for (uint32_t i = 0; i < node->key_count; i++)
    {
      int64_t key = temp->tools->node.cast_i64(node->keys + bptr->key_size * i),
              val =
                 temp->tools->node.cast_i64(node->vals + bptr->value_size * i);
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, key, "child[1] key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx + 1, val, "child[1] value not match");
      idx++;
    }
   bptr_node_unload(bptr, node);
   /*------------------- Check Correctness after Split END -------------------*/

   TEST_ASSERT_MESSAGE(bptr_unload(bptr) == 0,
                       "Failed to unload bptr");

   char path[256];
   _bptr_path(path, sizeof(path), temp->fnm);
   TEST_ASSERT_EQUAL(0, remove(path));
}


void test_simp_split_mid(struct bptr_temp *temp)
{
   struct bptr *bptr = _bptr_create(temp);
   struct bptr_node *node;
   bptr_node_t child[2];
   int64_t mid_i = CEIL_DIV(bptr->node_bound.leaf.up, 2);

   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");
   node = bptr_node_new(bptr, 0);
   TEST_ASSERT_MESSAGE(node, "failed at bptr_node_new");
   node->prev = node->next = 0;
   bptr->root_idx = node->node_idx;

   // Fill node
   temp->tools->node.val_ins_i64(node, 0, 0);
   for (int64_t i = 0, mx = bptr->node_bound.leaf.up - 1, j = 0;
        i < mx; i++, j++)
    {
       if (i == mid_i) j++;
      _bptr_kv_ins_i64(node, temp->tools, j, j + 1, i, bptr->is_lite);
    }
   bptr->record_cnt = node->key_count;
   bptr->node_cnt++;

   // Split
   TEST_ASSERT_MESSAGE(
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(mid_i),
                      temp->tools->node.val_wrapper_i64(mid_i + 1)),
      "Failed at Split");
   bptr_node_unload(bptr, node);

   /*--------------------- Check Correctness after Split ---------------------*/
   int64_t idx = 0, par_ki;
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(2, bptr->height, "bptr->height != 2 after split");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(bptr->node_bound.leaf.up, bptr->record_cnt,
                                    "record count not correct");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(3, bptr->node_cnt,
                                    "bptr->node_cnt != 3 after split");
   // Check Root
   node = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load root");
   TEST_ASSERT_FALSE_MESSAGE(node->is_leaf, "root after split is leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(1, node->level);
   TEST_ASSERT_EQUAL(0, node->parent);
   TEST_ASSERT_EQUAL(0, node->prev);
   TEST_ASSERT_EQUAL(0, node->next);
   TEST_ASSERT_BITS(0x3, 0x1, node->flags);
   TEST_ASSERT_EQUAL(1, node->key_count);

   child[0] = _node_brch_vals_get(bptr, node, 0);
   child[1] = _node_brch_vals_get(bptr, node, 1);
   par_ki = temp->tools->node.cast_i64(node->keys);
   TEST_ASSERT_EQUAL_INT64_MESSAGE(mid_i, par_ki, "mid_i not promoted");
   bptr_node_unload(bptr, node);

   node = bptr_node_fetch(bptr, child[0]);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load child[0]");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child is not leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(0, node->level);
   TEST_ASSERT_EQUAL(bptr->root_idx, node->parent);
   TEST_ASSERT_EQUAL(0, node->prev);
   TEST_ASSERT_EQUAL(child[1], node->next);
   TEST_ASSERT_BITS(0x3, 0x3, node->flags);
   TEST_ASSERT_NOT_EQUAL(0, node->key_count);
   uint32_t child0_kc = node->key_count;
   for (uint32_t i = 0; i < node->key_count; i++)
    {
      int64_t key = temp->tools->node.cast_i64(node->keys + bptr->key_size * i),
              val =
                 temp->tools->node.cast_i64(node->vals + bptr->value_size * i);
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, key, "child[0] key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx + 1, val, "child[0] value not match");
      idx++;
    }
   bptr_node_unload(bptr, node);

   TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, par_ki, "child[0] key not match");

   node = bptr_node_fetch(bptr, child[1]);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load child[1]");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child is not leaf");
   TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
   TEST_ASSERT_EQUAL(0, node->level);
   TEST_ASSERT_EQUAL(bptr->root_idx, node->parent);
   TEST_ASSERT_EQUAL(child[0], node->prev);
   TEST_ASSERT_EQUAL(0, node->next);
   TEST_ASSERT_BITS(0x3, 0x3, node->flags);
   TEST_ASSERT_NOT_EQUAL(0, node->key_count);
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      child0_kc, node->key_count, "Right child has more keys than left child");
   for (uint32_t i = 0; i < node->key_count; i++)
    {
      int64_t key = temp->tools->node.cast_i64(node->keys + bptr->key_size * i),
              val =
                 temp->tools->node.cast_i64(node->vals + bptr->value_size * i);
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx, key, "child[1] key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(idx + 1, val, "child[1] value not match");
      idx++;
    }
   bptr_node_unload(bptr, node);
   /*------------------- Check Correctness after Split END -------------------*/

   TEST_ASSERT_MESSAGE(bptr_unload(bptr) == 0,
                       "Failed to unload bptr");

   char path[256];
   _bptr_path(path, sizeof(path), temp->fnm);
   TEST_ASSERT_EQUAL(0, remove(path));
}


void test_simp_split_itr(struct bptr_temp *temp)
{
   // Compute node capacity from template (matches _bptr_bound_set logic)
   uint_fast32_t const rem_sz = temp->node_sz - BPTR_NODE_METADATA_BYTE;
   uint_fast32_t const node_cap =
      rem_sz / (temp->key_sz + temp->val_sz) + 1;
   uint_fast32_t const fill_cnt = node_cap - 1;
   uint_fast32_t const left_kc  = node_cap - node_cap / 2;

#define MERGED_KEY(pos, ins_idx) \
   ((int64_t)(pos) < (int64_t)(ins_idx) ? (int64_t)(pos) * 2 + 2 : \
    (int64_t)(pos) == (int64_t)(ins_idx) ? (int64_t)(ins_idx) * 2 + 1 : \
    (int64_t)(pos) * 2)

#define MERGED_VAL(pos, ins_idx) \
   ((int64_t)(pos) < (int64_t)(ins_idx) ? (int64_t)(pos) * 3 + 3 : \
    (int64_t)(pos) == (int64_t)(ins_idx) ? (int64_t)(ins_idx) * 3 + 1 : \
    (int64_t)(pos) * 3)

   for (uint_fast32_t ins_idx = 0; ins_idx < node_cap; ins_idx++)
    {
      struct bptr *bptr = _bptr_create(temp);
      struct bptr_node *node;
      bptr_node_t child[2];

      /*---------------------------- Pre-Split Setup ----------------------------*/
      node = bptr_node_new(bptr, 0);
      TEST_ASSERT_MESSAGE(node, "failed at bptr_node_new");
      node->prev = node->next = 0;
      bptr->root_idx = node->node_idx;

      // Fill node with keys i*2+2 and values i*3+3
      // (shifted by +2/+3 so hole values 2*ins_idx+1 are positive,
      //  enabling "before first" insertion for unsigned key types)
      for (uint_fast32_t i = 0; i < fill_cnt; i++)
         _bptr_kv_ins_i64(node, temp->tools,
                          (int64_t)i * 2 + 2, (int64_t)i * 3 + 3, i,
                          bptr->is_lite);
      bptr->record_cnt = node->key_count;
      bptr->node_cnt++;

      int64_t const new_key = (int64_t)ins_idx * 2 + 1;
      int64_t const new_val = (int64_t)ins_idx * 3 + 1;

      // Split
      TEST_ASSERT_MESSAGE(
         bptr_node_split(bptr, node,
                         temp->tools->node.key_wrapper_i64(new_key),
                         temp->tools->node.val_wrapper_i64(new_val)),
         "Failed at Split");
      bptr_node_unload(bptr, node);
      /*-------------------------- Pre-Split Setup END --------------------------*/

      /*--------------------- Check Correctness after Split ---------------------*/
      int64_t par_ki;
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(2, bptr->height,
                                       "bptr->height != 2 after split");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node_cap, bptr->record_cnt,
                                       "record count not correct");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(3, bptr->node_cnt,
                                       "bptr->node_cnt != 3 after split");

      // Check Root
      node = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load root");
      TEST_ASSERT_FALSE_MESSAGE(node->is_leaf, "root after split is leaf");
      TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
      TEST_ASSERT_EQUAL(1, node->level);
      TEST_ASSERT_EQUAL(0, node->parent);
      TEST_ASSERT_EQUAL(0, node->prev);
      TEST_ASSERT_EQUAL(0, node->next);
      TEST_ASSERT_BITS(0x3, 0x1, node->flags);
      TEST_ASSERT_EQUAL(1, node->key_count);

      child[0] = _node_brch_vals_get(bptr, node, 0);
      child[1] = _node_brch_vals_get(bptr, node, 1);
      par_ki = temp->tools->node.cast_i64(node->keys);
      TEST_ASSERT_EQUAL_INT64_MESSAGE(MERGED_KEY(left_kc, ins_idx),
                                      par_ki, "promoted key not correct");
      bptr_node_unload(bptr, node);

      // Check Left Child (child[0])
      int64_t idx = 0;
      node = bptr_node_fetch(bptr, child[0]);
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load child[0]");
      TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child is not leaf");
      TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
      TEST_ASSERT_EQUAL(0, node->level);
      TEST_ASSERT_EQUAL(bptr->root_idx, node->parent);
      TEST_ASSERT_EQUAL(0, node->prev);
      TEST_ASSERT_EQUAL(child[1], node->next);
      TEST_ASSERT_BITS(0x3, 0x3, node->flags);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(left_kc, node->key_count,
                                       "child[0] key_count not match");
      uint32_t child0_kc = node->key_count;
      for (uint32_t i = 0; i < node->key_count; i++)
       {
         int64_t key = temp->tools->node.cast_i64(
                          node->keys + bptr->key_size * i),
                 val = temp->tools->node.cast_i64(
                          node->vals + bptr->value_size * i);
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            MERGED_KEY(i, ins_idx), key, "child[0] key not match");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            MERGED_VAL(i, ins_idx), val, "child[0] value not match");
         idx++;
       }
      bptr_node_unload(bptr, node);

      TEST_ASSERT_EQUAL_INT64_MESSAGE((int64_t)left_kc, idx,
                                      "child[0] idx not match");

      // Check Right Child (child[1])
      node = bptr_node_fetch(bptr, child[1]);
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to load child[1]");
      TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "child is not leaf");
      TEST_ASSERT_NOT_EQUAL(0, node->node_idx);
      TEST_ASSERT_EQUAL(0, node->level);
      TEST_ASSERT_EQUAL(bptr->root_idx, node->parent);
      TEST_ASSERT_EQUAL(child[0], node->prev);
      TEST_ASSERT_EQUAL(0, node->next);
      TEST_ASSERT_BITS(0x3, 0x3, node->flags);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(node_cap - left_kc, node->key_count,
                                       "child[1] key_count not match");
      TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
         child0_kc, node->key_count,
         "Right child has more keys than left child");
      for (uint32_t i = 0; i < node->key_count; i++, idx++)
       {
         int64_t key = temp->tools->node.cast_i64(
                          node->keys + bptr->key_size * i),
                 val = temp->tools->node.cast_i64(
                          node->vals + bptr->value_size * i);
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            MERGED_KEY(idx, ins_idx), key, "child[1] key not match");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            MERGED_VAL(idx, ins_idx), val, "child[1] value not match");
       }
      bptr_node_unload(bptr, node);
      /*------------------- Check Correctness after Split END -------------------*/

      TEST_ASSERT_MESSAGE(bptr_unload(bptr) == 0,
                          "Failed to unload bptr");

      char path[256];
      _bptr_path(path, sizeof(path), temp->fnm);
      TEST_ASSERT_EQUAL(0, remove(path));
    }

#undef MERGED_KEY
#undef MERGED_VAL
}
/*--------------------------- Test Proccesses END ----------------------------*/
