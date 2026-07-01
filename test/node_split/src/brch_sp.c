/*----------------------------- Private Includes -----------------------------*/
#include "brch_sp.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "bptree.h"
#include "unity.h"
#include "test_bptr_temp.h"
#include "test_bptr_setup.h"
#include "bptr_node.h"
#include "bptr_static.h"
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
void _bptr_full_brch_create(struct bptr_temp *temp);
void _bptr_full_brch_verify(struct bptr_temp *temp);
void test_sing_brch_split(struct bptr_temp *temp);
int temp_instantiate
 (const struct bptr_temp *temp, const char *prefix,
  char *dst_path, size_t dst_path_sz);
void test_sing_brch_split_end(struct bptr_temp *temp, const char *fnm);
/*-------------------- Private Function Declarations END ---------------------*/


/*-------------------------------- Test Units --------------------------------*/
void test_brch_split(void)
{
   struct bptr_temp *test_matrix[] = { lite_temps_iu, norm_temps_iu };
   size_t test_sz_matrix[] = { lite_temps_iu_sz, norm_temps_iu_sz };
   puts("Test Unit: Internal Node Split (test_brch_split)");

   // Create and verify template .bptr files
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         _bptr_full_brch_create(test_matrix[m_it] + tp_it);
         _bptr_full_brch_verify(test_matrix[m_it] + tp_it);
       }
    }

   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         TEST_ASSERT_EQUAL_MESSAGE(0,
            temp_instantiate(test_matrix[m_it] + tp_it, "temp",
                             path, sizeof(path)),
            "temp_instantiate failure");
         test_sing_brch_split_end(test_matrix[m_it] + tp_it, path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
            "failed to remove instantiated template");
       }
    }

   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         _bptr_path_subdir(path, sizeof(path),
                           test_matrix[m_it][tp_it].fnm, "temp");
         TEST_ASSERT_EQUAL_MESSAGE(0, remove(path),
                                   "failed to remove template");
       }
    }
}
/*------------------------------ Test Units END ------------------------------*/


/*------------------------------ Test Processes ------------------------------*/
// Trigger a leaf split that causes its parent (an internal node) to be full
// and split. The spawned leaf will be the right most node.
void test_sing_brch_split_end(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                 temp->cache_cap, temp->cmp);
   struct bptr_node *par_n, *node, *next_n, *root_n;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");
   par_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      bptr->node_bound.brch.up - 1, par_n->key_count, "root not full");

   node = bptr_node_fetch(bptr,
                          _node_brch_vals_get(bptr, par_n, par_n->key_count));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch child");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      bptr->node_bound.leaf.up - 1, node->key_count,
      "rightmost child not full");
   // check node correctness before split
   int64_t i = (par_n->key_count) * (bptr->node_bound.leaf.up - 1),
           i_before_split = i;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) before split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) before split");
    }

   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(i * 2),
                      temp->tools->node.val_wrapper_i64(i * 3));
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx, "`bptr_node_split failure'");

   // check node correctness after split
   next_n = bptr_node_fetch(bptr, n_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to load new node");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      bptr->node_bound.leaf.up, node->key_count + next_n->key_count,
      "sum of key_count of node and next_n incorrect");
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      node->key_count, next_n->key_count,
      "right child has more key than left child");
   TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(
      1, node->key_count - next_n->key_count,
      "(node->key_count - next_n->key_count)");
   i = i_before_split;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) after split");
    }
   for (uint32_t leaf_i = 0; leaf_i < next_n->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2,
         temp->tools->node.cast_i64(next_n->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3,
         temp->tools->node.cast_i64(next_n->vals + bptr->value_size * leaf_i),
         "Invalid node (value) after split");
    }
   TEST_ASSERT_NOT_EQUAL_HEX64_MESSAGE(par_n->node_idx, bptr->root_idx,
                                       "par_n is still root after split");
   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(root_n->key_count, 1, "root_n->key_count");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx,
      _node_brch_vals_get(bptr, root_n, 0),
      "par_n != first child of root_n");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(_node_brch_vals_get(bptr, root_n, 1),
      par_n->next,
      "par_n->next != (root_n->child)[1]");
   bptr_node_unload(bptr, par_n);
   par_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch (root_n->child)[1]");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, par_n->key_count,
                                               "par[1] has too few vals");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx,
      _node_brch_vals_get(bptr, par_n, par_n->key_count - 1),
      "node != second to last item of par[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx,
      _node_brch_vals_get(bptr, par_n, par_n->key_count),
      "next_n != last item of par[1]");
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, next_n);
   bptr_node_unload(bptr, par_n);
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch (root_n->child)[0]");
   next_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch (root_n->child)[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev,
                                    "(root_n->child)[0].prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
      "(root_n->child)[0].next != (root_n->child)[1]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, next_n->next,
                                    "(root_n->child)[1].next != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
      "(root_n->child)[1].prev != (root_n->child)[0]");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(bptr->node_bound.brch.up - 1,
      node->key_count + next_n->key_count,
      "(root_n->child)[0].key_count+(root_n->child)[1].key_count not full");
   bptr_node_unload(bptr, next_n);
   // check left internal node
   par_n = node;
   // TODO: check all members of par_n correct (except checksum, not implemented yet)
   i = 0;
   // Leftmost leaf node
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leaf");
   TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, node->key_count,
                             "leaf node not full");
   /* TODO: check other members of node */
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev, "prev of first leaf not 0");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   for (uint32_t brch_i = 0; brch_i < par_n->key_count; brch_i++)
    {
      next_n =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_i + 1));
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
         "prev and par_n child[leaf_i] not match");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
         "next and par_n child[leaf_i] not match");
      bptr_node_unload(bptr, node);
      node = next_n;
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                       "node->parent != par idx");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
         bptr->node_bound.leaf.up - 1, node->key_count,
         "leaf node not full");
      // TODO: check other members of node
      TEST_ASSERT_EQUAL_MESSAGE(
         temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
         temp->tools->node.cast_i64(node->keys),
         "incorrect internal node key");
      for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2,
            temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3,
            temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
            "leaf val not correct");
       }
    }
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, node->next,
                                 "next of last child of left internal node");
   next_n = bptr_node_fetch(bptr, node->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
      "prev of first child of right brch != last child if left brch");

   bptr_node_unload(bptr, node);
   node = bptr_node_fetch(bptr, par_n->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch right internal node");
   bptr_node_unload(bptr, par_n);
   par_n = node;
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node,
                                "failed to fetch 0th child of right brch");
   TEST_ASSERT_EQUAL_PTR_MESSAGE(next_n, node,
      "next of last child of left brch != first child of right brch");
   bptr_node_unload(bptr, next_n);
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "node->parent != par idx");
   TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, node->key_count,
                             "leaf node not full");
   // TODO: check other members of node
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   for (uint32_t brch_i = 0; brch_i < par_n->key_count - 2; brch_i++)
    {
      next_n =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_i + 1));
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
         "prev and par_n child[leaf_i] not match");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
         "next and par_n child[leaf_i] not match");
      bptr_node_unload(bptr, node);
      node = next_n;
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                       "node->parent != par idx");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
         bptr->node_bound.leaf.up - 1, node->key_count,
         "leaf node not full");
      // TODO: check other members of node
      TEST_ASSERT_EQUAL_MESSAGE(
         temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
         temp->tools->node.cast_i64(node->keys),
         "incorrect internal node key");
      for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2,
            temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3,
            temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
            "leaf val not correct");
       }
    }

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}
/*---------------------------- Test Processes END ----------------------------*/


/*---------------------------- Private Utilities -----------------------------*/
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
      _bptr_kv_ins_i64(node, temp->tools, i * 2, i * 3, leaf_i, bptr->is_lite);
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
         _bptr_kv_ins_i64(node, temp->tools, i * 2, i * 3, leaf_i, bptr->is_lite);
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
         i * 2, temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "child key not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3, temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
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
            i * 2, temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "child key not match");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(
            i * 3,
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

   _bptr_path_subdir(path, sizeof(path), temp->fnm, "temp");
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
/*-------------------------- Private Utilities END ---------------------------*/
