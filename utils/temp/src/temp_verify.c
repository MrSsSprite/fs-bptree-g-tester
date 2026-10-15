/*----------------------------- Private Includes -----------------------------*/
#include "temp_full.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include "bptr_io.h"
#include "unity.h"
#include <string.h>
/*--------------------------- Private Includes END ---------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static struct bptr_node *_fetch_checked(struct bptr *self, bptr_node_t idx);
static int64_t _find_correct_key(struct bptr *self, struct bptr_node *node,
                                 uint32_t idx);
static void _verify_parent_links(struct bptr *self, struct bptr_node *node);
static void _verify_lattice_record(int64_t *st, int64_t interval,
                                   _Bool *has_record, int64_t key, int64_t val);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Assert that a loaded image is a perfectly full tree of @p lay_cnt
 *          levels over the `st'/`interval' key lattice
 *
 * Walks a loaded image and checks its shape: leftmost leaf, one global chain
 * per level, leaf fullness and `st'/`interval'/`val == key * 2`, internal-node
 * fullness, flags/levels, parent pointers, and
 * `key[i] == leftmost key of child i + 1'. The lattice may start at any @p st,
 * and @p interval must not be 0. Only keys actually visited are computed;
 * advancing the lattice and doubling a key are checked before evaluation.
 *
 * `has_new_kv' / @p key / @p val describe an optional inserted record: with
 * `has_new_kv' the verifier expects `height == lay_cnt + 1' and requires that
 * record exactly once. The physical leaf-record total and the header count
 * must independently match the full-tree geometry plus the inserted record.
 * Every visited node must also satisfy its legal occupancy bounds, with the
 * lower root minimum, before the library decodes its key/value arrays.
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
   _Bool has_met_new_kv = 0, has_lattice_record = 0;
   uint64_t leaf_cnt = 0, leaf_record_cnt = 0;
   uint64_t expect_record_cnt, expect_leaf_cnt, expect_node_cnt = 1;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "bptr == NULL");
   if (lay_cnt == 0) return;
   /* A zero step is not a lattice. */
   TEST_ASSERT_NOT_EQUAL_INT64_MESSAGE(0, interval, "interval == 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE((uint64_t)lay_cnt + (has_new_kv ? 1u : 0u),
                                    bptr->height, "height incorrect");
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, bptr->root_idx, "root_idx == 0");

   /* Derive the expected total from the original full-tree shape, without
    * trusting the file's record count, the visited leaf count or the lattice
    * walk. Keep this second safeguard independent of inserted-key detection. */
   expect_record_cnt = bptr->node_bound.leaf.up - 1;
   for (unsigned int level = 1; level < lay_cnt; level++)
    {
      TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(
         UINT64_MAX / bptr->node_bound.brch.up, expect_record_cnt,
         "expected record count overflows");
      expect_record_cnt *= bptr->node_bound.brch.up;
      /* Horner's recurrence sums the nodes without forming an unused power
       * one level beyond the tree: next = current * fanout + 1. */
      TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(
         (UINT64_MAX - 1u) / bptr->node_bound.brch.up, expect_node_cnt,
         "expected node count overflows");
      expect_node_cnt = expect_node_cnt * bptr->node_bound.brch.up + 1u;
    }
   expect_leaf_cnt = expect_record_cnt / (bptr->node_bound.leaf.up - 1u);
   if (has_new_kv)
    {
      uint64_t added_nodes = (uint64_t)lay_cnt + 1u;

      TEST_ASSERT_LESS_THAN_UINT64_MESSAGE(UINT64_MAX, expect_record_cnt,
                                           "expected record count overflows");
      expect_record_cnt++;
      /* The original leaf total cannot exceed the original record total. */
      expect_leaf_cnt++;
      TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(UINT64_MAX - added_nodes,
                                               expect_node_cnt,
                                               "expected node count overflows");
      expect_node_cnt += added_nodes;
    }

   /*--------------------------- check leaf layer ----------------------------*/
   node = _fetch_checked(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->parent, "root parent != 0");
   // find leftmost leaf
   while (node->level != 0)
    {
      par_n = node;
      node = _fetch_checked(bptr, _node_brch_vals_get(bptr, par_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch node");
      bptr_node_unload(bptr, par_n);
    }
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev, "prev of leftmost leaf");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "leaf is_leaf false");
   if (bptr->height > 1)
    {
      TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, node->parent, "leaf parent == 0");
      par_n = _fetch_checked(bptr, node->parent);
      TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch parent of leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx,
                                       _node_brch_vals_get(bptr, par_n, 0),
                                       "par_n->vals[0] != node");
    }
   leaf_record_cnt = node->key_count;
   for (uint32_t i = 0; i < node->key_count; i++)
    {
      if (has_new_kv && ((int64_t*)node->keys)[i] == key)
       {
         TEST_ASSERT_FALSE_MESSAGE(has_met_new_kv, "duplicate inserted record");
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
         _verify_lattice_record(&st, interval, &has_lattice_record,
                                 ((int64_t*)node->keys)[i],
                                 ((int64_t*)node->vals)[i]);
       }
    }
   leaf_cnt = 1;
   for (uint32_t i = 1; node->next; i++)
    {
      next_n = _fetch_checked(bptr, node->next);
      /* the fetch of the next sibling is dereferenced below: a cache too small
       * for the walk has to fail the check, not crash inside it */
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to load next node");
      if (i == _node_val_cnt(par_n))
       {
         struct bptr_node *next_par_n;
         TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, par_n->next, "par next == 0");
         next_par_n = _fetch_checked(bptr, par_n->next);
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
      TEST_ASSERT_LESS_OR_EQUAL_UINT64_MESSAGE(UINT64_MAX - node->key_count,
                                               leaf_record_cnt,
                                               "leaf record count overflows");
      leaf_record_cnt += node->key_count;
      for (uint32_t i = 0; i < node->key_count; i++)
       {
         if (has_new_kv && ((int64_t*)node->keys)[i] == key)
          {
            TEST_ASSERT_FALSE_MESSAGE(has_met_new_kv, "duplicate inserted record");
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
            _verify_lattice_record(&st, interval, &has_lattice_record,
                                    ((int64_t*)node->keys)[i],
                                    ((int64_t*)node->vals)[i]);
          }
       }
      TEST_ASSERT_LESS_THAN_UINT64_MESSAGE(UINT64_MAX, leaf_cnt,
                                           "leaf count overflows");
      leaf_cnt++;
    }
   if (has_new_kv)
      TEST_ASSERT_TRUE_MESSAGE(has_met_new_kv, "inserted record missing");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(expect_record_cnt, leaf_record_cnt,
                                    "leaf record count incorrect");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(expect_record_cnt, bptr->record_cnt,
                                    "record count incorrect");

   /* Counts come from checked geometry, never from a signed key difference
    * or a product with interval. Multiplying even a valid lattice's record
    * count by its interval (or computing an unused next key) can exceed
    * int64_t although every stored key and value fits. */
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(expect_leaf_cnt, leaf_cnt,
                                    "leaf count incorrect");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(expect_node_cnt, bptr->node_cnt,
                                    "node count incorrect");

   /*----------------------- check internal node layers ----------------------*/
   // The leaf layer has been traversed to its far right; walk every internal
   // layer from the extreme node left by the previous one, checking the sibling
   // chain, flags/level, parent/child relationships, fill, and every separator.
   _Bool ltr = 0;  // leaf node traversed to far right
   struct bptr_node dummy_n = { .node_idx = 0 }, *prior_n, *following_n;
   for (uint32_t level = 1; level < bptr->height; level++, ltr ^= 1)
    {
      // step up to this layer: level 1 starts from the rightmost leaf, higher
      // layers from the extreme node left by the previous (reverse) pass
      if (node->parent != 0)
       {
         struct bptr_node *tmp_n = _fetch_checked(bptr, node->parent);
         TEST_ASSERT_NOT_NULL_MESSAGE(tmp_n, "failed to fetch parent node");
         bptr_node_unload(bptr, node);
         node = tmp_n;
       }

      prior_n = &dummy_n;
      while (node != &dummy_n)
       {
         bptr_node_t following_i = ltr ? node->next : node->prev;
         following_n = following_i ? _fetch_checked(bptr, following_i)
                                   : &dummy_n;
         TEST_ASSERT_NOT_NULL_MESSAGE(following_n, "failed to fetch node");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(ltr ? node->prev : node->next,
                                          prior_n->node_idx,
                                          "internal node chain broken");
         TEST_ASSERT_FALSE_MESSAGE(node->is_leaf, "internal node is_leaf true");
         TEST_ASSERT_EQUAL_UINT16_MESSAGE(level, node->level,
                                          "internal node level incorrect");
         _verify_parent_links(bptr, node);
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
static void _verify_lattice_record(int64_t *st, int64_t interval,
                                   _Bool *has_record, int64_t key, int64_t val)
{
   /* Advance before an original record, not after it. Inserted records do not
    * consume a lattice step, and no unused step after the last key is needed. */
   if (*has_record)
    {
      if (interval > 0)
         TEST_ASSERT_LESS_OR_EQUAL_INT64_MESSAGE(INT64_MAX - interval, *st,
                                                 "lattice key overflows");
      else
         TEST_ASSERT_GREATER_OR_EQUAL_INT64_MESSAGE(INT64_MIN - interval, *st,
                                                    "lattice key overflows");
      *st += interval;
    }
   TEST_ASSERT_EQUAL_INT64_MESSAGE(*st, key, "leaf key not match");
   TEST_ASSERT_GREATER_OR_EQUAL_INT64_MESSAGE(INT64_MIN / 2, *st,
                                              "lattice value overflows");
   TEST_ASSERT_LESS_OR_EQUAL_INT64_MESSAGE(INT64_MAX / 2, *st,
                                           "lattice value overflows");
   TEST_ASSERT_EQUAL_INT64_MESSAGE(*st * 2, val, "leaf val not match");
   *has_record = 1;
}


static void _verify_parent_links(struct bptr *self, struct bptr_node *node)
{
   if (node->node_idx != self->root_idx)
    {
      struct bptr_node *parent;
      uint32_t references = 0;

      TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, node->parent,
                                           "internal node parent == 0");
      parent = _fetch_checked(self, node->parent);
      TEST_ASSERT_NOT_NULL_MESSAGE(parent, "failed to fetch internal node parent");
      TEST_ASSERT_FALSE_MESSAGE(parent->is_leaf, "internal node parent is a leaf");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)node->level + 1, parent->level,
                                       "internal node parent level incorrect");
      /* Walking the sibling chain must not accept an orphan or a branch
       * referenced twice, even when its stored parent index looks plausible. */
      for (uint32_t i = 0; i <= parent->key_count; i++)
         if (_node_brch_vals_get(self, parent, i) == node->node_idx) references++;
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, references,
         "internal node parent must reference it exactly once");
      bptr_node_unload(self, parent);
    }

   /* Also check ownership in the other direction, including child 0, which
    * has no separator of its own. A child cannot silently belong to a
    * different parent that happens to contain a reference to it as well. */
   for (uint32_t i = 0; i <= node->key_count; i++)
    {
      struct bptr_node *child =
         _fetch_checked(self, _node_brch_vals_get(self, node, i));
      TEST_ASSERT_NOT_NULL_MESSAGE(child, "failed to fetch child for parent check");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, child->parent,
                                       "child parent != its owning internal node");
      bptr_node_unload(self, child);
    }
}


/**
 * Check persisted flags and count before bptr_node_fetch unmarshals arrays.
 * The loader trusts key_count, so checking an overfull node after fetching it can
 * already be too late. This verifier only reads freshly loaded file images.
 *
 * A leaf holds at most L = leaf.up - 1 records and needs ceil(L / 2) records.
 * A branch holds at most B = brch.up children and needs ceil(B / 2) children,
 * hence ceil(B / 2) - 1 keys. Derive minima from capacities independently of
 * the library's low bounds. The root of these nonempty images needs one key;
 * identify it by root_idx, never by a possibly incorrect parent pointer.
 */
static struct bptr_node *_fetch_checked(struct bptr *self, bptr_node_t idx)
{
   uint16_t flags, level;
   uint32_t count;
   uint_fast32_t minimum, maximum;

   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, idx, "cannot fetch node 0");
   TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, bptr_io_fread_node(self, idx),
                                 "failed to read node for occupancy check");
   memcpy(&flags, self->fbuf, sizeof flags);
   memcpy(&level, (char*)self->fbuf + 2, sizeof level);
   memcpy(&count, (char*)self->fbuf + 4, sizeof count);
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, flags,
      level == 0 ? "leaf node flags not valid" : "internal node flags not valid");
   if (level == 0)
      TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_LEAF, flags,
                                    "leaf node flags not marked as leaf");
   else
      TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, flags,
                                   "internal node flags marked as leaf");
   maximum = (level == 0 ? self->node_bound.leaf.up :
                          self->node_bound.brch.up) - 1;
   minimum = level == 0 ? self->node_bound.leaf.up / 2 :
                         (self->node_bound.brch.up - 1) / 2;
   if (idx == self->root_idx) minimum = 1;

   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(maximum, count,
      level == 0 ? "leaf node overfull" : "internal node overfull");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(minimum, count,
      level == 0 ? "leaf node underfilled" : "internal node underfilled");
   return bptr_node_fetch(self, idx);
}

static int64_t _find_correct_key(struct bptr *self, struct bptr_node *node,
                                 uint32_t idx)
{
   struct bptr_node *child_n, *iter_n;
   int64_t key;

   // The ith key of an internal node is the 0th key of the leftmost descendant
   // leaf of the (i+1)th child of the internal node.
   child_n = _fetch_checked(self, _node_brch_vals_get(self, node, idx + 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(child_n, "failed to fetch child node");

   while (!child_n->is_leaf)
    {
      iter_n = _fetch_checked(self, _node_brch_vals_get(self, child_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(iter_n, "failed to fetch descendant node");
      bptr_node_unload(self, child_n);
      child_n = iter_n;
    }

   key = ((int64_t*)child_n->keys)[0];
   bptr_node_unload(self, child_n);

   return key;
}
/*-------------------------- Private Functions END ---------------------------*/
