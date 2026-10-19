/*----------------------------- Private Includes -----------------------------*/
#include "temp_split.h"
#include "split_sampler.h"
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

/*---------------------------- Private Defines END ----------------------------*/

/* Released by tearDown even if a Unity assertion aborts the split case. */
static struct split_sampler samples;

struct split_ancestry
{
   bptr_node_t indices[SPLIT_CACHE_CAP];
   uint32_t keys[SPLIT_CACHE_CAP];
   unsigned int count;
};

/*---------------------------- Private Functions -----------------------------*/
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
static struct bptr_node *split_descend(struct bptr *self, int64_t key,
                                       struct split_ancestry *ancestors)
{
   struct bptr_node *node = bptr_node_fetch(self, self->root_idx);

   while (node != NULL && node->level != 0)
    {
      struct bptr_node *child;
      uint32_t idx;

      if (ancestors != NULL)
       {
         if (ancestors->count == SPLIT_CACHE_CAP)
          { bptr_node_unload(self, node); return NULL; }
         ancestors->indices[ancestors->count] = node->node_idx;
         ancestors->keys[ancestors->count++] = node->key_count;
       }
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
      int64_t st = tmpl->st, interval = tmpl->interval, k, v;
      unsigned int height;
      uint32_t node_size;
      uint64_t node_count;
      _Bool is_lite;
      int sample_rc, unload_rc;
      size_t gap_cnt;

      /* An inserted key has to fit between two keys of the image. */
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

      lay.lay_cnt = tmpl->lay_cnt;
      lay.leaf_up = (unsigned int)bptr->node_bound.leaf.up;
      lay.brch_up = (unsigned int)bptr->node_bound.brch.up;
      lay.rec_cnt = bptr->record_cnt;
      height = bptr->height;
      node_size = bptr->node_size;
      node_count = bptr->node_cnt;
      is_lite = bptr->is_lite;
      snprintf(msg, sizeof msg, "failed to unload the template %s", tmpl->path);
      unload_rc = bptr_unload(bptr);
      TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, unload_rc, msg);
      TEST_ASSERT_EQUAL_UINT_MESSAGE(lay.lay_cnt, height,
                                     "template height does not match its name");

      sample_rc = split_sampler_init_shape(&samples, &lay, &tmpl->shape,
                                           is_lite, node_size);
      snprintf(msg, sizeof msg, "%s: %s", tmpl->path,
               split_sampler_strerror(sample_rc));
      TEST_ASSERT_EQUAL_INT_MESSAGE(SPLIT_SAMPLE_OK, sample_rc, msg);
      if (samples.custom)
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(samples.original.nodes, node_count,
                                         "template node count does not match its shape");
      gap_cnt = split_sampler_count(&samples);
      if (samples.nonfull_leaves != 0)
         printf("  skip %" PRIu64 " non-full leaves in %s\n",
                samples.nonfull_leaves, tmpl->path);
      if (gap_cnt == 0)
       {
         printf("  skip %s: no full leaves to split\n", tmpl->path);
         split_sampler_destroy(&samples);
         continue;
       }
      /* Small custom samplers enumerate eligible gaps in order, lazily.
       * Taller lists are bounded; find their extrema before copying. */
      uint64_t low_gap = split_sampler_gap_at(&samples, 0);
      uint64_t high_gap = split_sampler_gap_at(&samples, gap_cnt - 1);
      if (lay.lay_cnt >= 3)
         for (size_t i = 0; i < gap_cnt; i++)
          {
            uint64_t gap = split_sampler_gap_at(&samples, i);
            if (gap < low_gap) low_gap = gap;
            if (gap > high_gap) high_gap = gap;
          }
      sample_rc = split_sampler_kv(st, interval, low_gap, &k, &v);
      if (sample_rc == SPLIT_SAMPLE_OK)
         sample_rc = split_sampler_kv(st, interval, high_gap, &k, &v);
      snprintf(msg, sizeof msg, "%s: %s", tmpl->path,
               split_sampler_strerror(sample_rc));
      TEST_ASSERT_EQUAL_INT_MESSAGE(SPLIT_SAMPLE_OK, sample_rc, msg);

      printf("  %zu positions: capacity %u keys per leaf, %u children and %u keys per "
             "branch, %" PRIu64 " records\n", gap_cnt, lay.leaf_up - 1,
             lay.brch_up, lay.brch_up - 1, lay.rec_cnt);
      /* the instances of this template as one name: the verdict line of each
       * phase below names the set it covered */
      snprintf(inst, sizeof inst, SPLIT_INST_DIR "%u-*.bptr", tmpl->lay_cnt);
      _bptr_path(pattern, sizeof pattern, inst);

      /*-------- split one fresh copy of the template per position ----------*/
      for (size_t gap_i = 0; gap_i < gap_cnt; gap_i++)
       {
         uint64_t gap = split_sampler_gap_at(&samples, gap_i);
         int64_t k, v;
         int status, unload_rc, rm_rc;
         bptr_node_t sibling;
         bptr_node_t original_root;
         struct split_ancestry ancestors = {0};
         struct temp_verify_expect expected;
         unsigned int stop_depth = UINT_MAX;

         split_sampler_kv(st, interval, gap, &k, &v);
         if (samples.custom)
          {
            sample_rc = split_sampler_expect(&samples, gap, &expected, &stop_depth);
            snprintf(msg, sizeof msg, "%s: %s", tmpl->path,
                     split_sampler_strerror(sample_rc));
            TEST_ASSERT_EQUAL_INT_MESSAGE(SPLIT_SAMPLE_OK, sample_rc, msg);
          }

         snprintf(inst, sizeof inst, SPLIT_INST_DIR "%u-%zu.bptr",
                  tmpl->lay_cnt, gap_i);
         _bptr_path(path, sizeof path, inst);
         progress_update("    split %s: key %" PRIi64 " (position %zu/%zu, "
                         "%" PRIu64 " keys below it)", path, k,
                         gap_i + 1, gap_cnt, gap);

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

         original_root = bptr->root_idx;
         node = split_descend(bptr, k, samples.custom ? &ancestors : NULL);
         snprintf(msg, sizeof msg, "failed to fetch the leaf of %s for key %"
                  PRIi64, path, k);
         if (node == NULL) progress_break();
         TEST_ASSERT_NOT_NULL_MESSAGE(node, msg);
         if (node->key_count != lay.leaf_up - 1) progress_break();
         TEST_ASSERT_EQUAL_UINT_MESSAGE(lay.leaf_up - 1, node->key_count,
                                        "selected leaf is not full");

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

         if (samples.custom)
          {
            if (bptr->height != expected.height || bptr->node_cnt != expected.nodes ||
                bptr->record_cnt != samples.original.records + 1) progress_break();
            TEST_ASSERT_EQUAL_UINT_MESSAGE(expected.height, bptr->height,
                                           "split propagated to the wrong height");
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(expected.nodes, bptr->node_cnt,
                                             "split created the wrong number of nodes");
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(samples.original.records + 1,
                                             bptr->record_cnt, "split record count");
            if (stop_depth != UINT_MAX)
             {
               if (bptr->root_idx != original_root) progress_break();
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(original_root, bptr->root_idx,
                                                "absorbed split replaced the root");
               TEST_ASSERT_LESS_THAN_UINT_MESSAGE(ancestors.count, stop_depth,
                                                  "missing stopping ancestor");
               for (unsigned int depth = 0; depth <= stop_depth; depth++)
                {
                  node = bptr_node_fetch(bptr, ancestors.indices[depth]);
                  if (node == NULL) progress_break();
                  TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch original ancestor");
                  uint32_t count = node->key_count;
                  bptr_node_unload(bptr, node);
                  uint32_t wanted = ancestors.keys[depth] + (depth == stop_depth);
                  if (count != wanted) progress_break();
                  TEST_ASSERT_EQUAL_UINT_MESSAGE(wanted, count,
                                                 "unexpected ancestor occupancy after split");
                }
             }
            else
             {
               if (bptr->root_idx == original_root) progress_break();
               TEST_ASSERT_NOT_EQUAL_MESSAGE(original_root, bptr->root_idx,
                                              "full cascade did not replace the root");
             }
          }

         /* Flush and close before another process reads the image. Keep only
          * this instance until it has been verified and successfully removed. */
         snprintf(msg, sizeof msg, "failed to unload the instance %s", path);
         unload_rc = bptr_unload(bptr);
         if (unload_rc != BPTR_E_SUCCESS) progress_break();
         TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, unload_rc, msg);

         progress_update("    verify %s: key %" PRIi64
                         " (position %zu/%zu)", path, k, gap_i + 1, gap_cnt);
         status = tools_verify(tmpl, 1, k, v,
                                samples.custom ? &expected : NULL, path);
         if (status != 0) progress_break();
         tools_strstatus(err, sizeof err, TOOLS_TEMP_VERIFY, status);
         snprintf(msg, sizeof msg, "failed to verify the instance %s: %s",
                  path, err);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, msg);

         progress_update("    remove %s: key %" PRIi64
                         " (position %zu/%zu)", path, k, gap_i + 1, gap_cnt);
         rm_rc = remove(path);
         if (rm_rc != 0) progress_break();
         snprintf(msg, sizeof msg, "failed to remove the instance %s", path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, rm_rc, msg);
       }
      progress_done("    split %s: PASS", pattern);
      progress_done("    verify %s: PASS", pattern);
      split_sampler_destroy(&samples);
    }
}
/*------------------------------ Test Units END ------------------------------*/

/* The runner invokes this after a successful or aborted case. */
void temp_split_cleanup(void)
{ split_sampler_destroy(&samples); }
