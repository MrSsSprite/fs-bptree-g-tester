/*----------------------------- Private Includes -----------------------------*/
#include "temp_split.h"
#include "templates.h"
#include "tools.h"
#include "progress.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include "bptr_static.h"
#include "test_bptr_setup.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Private Defines -----------------------------*/
/* node cache capacity of the cases: walking down to a leaf and splitting it
 * holds a handful of nodes at a time, and the verification of the image
 * reloads whatever it evicted on the way */
#define SPLIT_CACHE_CAP 256u

/* the instance images of the cases, relative to `bptr_files/' */
#define SPLIT_INST_DIR "temp/split/"

/* Roles a branch level of the template is probed at: the two positions below
 * the split point (`<' at the edge and next to it), the split point itself
 * (`=='), and the two above it (`>' next to it and at the edge). */
#define SPLIT_ROLE_CNT (5u)
/* Roles of the leaf the insertion lands in: the last position below the leaf
 * split point, the first above it, and the one behind its last key. */
#define SPLIT_LEAF_CNT (3u)
/*---------------------------- Private Defines END ----------------------------*/


/*------------------------------ Private Structs -----------------------------*/
/**
 * @brief   Shape of the template a split case starts from
 *
 * Read off the loaded template rather than derived from the request: the node
 * capacity decides where a split lands, and a case must not restate the
 * library's own arithmetic.
 */
struct split_layout
{
   unsigned int lay_cnt;   /* levels of the template */
   unsigned int leaf_up;   /* `leaf.up': a full leaf holds `leaf_up - 1' keys */
   unsigned int brch_up;   /* `brch.up': a full node holds `brch_up - 1' keys */
   uint64_t     rec_cnt;   /* records in the template */
};
/*---------------------------- Private Structs END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
/**
 * @brief   Number of insertion positions a template is split at
 *
 * A position is the number of existing keys the inserted key sorts after, so
 * position 0 puts it before every key of the image and `rec_cnt' behind the
 * last one.
 *
 * A 1- and a 2-level template is tried at every position it has: each insertion
 * splits a full image, so it cascades through the whole height and every node
 * on the way is a candidate for an off-by-one in the position arithmetic.  The
 * 3-level template has ~40 000 positions -- and two full verification walks per
 * insertion -- so it is sampled instead, at the positions that put each level
 * of the cascade into each of its cases (see `split_gap_at').
 *
 * @param[in] lay  shape of the template
 *
 * @return  the number of positions `split_gap_at' answers for
 */
static size_t split_gap_cnt(const struct split_layout *lay)
{
   if (lay->lay_cnt <= 2) return lay->rec_cnt + 1;

   /* the two ends, then one position per (leaf, level-1, root) role triple.
    * The last leaf's last position is the end of the image, so that one is
    * tried twice; a repeated position costs one insertion and keeps the
    * mapping from index to position free of special cases. */
   return 2 + SPLIT_ROLE_CNT * SPLIT_ROLE_CNT * SPLIT_LEAF_CNT;
}


/**
 * @brief   The number of keys a branch role sorts after
 *
 * An internal node holds `brch_up - 1' keys and `brch_up' children.  A split of
 * a full one promotes its key `brch_up / 2' and leaves `brch_up / 2' keys in
 * the node, so the insertion is a `<' when the new key sorts below that point,
 * a `==' when it is the promoted key itself and a `>' when it sorts above it.
 * The role is also the index of the child the insertion descends into.
 *
 * @param[in] lay   shape of the template
 * @param[in] role  0 .. SPLIT_ROLE_CNT - 1
 *
 * @return  the role's number of keys below the inserted key
 */
static unsigned int split_role(const struct split_layout *lay,
                               unsigned int role)
{
   unsigned int half = lay->brch_up / 2;

   switch (role)
    {
   case 0  : return 0;                   /* in front of every key */
   case 1  : return half - 1;            /* the last key below the split point */
   case 2  : return half;                /* the split point */
   case 3  : return half + 1;            /* the first key above the split point */
   default : return lay->brch_up - 1;    /* behind every key */
    }
}


/**
 * @brief   The number of keys a leaf role sorts after
 *
 * A leaf split moves `leaf_up / 2' keys into the new sibling, so the insertion
 * is a `<' when the new key sorts below the key the original node keeps last,
 * and a `>' at and above it.
 *
 * @param[in] lay  shape of the template
 * @param[in] pos  0 .. SPLIT_LEAF_CNT - 1
 *
 * @return  the role's number of keys below the inserted key
 */
static unsigned int split_leaf_pos(const struct split_layout *lay,
                                   unsigned int pos)
{
   unsigned int last_below = lay->leaf_up - 1 - lay->leaf_up / 2;

   switch (pos)
    {
   case 0  : return last_below;          /* the last key below the split point */
   case 1  : return last_below + 1;      /* the first key above it */
   default : return lay->leaf_up - 1;    /* behind the last key of the leaf */
    }
}


/**
 * @brief   The @p idx-th insertion position of a template
 *
 * Every position of a 1- and a 2-level template is its own index.  A 3-level
 * template is sampled: the leaf index a position lands in decides the level-1
 * node the cascade crosses (`leaf / brch_up') and the position inside it
 * (`leaf % brch_up'), and both of those decide where the level-1 node and the
 * root are split; the two ends come first, then one position per triple of
 * (root role, level-1 role, leaf role).
 *
 * @param[in] lay  shape of the template
 * @param[in] idx  position index, below `split_gap_cnt(lay)'
 *
 * @return  the number of existing keys the inserted key sorts after
 */
static unsigned int split_gap_at(const struct split_layout *lay, size_t idx)
{
   unsigned int leaf_mx, leaf_pos, l1_role, root_role, leaf;

   if (lay->lay_cnt <= 2) return (unsigned int)idx;

   if (idx == 0) return 0;
   if (idx == 1) return (unsigned int)lay->rec_cnt;
   idx -= 2;

   leaf_pos  = (unsigned int)(idx % SPLIT_LEAF_CNT);
   idx      /= SPLIT_LEAF_CNT;
   l1_role   = (unsigned int)(idx % SPLIT_ROLE_CNT);
   root_role = (unsigned int)(idx / SPLIT_ROLE_CNT);

   leaf_mx = lay->leaf_up - 1;
   leaf = split_role(lay, root_role) * lay->brch_up + split_role(lay, l1_role);
   return leaf * leaf_mx + split_leaf_pos(lay, leaf_pos);
}


/**
 * @brief   Fetch the leftmost leaf of a loaded template
 *
 * Walks down the first child of every node from the root.  The first keys of
 * that leaf are the key lattice of the image: `st' and the `interval' every
 * insertion position is built from.
 *
 * @param[in] self  loaded tree
 *
 * @return  the leftmost leaf, still loaded; NULL when a node cannot be fetched
 */
static struct bptr_node *split_leftmost_leaf(struct bptr *self)
{
   struct bptr_node *node = bptr_node_fetch(self, self->root_idx);

   while (node != NULL && node->level != 0)
    {
      struct bptr_node *child =
         bptr_node_fetch(self, _node_brch_vals_get(self, node, 0));
      bptr_node_unload(self, node);
      node = child;
    }

   return node;
}


/**
 * @brief   Fetch the leaf an inserted key belongs to
 *
 * Follows the child whose range holds @p key: the first key greater than it
 * separates the children, which is how `bptr_find_node' reads an image.  No
 * inserted key of this unit equals a key of the template -- an insertion sits
 * half an interval behind one of them -- so no tie has to be broken.
 *
 * @param[in] self  loaded tree
 * @param[in] key   key to insert; the fixtures are built with 8-byte keys
 *
 * @return  the leaf, still loaded; NULL when a node cannot be fetched
 */
static struct bptr_node *split_descend(struct bptr *self, int64_t key)
{
   struct bptr_node *node = bptr_node_fetch(self, self->root_idx);

   while (node != NULL && node->level != 0)
    {
      struct bptr_node *child;
      uint32_t idx;

      for (idx = 0; idx < node->key_count; idx++)
         if (key < ((int64_t*)node->keys)[idx]) break;
      child = bptr_node_fetch(self, _node_brch_vals_get(self, node, idx));
      bptr_node_unload(self, node);
      node = child;
    }

   return node;
}
/*-------------------------- Private Functions END ---------------------------*/


/*-------------------------------- Test Units --------------------------------*/
void test_full_split(void)
{
   char inst[PATH_MAX], path[PATH_MAX], pattern[PATH_MAX], msg[PATH_MAX + 160];
   char err[PATH_MAX + 128];
   const struct template *tmpls;
   size_t tmpl_cnt;

   tmpls = templates_get(&tmpl_cnt);

   for (size_t tmpl_i = 0; tmpl_i < tmpl_cnt; tmpl_i++)
    {
      const struct template *tmpl = &tmpls[tmpl_i];
      struct split_layout lay;
      struct bptr *bptr;
      struct bptr_node *node;
      int64_t st, interval, half;
      size_t gap_cnt;

      /* the sampler below is written for a leaf level and two branch levels,
       * and an inserted key has to fit between two keys of the image: a
       * template the unit cannot split is skipped with a notice, never dropped
       * silently */
      if (tmpl->lay_cnt > 3)
       {
         printf("  skip %s: %u levels is more than the sampler covers\n",
                tmpl->path, tmpl->lay_cnt);
         continue;
       }
      if (tmpl->interval / 2 == 0)
       {
         printf("  skip %s: interval %" PRIi64 " leaves no room between two "
                "keys\n", tmpl->path, tmpl->interval);
         continue;
       }

      /*----------------- the template the cases are built on ----------------*/
      printf("  template %s\n", tmpl->path);
      snprintf(msg, sizeof msg, "failed to load the template %s", tmpl->path);
      bptr = bptr_load(tmpl->path, SPLIT_CACHE_CAP, &cmp_i64);
      TEST_ASSERT_NOT_NULL_MESSAGE(bptr, msg);

      node = split_leftmost_leaf(bptr);
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch the leftmost leaf");
      st = ((int64_t*)node->keys)[0];
      interval = ((int64_t*)node->keys)[1] - st;
      half = interval / 2;
      lay.lay_cnt = tmpl->lay_cnt;
      lay.leaf_up = (unsigned int)bptr->node_bound.leaf.up;
      lay.brch_up = (unsigned int)bptr->node_bound.brch.up;
      lay.rec_cnt = bptr->record_cnt;
      bptr_node_unload(bptr, node);

      /* an inserted key has to fit between two keys of the template, or the
       * split would be handed a key the image already holds */
      snprintf(msg, sizeof msg, "%s: interval %" PRIi64 " leaves no room "
               "between two keys", tmpl->path, interval);
      TEST_ASSERT_NOT_EQUAL_INT64_MESSAGE(0, half, msg);
      snprintf(msg, sizeof msg, "failed to unload the template %s",
               tmpl->path);
      TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, bptr_unload(bptr), msg);

      gap_cnt = split_gap_cnt(&lay);
      printf("  %zu positions: %u keys per leaf, %u children and %u keys per "
             "node, %" PRIu64 " records\n", gap_cnt, lay.leaf_up - 1,
             lay.brch_up, lay.brch_up - 1, lay.rec_cnt);
      /* the instances of this template as one name: the verdict line of each
       * phase below names the set it covered */
      snprintf(inst, sizeof inst, SPLIT_INST_DIR "%u-*.bptr", tmpl->lay_cnt);
      _bptr_path(pattern, sizeof pattern, inst);

      /*-------- split one fresh copy of the template per position ----------*/
      for (size_t gap_i = 0; gap_i < gap_cnt; gap_i++)
       {
         unsigned int gap = split_gap_at(&lay, gap_i);
         int64_t k = st - half + (int64_t)gap * interval, v = k * 2;
         int status, unload_rc;
         bptr_node_t sibling;

         snprintf(inst, sizeof inst, SPLIT_INST_DIR "%u-%zu.bptr",
                  tmpl->lay_cnt, gap_i);
         _bptr_path(path, sizeof path, inst);
         progress_update("    split %s: key %" PRIi64 " (position %zu/%zu, "
                         "%u keys below it)", path, k, gap_i + 1, gap_cnt, gap);

         /* a run that a failing case aborted leaves instances behind, and the
          * copy refuses to overwrite a file that already exists */
         remove(path);
         status = tools_instantiate(tmpl->path, path);
         if (status != 0) progress_break();
         tools_strstatus(err, sizeof err, TOOLS_TEMP_INST, status);
         snprintf(msg, sizeof msg, "failed to instantiate %s from %s: %s",
                  path, tmpl->path, err);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, msg);

         snprintf(msg, sizeof msg, "failed to load the instance %s", path);
         bptr = bptr_load(path, SPLIT_CACHE_CAP, &cmp_i64);
         if (bptr == NULL) progress_break();
         TEST_ASSERT_NOT_NULL_MESSAGE(bptr, msg);

         node = split_descend(bptr, k);
         snprintf(msg, sizeof msg, "failed to fetch the leaf of %s for key %"
                  PRIi64, path, k);
         if (node == NULL) progress_break();
         TEST_ASSERT_NOT_NULL_MESSAGE(node, msg);

         sibling = bptr_node_split(bptr, node, &k, &v);
         snprintf(msg, sizeof msg, "split of key %" PRIi64 " at %s failed: "
                  "bptr_errno %d", k, path, bptr_errno);
         if (sibling == 0) progress_break();
         TEST_ASSERT_GREATER_THAN_UINT64_MESSAGE(0, sibling, msg);

         /* the split returns the sibling it created, which is the node the
          * split leaf was linked to -- whether or not the split cascaded */
         snprintf(msg, sizeof msg, "split of key %" PRIi64 " at %s reported "
                  "the sibling %llu while the leaf links to %llu", k, path,
                  (unsigned long long)sibling, (unsigned long long)node->next);
         if (node->next != sibling) progress_break();
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->next, sibling, msg);
         bptr_node_unload(bptr, node);

         /* flush and close the instance; what the split left behind is checked
          * on the file in the second loop, once no instance of this template is
          * open any more */
         snprintf(msg, sizeof msg, "failed to unload the instance %s", path);
         unload_rc = bptr_unload(bptr);
         if (unload_rc != BPTR_E_SUCCESS) progress_break();
         TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, unload_rc, msg);
       }
      progress_done("    split %s: PASS", pattern);

      /*------- and let the tool check every one of them from the file -------*/
      for (size_t gap_i = 0; gap_i < gap_cnt; gap_i++)
       {
         unsigned int gap = split_gap_at(&lay, gap_i);
         int64_t k = st - half + (int64_t)gap * interval, v = k * 2;
         int status, rm_rc;

         snprintf(inst, sizeof inst, SPLIT_INST_DIR "%u-%zu.bptr",
                  tmpl->lay_cnt, gap_i);
         _bptr_path(path, sizeof path, inst);
         progress_update("    verify %s: key %" PRIi64, path, k);

         /* the split has to be in the file, not just in the cache it was
          * written through.  Every instance of this template is flushed and
          * closed by now -- the first loop is over -- so the tool reads the
          * image from a clean file, and the whole tree, not just the nodes the
          * split touched, is checked against the shape of the template. */
         status = tools_verify(tmpl->lay_cnt, st, interval, 1, k, v, path);
         if (status != 0) progress_break();
         tools_strstatus(err, sizeof err, TOOLS_TEMP_VERIFY, status);
         snprintf(msg, sizeof msg, "failed to verify the instance %s: %s",
                  path, err);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, msg);

         rm_rc = remove(path);
         if (rm_rc != 0) progress_break();
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, rm_rc,
                                       "failed to remove the instance");
       }
      progress_done("    verify %s: PASS", pattern);
    }
}
/*------------------------------ Test Units END ------------------------------*/
