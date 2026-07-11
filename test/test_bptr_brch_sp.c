/*----------------------------- Private Includes -----------------------------*/
#include "test_bptr_brch_sp.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "bptree.h"
#include "unity.h"
#include "test_bptr_setup.h"
#include "bptr_node.h"
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Test Utilities ------------------------------*/
void _bptr_full_brch_create(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   struct bptr_node *node, *par_n;
   int64_t i = 0;

   _bptr_path_subdir(path, sizeof(path), temp->fnm, "temp");
   if (access(path, F_OK) == 0) return;
   bptr = _bptr_create_subdir(temp, "temp");
   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");
   // Fill up the internal node at level==1
   // Since Redistribution is not available yet, manually create all the nodes.
   node = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "bptr_node_new failure");
   node->prev = 0;
   for (uint32_t leaf_i = 0, leaf_mx = bptr->node_bound.leaf.up - 1;
        leaf_i < leaf_mx; leaf_i++, i++)
      _bptr_kv_ins_i64(node, temp->tools, i * 2 + 2, i * 3 + 3, leaf_i, bptr->is_lite);
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
         _bptr_kv_ins_i64(node, temp->tools, i * 2 + 2, i * 3 + 3, leaf_i, bptr->is_lite);
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

   _bptr_path_subdir(path, sizeof(path), temp->fnm, "temp");
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
         i * 2 + 2, temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "child key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3, temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
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
            i * 2 + 2, temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "child key not match");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            i * 3 + 3,
            temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
            "child val not match");
         i++;
       }
      bptr_node_unload(bptr, node);
    }

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "bptr_unload failure");
}


int temp_instantiate
 (const struct bptr_temp *temp, const char *prefix,
  char *dst_path, size_t dst_path_sz)
{
   char path[256], buf[4096];
   int sfd, dfd;
   int fn_ret;
   ssize_t n;
   struct stat st;

   _bptr_path_subdir(path, sizeof(path), temp->fnm, prefix);
   sfd = open(path, O_RDONLY);
   if (sfd < 0) return -1;

   if (fstat(sfd, &st)) { close(sfd); return -1; }

   fn_ret = snprintf(dst_path, dst_path_sz, "bptr_files/%s_%s", prefix, temp->fnm);
   if (fn_ret < 0 || fn_ret > dst_path_sz) { close(sfd); return -1; }
   dfd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
   if (dfd  < 0) { close(sfd); return -1; }

   while ((n = read(sfd, buf, sizeof(buf))) > 0)
    {
      char *str_it = buf;
      ssize_t written;
      while (n > 0 && (written = write(dfd, str_it, n)) > 0)
       { str_it += written; n -= written; }
      if (written < 0) { close(sfd); close(dfd); return -1; }
    }

   close(sfd);
   if (n < 0) { close(dfd); return -1; }
   close(dfd);
   return 0;
}


void _bptr_full_brch_casc_create(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   struct bptr_node *root_n, *l1_n, *node;
   int64_t i = 0;

   // Manually build path and create directory to work around
   // _bptr_path_subdir's null-termination bug for longer subdirs.
   snprintf(path, sizeof(path), "bptr_files/temp_casc/%s", temp->fnm);
   mkdir("bptr_files", 0755);
   mkdir("bptr_files/temp_casc", 0755);
   if (access(path, F_OK) == 0) return;
   bptr = bptr_init(path, temp->is_lite, temp->node_sz, temp->key_sz,
                    temp->val_sz, temp->cache_cap, temp->cmp);
   TEST_ASSERT_MESSAGE(bptr, "failed at bptr_init");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   // First leaf (parent=0) -> level=0, height=1
   node = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "bptr_node_new failure");
   node->prev = 0;

   // First L1 (parent=0) -> level=1, height=2
   l1_n = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(l1_n, "bptr_node_new failure");
   l1_n->prev = 0;

   // Root (parent=0) -> level=2, height=3
   root_n = bptr_node_new(bptr, 0);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "bptr_node_new failure");
   root_n->prev = root_n->next = 0;
   bptr->root_idx = root_n->node_idx;
   bptr->node_cnt++;

   // Fix parent pointers for the first leaf and L1
   node->parent = l1_n->node_idx;
   l1_n->parent = root_n->node_idx;

   // Link: root[0] = L1, L1[0] = leaf
   _bptr_val_ins_ptr(root_n, l1_n->node_idx, 0, bptr->is_lite);
   _bptr_val_ins_ptr(l1_n, node->node_idx, 0, bptr->is_lite);
   bptr->node_cnt += 2;

   // Fill first leaf
   for (uint32_t leaf_i = 0; leaf_i < leaf_full; leaf_i++, i++)
      _bptr_kv_ins_i64(node, temp->tools, i * 2 + 2, i * 3 + 3, leaf_i, bptr->is_lite);
   bptr->record_cnt += node->key_count;

   // Remaining leaves of first L1
   for (uint32_t brch_i = 0; brch_i < brch_full; brch_i++)
    {
      struct bptr_node *next_n = bptr_node_new(bptr, l1_n->node_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "bptr_node_new failure");
      node->next = next_n->node_idx;
      next_n->prev = node->node_idx;
      bptr_node_unload(bptr, node);
      node = next_n;

      for (uint32_t leaf_i = 0; leaf_i < leaf_full; leaf_i++, i++)
         _bptr_kv_ins_i64(node, temp->tools, i * 2 + 2, i * 3 + 3, leaf_i, bptr->is_lite);
      bptr->record_cnt += node->key_count;
      bptr->node_cnt++;

      _bptr_kv_ins_i64(l1_n, temp->tools,
                       temp->tools->node.cast_i64(node->keys),
                       node->node_idx, brch_i, bptr->is_lite);
    }
   // Don't set node->next=0 yet — it will link to the next L1's first
   // leaf when that L1 is created. Only the very last L1's last leaf
   // terminates the chain.
   // Track the last leaf to link leaf chains across L1s.
   bptr_node_t prev_l1_last_leaf = node->node_idx;
   bptr_node_unload(bptr, node);

   // Remaining L1 nodes and their leaves
   for (uint32_t root_i = 0; root_i < brch_full; root_i++)
    {
      struct bptr_node *prev_l1 = l1_n;
      l1_n = bptr_node_new(bptr, root_n->node_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(l1_n, "bptr_node_new failure");
      prev_l1->next = l1_n->node_idx;
      l1_n->prev = prev_l1->node_idx;
      bptr_node_unload(bptr, prev_l1);
      bptr->node_cnt++;

      // First leaf of this L1 — link to previous L1's last leaf
      node = bptr_node_new(bptr, l1_n->node_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "bptr_node_new failure");
      {
         struct bptr_node *prev_last =
            bptr_node_fetch(bptr, prev_l1_last_leaf);
         TEST_ASSERT_NOT_NULL_MESSAGE(prev_last,
            "failed to fetch prev L1 last leaf");
         prev_last->next = node->node_idx;
         node->prev = prev_last->node_idx;
         bptr_node_unload(bptr, prev_last);
      }
      for (uint32_t leaf_i = 0; leaf_i < leaf_full; leaf_i++, i++)
         _bptr_kv_ins_i64(node, temp->tools, i * 2 + 2, i * 3 + 3, leaf_i, bptr->is_lite);
      bptr->record_cnt += node->key_count;
      bptr->node_cnt++;
      _bptr_val_ins_ptr(l1_n, node->node_idx, 0, bptr->is_lite);

      // Remaining leaves of this L1
      for (uint32_t brch_i = 0; brch_i < brch_full; brch_i++)
       {
         struct bptr_node *next_n = bptr_node_new(bptr, l1_n->node_idx);
         TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "bptr_node_new failure");
         node->next = next_n->node_idx;
         next_n->prev = node->node_idx;
         bptr_node_unload(bptr, node);
         node = next_n;

         for (uint32_t leaf_i = 0; leaf_i < leaf_full; leaf_i++, i++)
            _bptr_kv_ins_i64(node, temp->tools, i * 2 + 2, i * 3 + 3, leaf_i, bptr->is_lite);
         bptr->record_cnt += node->key_count;
         bptr->node_cnt++;

         _bptr_kv_ins_i64(l1_n, temp->tools,
                          temp->tools->node.cast_i64(node->keys),
                          node->node_idx, brch_i, bptr->is_lite);
       }
      // Link last leaf to next L1's first leaf (handled at top of next
      // iteration), or terminate chain if this is the very last L1.
      if (root_i == brch_full - 1)
         node->next = 0;
      prev_l1_last_leaf = node->node_idx;
      bptr_node_unload(bptr, node);

      // Promote L1 into root: key = first key of L1's first leaf
      {
         struct bptr_node *fl = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1_n, 0));
         TEST_ASSERT_NOT_NULL_MESSAGE(fl, "failed to fetch first leaf");
         _bptr_kv_ins_i64(root_n, temp->tools,
                          temp->tools->node.cast_i64(fl->keys),
                          l1_n->node_idx, root_i, bptr->is_lite);
         bptr_node_unload(bptr, fl);
      }
    }
   l1_n->next = 0;
   bptr_node_unload(bptr, l1_n);
   bptr_node_unload(bptr, root_n);

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "bptr_unload failure");
}


void _bptr_full_brch_casc_verify(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   struct bptr_node *root_n, *l1_n, *node;
   int64_t i = 0;

   snprintf(path, sizeof(path), "bptr_files/temp_casc/%s", temp->fnm);
   bptr = bptr_load(path, 256, temp->cmp);
   TEST_ASSERT_MESSAGE(bptr, "failed at bptr_load");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "height != 3");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root index == 0");

   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_FALSE_MESSAGE(root_n->is_leaf, "root should not be leaf");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, root_n->key_count, "root not full");
   TEST_ASSERT_EQUAL_MESSAGE(2, root_n->level, "root level != 2");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, root_n->flags,
                                  "root flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, root_n->flags,
                                 "root flags has LEAF set");

   // Verify each L1 and its leaves
   for (uint32_t root_i = 0; root_i <= brch_full; root_i++)
    {
      l1_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, root_i));
      TEST_ASSERT_NOT_NULL_MESSAGE(l1_n, "failed to fetch L1 node");
      TEST_ASSERT_FALSE_MESSAGE(l1_n->is_leaf, "L1 should not be leaf");
      TEST_ASSERT_EQUAL_MESSAGE(brch_full, l1_n->key_count, "L1 not full");
      TEST_ASSERT_EQUAL_MESSAGE(1, l1_n->level, "L1 level != 1");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, l1_n->parent,
                                        "L1 parent != root");
      TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, l1_n->flags,
                                     "L1 flags missing VALID");
      TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, l1_n->flags,
                                    "L1 flags has LEAF set");

      if (root_i > 0)
       {
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            _node_brch_vals_get(bptr, root_n, root_i - 1), l1_n->prev,
            "L1 prev linkage incorrect");
         if (root_i < brch_full)
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(
               _node_brch_vals_get(bptr, root_n, root_i + 1), l1_n->next,
               "L1 next linkage incorrect");
       }
      else
       {
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, l1_n->prev,
                                           "first L1 prev != 0");
         if (brch_full > 0)
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(
               _node_brch_vals_get(bptr, root_n, 1), l1_n->next,
               "first L1 next linkage incorrect");
       }

      // Check last L1's next == 0
      if (root_i == brch_full)
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, l1_n->next,
            "last L1 next != 0");

      // Verify each leaf of this L1
      for (uint32_t brch_i = 0; brch_i <= brch_full; brch_i++)
       {
         node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1_n, brch_i));
         TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leaf");
         TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "leaf not leaf");
         TEST_ASSERT_EQUAL_UINT32_MESSAGE(leaf_full, node->key_count,
                                           "leaf not full");
         TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "leaf level != 0");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1_n->node_idx, node->parent,
                                           "leaf parent != L1");
         TEST_ASSERT_BITS_HIGH_MESSAGE(
            BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
            node->flags, "leaf flags incorrect");

         if (brch_i > 0)
          {
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(
               _node_brch_vals_get(bptr, l1_n, brch_i - 1), node->prev,
               "leaf prev linkage incorrect");
            if (brch_i < brch_full)
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(
                  _node_brch_vals_get(bptr, l1_n, brch_i + 1), node->next,
                  "leaf next linkage incorrect");
          }
         else
          {
            // Only the very first leaf of the first L1 has prev == 0
            if (root_i == 0)
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev,
                                                 "first leaf prev != 0");
            if (brch_full > 0)
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(
                  _node_brch_vals_get(bptr, l1_n, 1), node->next,
                  "first leaf next linkage incorrect");
          }

         // Only the very last leaf of the last L1 has next == 0
         if (brch_i == brch_full && root_i == brch_full)
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->next,
               "last leaf next != 0");

         // Verify promoted key matches first key of leaf
         if (brch_i > 0)
            TEST_ASSERT_EQUAL_MESSAGE(
               temp->tools->node.cast_i64(l1_n->keys + bptr->key_size * (brch_i - 1)),
               temp->tools->node.cast_i64(node->keys),
               "promoted key in L1 does not match first key of leaf");

         for (uint32_t leaf_i = 0; leaf_i < leaf_full; leaf_i++, i++)
          {
            TEST_ASSERT_EQUAL_INT64_MESSAGE(
               i * 2 + 2,
               temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
               "leaf key not match");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(
               i * 3 + 3,
               temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
               "leaf val not match");
          }
         bptr_node_unload(bptr, node);
       }
      bptr_node_unload(bptr, l1_n);
    }
   bptr_node_unload(bptr, root_n);

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "bptr_unload failure");
}
/*---------------------------- Test Utilities END ----------------------------*/
