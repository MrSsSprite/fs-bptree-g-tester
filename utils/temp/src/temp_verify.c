/*----------------------------- Private Includes -----------------------------*/
#include "temp_full.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include "unity.h"
/*--------------------------- Private Includes END ---------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static int64_t _find_correct_key(struct bptr *self, struct bptr_node *node,
                                 uint32_t idx);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Assert that a loaded image is a perfectly full tree of @p lay_cnt
 *          levels over the `st'/`interval' key lattice
 *
 * Walks a loaded image and checks its shape: leftmost leaf, one global chain
 * per level, leaf fullness and `st'/`interval'/`val == key * 2`, internal-node
 * fullness, flags/levels, parent pointers, and
 * `key[i] == leftmost key of child i + 1'.  The lattice may start at any @p st
 * -- the record-count checks below measure the walk from it -- and @p interval
 * must not be 0, because they divide by it.
 *
 * `has_new_kv' / @p key / @p val describe an optional inserted record: with
 * `has_new_kv' the verifier expects `height == lay_cnt + 1' and tolerates that
 * one record.
 *
 * @note  It asserts through Unity and needs the abort frame `RUN_TEST' installs;
 *        a failing check longjmps out, which is what makes the caller's case
 *        fail.  Called outside a case, the assertion has no frame to jump to.
 */
void temp_full_verify(struct bptr *bptr,
                      unsigned int lay_cnt, int64_t st, int64_t interval,
                      _Bool has_new_kv, int64_t key, int64_t val)
{
   struct bptr_node *node, *par_n, *next_n;
   _Bool has_met_new_kv = 0;
   uint_fast64_t leaf_cnt = 0;
   /* The lattice starts here: `st' below walks it and is advanced by
    * `interval' as keys are laid down, so the count checks at the end have to
    * measure the walk from the start key rather than from 0. */
   const int64_t st0 = st;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "bptr == NULL");
   if (lay_cnt == 0) return;
   /* the count checks divide by it: a zero step is not a lattice */
   TEST_ASSERT_TRUE_MESSAGE(interval != 0, "interval == 0");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE((has_new_kv ? lay_cnt + 1 : lay_cnt),
                                    bptr->height, "height incorrect");
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, bptr->root_idx, "root_idx == 0");

   /*--------------------------- check leaf layer ----------------------------*/
   node = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch root");
   // find leftmost leaf
   while (node->level != 0)
    {
      par_n = node;
      node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch node");
      bptr_node_unload(bptr, par_n);
    }
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev, "prev of leftmost leaf");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "leaf is_leaf false");
   if (bptr->height > 1)
    {
      TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, node->parent, "leaf parent == 0");
      par_n = bptr_node_fetch(bptr, node->parent);
      TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch parent of leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx,
                                       _node_brch_vals_get(bptr, par_n, 0),
                                       "par_n->vals[0] != node");
    }
   for (uint32_t i = 0; i < node->key_count; i++)
    {
      if (has_new_kv && ((int64_t*)node->keys)[i] == key)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(val, ((int64_t*)node->vals)[i],
                                         "leaf val not match");
         /* the inserted key is passed: every key from here on belongs to the
          * original `st' sequence again and must sort after it */
         has_met_new_kv = 1;
       }
      else
       {
         if (has_new_kv)
          {
            if (has_met_new_kv)
               TEST_ASSERT_GREATER_THAN_INT64_MESSAGE(key,
                                                      ((int64_t*)node->keys)[i],
                                                      "incorrect key insert "
                                                      "location");
            else
               TEST_ASSERT_LESS_THAN_INT64_MESSAGE(key,
                                                   ((int64_t*)node->keys)[i],
                                                   "incorrect key insert "
                                                   "location");
          }
         TEST_ASSERT_EQUAL_INT64_MESSAGE(st, ((int64_t*)node->keys)[i],
                                         "leaf key not match");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(st * 2, ((int64_t*)node->vals)[i],
                                         "leaf val not match");
         st += interval;
       }
    }
   leaf_cnt++;
   for (uint32_t i = 1; node->next; i++)
    {
      next_n = bptr_node_fetch(bptr, node->next);
      /* the fetch of the next sibling is dereferenced below: a cache too small
       * for the walk has to fail the check, not crash inside it */
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to load next node");
      if (i == _node_val_cnt(par_n))
       {
         struct bptr_node *next_par_n;
         TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, par_n->next, "par next == 0");
         next_par_n = bptr_node_fetch(bptr, par_n->next);
         TEST_ASSERT_NOT_NULL_MESSAGE(next_par_n, "failed to load next parent");
         bptr_node_unload(bptr, par_n);
         par_n = next_par_n;
         i = 0;
       }
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->next, next_n->node_idx,
                                       "next_n idx != node->next");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx,
                                       _node_brch_vals_get(bptr, par_n, i),
                                       "par_n->vals[i] != next_n idx");
      /* a split hands the children it moves over to the new sibling; the leaf
       * has to know which node it hangs from, not only the other way round */
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, next_n->parent,
                                       "next_n parent != its parent");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
                                       "next_n prev != node");
      bptr_node_unload(bptr, node);
      node = next_n;

      TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "leaf is_leaf false");
      for (uint32_t i = 0; i < node->key_count; i++)
       {
         if (has_new_kv && ((int64_t*)node->keys)[i] == key)
          {
            TEST_ASSERT_EQUAL_INT64_MESSAGE(val, ((int64_t*)node->vals)[i],
                                            "leaf val not match");
            has_met_new_kv = 1;
          }
         else
          {
            if (has_new_kv)
             {
               if (has_met_new_kv)
                  TEST_ASSERT_GREATER_THAN_INT64_MESSAGE(key,
                                                         ((int64_t*)node->keys)[i],
                                                         "incorrect key insert "
                                                         "location");
               else
                  TEST_ASSERT_LESS_THAN_INT64_MESSAGE(key,
                                                      ((int64_t*)node->keys)[i],
                                                      "incorrect key insert "
                                                      "location");
             }
            TEST_ASSERT_EQUAL_INT64_MESSAGE(st, ((int64_t*)node->keys)[i],
                                            "leaf key not match");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(st * 2, ((int64_t*)node->vals)[i],
                                            "leaf val not match");
            st += interval;
          }
       }
      leaf_cnt++;
    }
   /* `st' has walked the keys of the original lattice only, from `st0': the
    * inserted key is skipped where it is met, and the leaf it was added to was
    * split in two, so the walk sees the keys of `leaf_cnt - 1' full leaves.
    * The inserted record is one more than that in the tree's own count. */
   if (has_new_kv)
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         st0 + ((int64_t)(leaf_cnt - 1) * (bptr->node_bound.leaf.up - 1))
                  * interval,
         st,
         "record count (derived from st) not correct");
   else
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         st0 + (int64_t)leaf_cnt * (bptr->node_bound.leaf.up - 1) * interval,
         st,
         "record count (derived from st) not correct");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      (uint_fast64_t)((st - st0) / interval) + (has_new_kv ? 1u : 0u),
      bptr->record_cnt,
      "record count does not match st");

   /* A template holds one node per level of its shape: `brch.up' children per
    * node means `brch.up^(lay_cnt - 1)' leaves and one node per node above
    * them.  The split of a full image adds one sibling per level and, over the
    * root it split, one new root. */
   {
      uint_fast64_t level_cnt = 1, expect_node_cnt = 0;

      for (uint32_t level = 0; level < lay_cnt; level++)
       {
         expect_node_cnt += level_cnt;
         level_cnt *= bptr->node_bound.brch.up;
       }
      if (has_new_kv) expect_node_cnt += lay_cnt + 1;
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(expect_node_cnt, bptr->node_cnt,
                                       "node count incorrect");
   }

   /*----------------------- check internal node layers ----------------------*/
   // The leaf layer has been traversed to its far right; walk every internal
   // layer from the extreme node left by the previous one, checking the sibling
   // chain, flags/level, fill, and every separator key.
   _Bool ltr = 0;  // leaf node traversed to far right
   struct bptr_node dummy_n = { .node_idx = 0 }, *prior_n, *following_n;
   for (uint32_t level = 1; level < bptr->height; level++, ltr ^= 1)
    {
      // step up to this layer: level 1 starts from the rightmost leaf, higher
      // layers from the extreme node left by the previous (reverse) pass
      if (node->parent != 0)
       {
         struct bptr_node *tmp_n = bptr_node_fetch(bptr, node->parent);
         TEST_ASSERT_NOT_NULL_MESSAGE(tmp_n, "failed to fetch parent node");
         bptr_node_unload(bptr, node);
         node = tmp_n;
       }

      prior_n = &dummy_n;
      while (node != &dummy_n)
       {
         bptr_node_t following_i = ltr ? node->next : node->prev;
         following_n = following_i ? bptr_node_fetch(bptr, following_i)
                                   : &dummy_n;
         TEST_ASSERT_NOT_NULL_MESSAGE(following_n, "failed to fetch node");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(ltr ? node->prev : node->next,
                                          prior_n->node_idx,
                                          "internal node chain broken");
         TEST_ASSERT_FALSE_MESSAGE(node->is_leaf, "internal node is_leaf true");
         TEST_ASSERT_TRUE_MESSAGE((node->flags & BPTR_NODE_FLAG_VALID) != 0,
                                  "internal node flags not valid");
         TEST_ASSERT_FALSE_MESSAGE((node->flags & BPTR_NODE_FLAG_LEAF) != 0,
                                   "internal node flags marked as leaf");
         TEST_ASSERT_EQUAL_UINT16_MESSAGE(level, node->level,
                                          "internal node level incorrect");
         if (!has_new_kv)
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(bptr->node_bound.brch.up - 1,
                                             node->key_count,
                                             "internal node not full");
         // TODO: checksum not implemented yet. SKIP that
         for (uint32_t i = 0; i < node->key_count; i++)
            TEST_ASSERT_EQUAL_INT64_MESSAGE(
               _find_correct_key(bptr, node, i), ((int64_t*)node->keys)[i],
               "internal node key not match");

         if (prior_n != &dummy_n) bptr_node_unload(bptr, prior_n);
         prior_n = node;
         node = following_n;
       }

      /* the walk leaves the level at the end it did not start from; that node
       * terminates the chain, and this pass has only read the links that
       * pointed the way it came from */
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, ltr ? prior_n->next : prior_n->prev,
                                       "internal node chain not terminated");

      node = prior_n;  // extreme node of this layer, still loaded
    }
   if (bptr->height > 1) bptr_node_unload(bptr, node);
   // TODO: verify the remaining stats of `bptr`
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static int64_t _find_correct_key(struct bptr *self, struct bptr_node *node,
                                 uint32_t idx)
{
   struct bptr_node *child_n, *iter_n;
   int64_t key;

   // The ith key of an internal node is the 0th key of the leftmost descendant
   // leaf of the (i+1)th child of the internal node.
   child_n = bptr_node_fetch(self, _node_brch_vals_get(self, node, idx + 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(child_n, "failed to fetch child node");

   while (!child_n->is_leaf)
    {
      iter_n = bptr_node_fetch(self, _node_brch_vals_get(self, child_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(iter_n, "failed to fetch descendant node");
      bptr_node_unload(self, child_n);
      child_n = iter_n;
    }

   key = ((int64_t*)child_n->keys)[0];
   bptr_node_unload(self, child_n);

   return key;
}
/*-------------------------- Private Functions END ---------------------------*/
