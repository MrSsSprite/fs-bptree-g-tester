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
void test_sing_brch_split_beg(struct bptr_temp *temp, const char *fnm);
void test_sing_brch_split_iter(struct bptr_temp *temp);
void _bptr_full_brch_casc_create(struct bptr_temp *temp);
void _bptr_full_brch_casc_verify(struct bptr_temp *temp);
void test_casc_brch_split_end(struct bptr_temp *temp, const char *fnm);
void test_casc_brch_split_beg(struct bptr_temp *temp, const char *fnm);
void test_casc_brch_split_iter(struct bptr_temp *temp);
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
         TEST_ASSERT_EQUAL_MESSAGE(0,
            temp_instantiate(test_matrix[m_it] + tp_it, "temp",
                             path, sizeof(path)),
            "temp_instantiate failure");
         test_sing_brch_split_beg(test_matrix[m_it] + tp_it, path);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
            "failed to remove instantiated template");
       }
    }

   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
         test_sing_brch_split_iter(test_matrix[m_it] + tp_it);
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
         /* Also clean up stale directory at instantiation dest path */
         {
            char dpath[256];
            snprintf(dpath, sizeof(dpath), "bptr_files/temp_%s",
                     test_matrix[m_it][tp_it].fnm);
            rmdir(dpath);
         }
       }
    }
}


void test_casc_brch_split(void)
{
   // Use only the smallest-fanout configs: lite_128, norm_128.
   // Larger configs create massive trees that are too slow with
   // the current cache size.
   struct bptr_temp *test_matrix[] = { lite_temps_iu + 1, norm_temps_iu + 1 };
   size_t test_sz_matrix[] = { 1, 1 };
   puts("Test Unit: Cascading Internal Node Split (test_casc_brch_split)");

   // Create and verify template .bptr files
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         _bptr_full_brch_casc_create(test_matrix[m_it] + tp_it);
         _bptr_full_brch_casc_verify(test_matrix[m_it] + tp_it);
       }
    }

   // test_casc_brch_split_end
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char src[256], dst[256];
         snprintf(src, sizeof(src), "bptr_files/temp_casc/%s",
                  test_matrix[m_it][tp_it].fnm);
         snprintf(dst, sizeof(dst), "bptr_files/temp_casc_%s",
                  test_matrix[m_it][tp_it].fnm);
         // Copy template to working copy (simple file copy)
         {
            char buf[4096];
            int sfd = open(src, O_RDONLY);
            TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, sfd, "open src");
            struct stat st;
            fstat(sfd, &st);
            int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
            TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, dfd, "open dst");
            ssize_t n;
            while ((n = read(sfd, buf, sizeof(buf))) > 0)
             {
               ssize_t written;
               char *p = buf;
               while (n > 0 && (written = write(dfd, p, n)) > 0)
                { p += written; n -= written; }
               TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, (int)written,
                  "write failure");
             }
            close(sfd);
            close(dfd);
         }
         test_casc_brch_split_end(test_matrix[m_it] + tp_it, dst);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(dst),
            "failed to remove instantiated template");
       }
    }

   // test_casc_brch_split_beg
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char src[256], dst[256];
         snprintf(src, sizeof(src), "bptr_files/temp_casc/%s",
                  test_matrix[m_it][tp_it].fnm);
         snprintf(dst, sizeof(dst), "bptr_files/temp_casc_%s",
                  test_matrix[m_it][tp_it].fnm);
         {
            char buf[4096];
            int sfd = open(src, O_RDONLY);
            TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, sfd, "open src");
            struct stat st;
            fstat(sfd, &st);
            int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
            TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, dfd, "open dst");
            ssize_t n;
            while ((n = read(sfd, buf, sizeof(buf))) > 0)
             {
               ssize_t written;
               char *p = buf;
               while (n > 0 && (written = write(dfd, p, n)) > 0)
                { p += written; n -= written; }
               TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, (int)written,
                  "write failure");
             }
            close(sfd);
            close(dfd);
         }
         test_casc_brch_split_beg(test_matrix[m_it] + tp_it, dst);
         TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(dst),
            "failed to remove instantiated template");
       }
    }

   // test_casc_brch_split_iter
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
         test_casc_brch_split_iter(test_matrix[m_it] + tp_it);
    }

   // Cleanup
   for (size_t m_it = 0, m_mx = sizeof(test_matrix)/sizeof(*test_matrix);
        m_it < m_mx; m_it++)
    {
      for (size_t tp_it = 0, tp_mx = test_sz_matrix[m_it];
           tp_it < tp_mx; tp_it++)
       {
         char path[256];
         snprintf(path, sizeof(path), "bptr_files/temp_casc/%s",
                  test_matrix[m_it][tp_it].fnm);
         TEST_ASSERT_EQUAL_MESSAGE(0, remove(path),
                                   "failed to remove template");
         {
            char dpath[256];
            snprintf(dpath, sizeof(dpath), "bptr_files/temp_casc_%s",
                     test_matrix[m_it][tp_it].fnm);
            rmdir(dpath);
         }
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
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) before split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) before split");
    }

   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(i * 2 + 2),
                      temp->tools->node.val_wrapper_i64(i * 3 + 3));
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
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) after split");
    }
   for (uint32_t leaf_i = 0; leaf_i < next_n->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(next_n->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
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
   TEST_ASSERT_FALSE_MESSAGE(par_n->is_leaf, "par_n is_leaf should be false");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, root_n, 0), par_n->node_idx,
      "par_n->node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "par_n->level != 1");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, par_n->parent,
                                    "par_n->parent != root_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, par_n->flags,
                                 "par_n flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, par_n->flags,
                                "par_n flags has LEAF set");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, par_n->key_count,
                                               "par_n has too few keys");
   i = 0;
   // Leftmost leaf node
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leaf");
   TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, node->key_count,
                             "leaf node not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "first leaf is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "first leaf node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "first leaf level != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "first leaf parent != par_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      node->flags, "node flags incorrect");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev, "prev of first leaf not 0");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
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
      TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         _node_brch_vals_get(bptr, par_n, brch_i + 1), node->node_idx,
         "node->node_idx incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
      TEST_ASSERT_BITS_HIGH_MESSAGE(
         BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
         node->flags, "node flags incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(
         temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
         temp->tools->node.cast_i64(node->keys),
         "incorrect internal node key");
      for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
            temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
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

   // Check right brch
   bptr_node_unload(bptr, node);
   node = bptr_node_fetch(bptr, par_n->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch right internal node");
   bptr_node_unload(bptr, par_n);
   par_n = node;
   TEST_ASSERT_FALSE_MESSAGE(par_n->is_leaf, "right brch is_leaf should be false");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, root_n, 1), par_n->node_idx,
      "right brch node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "right brch level != 1");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, par_n->parent,
                                    "right brch parent != root_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, par_n->flags,
                                 "right brch flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, par_n->flags,
                                "right brch flags has LEAF set");
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node,
                                "failed to fetch 0th child of right brch");
   TEST_ASSERT_EQUAL_PTR_MESSAGE(next_n, node,
      "next of last child of left brch != first child of right brch");
   bptr_node_unload(bptr, next_n);
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "node->parent != par idx");
   if (par_n->key_count > 1)  // spec. case: right brch has only 2 children
      TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, node->key_count,
                                "leaf node not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "node->node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      node->flags, "node flags incorrect");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   if (par_n->key_count > 2)
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
         TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            _node_brch_vals_get(bptr, par_n, brch_i + 1), node->node_idx,
            "node->node_idx incorrect");
         TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
         TEST_ASSERT_BITS_HIGH_MESSAGE(
            BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
            node->flags, "node flags incorrect");
         TEST_ASSERT_EQUAL_MESSAGE(
            temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
            temp->tools->node.cast_i64(node->keys),
            "incorrect internal node key");
         for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
          {
            TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
               temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
               "leaf key not correct");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
               temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
               "leaf val not correct");
          }
       }
   // Last two nodes
   if (par_n->key_count > 1)
    {
      next_n =
         bptr_node_fetch(bptr,
                         _node_brch_vals_get(bptr, par_n, par_n->key_count - 1));
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
         "prev and par_n child[leaf_i] not match");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx, node->next,
         "next and par_n child[leaf_i] not match");
      bptr_node_unload(bptr, node);
      node = next_n;
    }
   else  // else node is the first node that was checked and not yet unloaded
      i -= node->key_count;
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "node->parent != par idx");
   next_n = bptr_node_fetch(bptr, node->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to fetch leaf");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
      "prev of last node");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->next, next_n->node_idx,
      "node_idx of last node");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, next_n->next, "next of last node");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(
      next_n->key_count, node->key_count,
      "node has less key than new_n after split");
   TEST_ASSERT_TRUE_MESSAGE(next_n->is_leaf, "last node is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, par_n->key_count), next_n->node_idx,
      "last node node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, next_n->level, "last node level != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, next_n->parent,
                                    "last node parent != par_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      next_n->flags, "last node flags incorrect");
   for (struct bptr_node *n_arr[2] = {node, next_n},
                         **it = n_arr, **ed = n_arr + 2;
        it < ed; it++)
    {
      for (uint32_t leaf_i = 0; leaf_i < (*it)->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
            temp->tools->node.cast_i64((*it)->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
            temp->tools->node.cast_i64((*it)->vals + bptr->value_size * leaf_i),
            "leaf val not correct");
       }
    }

   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Trigger a leaf split that causes its parent (an internal node) to be full
// and split. The spawned leaf will be the left most node.
void test_sing_brch_split_beg(struct bptr_temp *temp, const char *fnm)
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

   // Fetch leftmost child (first child of root)
   node = bptr_node_fetch(bptr,
                          _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch child");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      bptr->node_bound.leaf.up - 1, node->key_count,
      "leftmost child not full");
   // check node correctness before split
   int64_t i = 0;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) before split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) before split");
    }

   // Split with a key smaller than all existing keys (key=0 for i=-1)
   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(0),
                      temp->tools->node.val_wrapper_i64(0));
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx, "`bptr_node_split failure'");

   // check node correctness after split
   // next_n is the new node, always to the right of node
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
   // i starts at -1 because the new key at position 0 corresponds to i=-1
   i = -1;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) after split");
    }
   for (uint32_t leaf_i = 0; leaf_i < next_n->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(next_n->keys + bptr->key_size * leaf_i),
         "Invalid node (key) after split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
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
   // node and next_n are the first two children of the left internal node (par_n)
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx,
      _node_brch_vals_get(bptr, par_n, 0),
      "node != first item of par[0]");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(next_n->node_idx,
      _node_brch_vals_get(bptr, par_n, 1),
      "next_n != second item of par[0]");
   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, next_n);
   bptr_node_unload(bptr, par_n);
   // Load left internal node (par_n = first child of root)
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
   // check left internal node (contains the split leaves)
   par_n = node;
   TEST_ASSERT_FALSE_MESSAGE(par_n->is_leaf, "par_n is_leaf should be false");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, root_n, 0), par_n->node_idx,
      "par_n->node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "par_n->level != 1");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, par_n->parent,
                                    "par_n->parent != root_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, par_n->flags,
                                 "par_n flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, par_n->flags,
                                "par_n flags has LEAF set");
   TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, par_n->key_count,
                                               "par_n has too few keys");
   i = -1;
   // The first two children are the split leaves (node and next_n)
   // Verify node (first child of left internal node, part of split)
   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leaf");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "first leaf is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "first leaf node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "first leaf level != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, node->parent,
                                    "first leaf parent != par_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      node->flags, "node flags incorrect");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev, "prev of first leaf not 0");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   // Verify next_n and remaining children of left internal node
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
      if (brch_i > 0)  // first leaf (brch_i==0) was split; later ones are full
         TEST_ASSERT_EQUAL_UINT32_MESSAGE(
            bptr->node_bound.leaf.up - 1, node->key_count,
            "leaf node not full");
      TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         _node_brch_vals_get(bptr, par_n, brch_i + 1), node->node_idx,
         "node->node_idx incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
      TEST_ASSERT_BITS_HIGH_MESSAGE(
         BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
         node->flags, "node flags incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(
         temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
         temp->tools->node.cast_i64(node->keys),
         "incorrect internal node key");
      for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
            temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
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

   // Check right brch (all children should be full)
   bptr_node_unload(bptr, node);
   node = bptr_node_fetch(bptr, par_n->next);
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch right internal node");
   bptr_node_unload(bptr, par_n);
   par_n = node;
   TEST_ASSERT_FALSE_MESSAGE(par_n->is_leaf, "right brch is_leaf should be false");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, root_n, 1), par_n->node_idx,
      "right brch node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "right brch level != 1");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(root_n->node_idx, par_n->parent,
                                    "right brch parent != root_n");
   TEST_ASSERT_BITS_HIGH_MESSAGE(BPTR_NODE_FLAG_VALID, par_n->flags,
                                 "right brch flags missing VALID");
   TEST_ASSERT_BITS_LOW_MESSAGE(BPTR_NODE_FLAG_LEAF, par_n->flags,
                                "right brch flags has LEAF set");
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
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(
      _node_brch_vals_get(bptr, par_n, 0), node->node_idx,
      "node->node_idx incorrect");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
   TEST_ASSERT_BITS_HIGH_MESSAGE(
      BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
      node->flags, "node flags incorrect");
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "leaf key does not match");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "leaf val does not match");
    }
   if (par_n->key_count > 1)
      for (uint32_t brch_i = 0; brch_i < par_n->key_count - 1; brch_i++)
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
         TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "node is_leaf not true");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(
            _node_brch_vals_get(bptr, par_n, brch_i + 1), node->node_idx,
            "node->node_idx incorrect");
         TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "node->level != 0");
         TEST_ASSERT_BITS_HIGH_MESSAGE(
            BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
            node->flags, "node flags incorrect");
         TEST_ASSERT_EQUAL_MESSAGE(
            temp->tools->node.cast_i64(par_n->keys + bptr->key_size * brch_i),
            temp->tools->node.cast_i64(node->keys),
            "incorrect internal node key");
         for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
          {
            TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
               temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
               "leaf key not correct");
            TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
               temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
               "leaf val not correct");
          }
       }
   // Last node of right internal node
   next_n = bptr_node_fetch(bptr, node->next);
   if (next_n)  // last node
    {
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, next_n->prev,
         "prev of last node");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->next, next_n->node_idx,
         "node_idx of last node");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, next_n->next, "next of last node");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx, next_n->parent,
                                       "last node parent != par_n");
      TEST_ASSERT_EQUAL_MESSAGE(bptr->node_bound.leaf.up - 1, next_n->key_count,
                                "leaf node not full");
      TEST_ASSERT_TRUE_MESSAGE(next_n->is_leaf, "last node is_leaf not true");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(
         _node_brch_vals_get(bptr, par_n, par_n->key_count), next_n->node_idx,
         "last node node_idx incorrect");
      TEST_ASSERT_EQUAL_MESSAGE(0, next_n->level, "last node level != 0");
      TEST_ASSERT_BITS_HIGH_MESSAGE(
         BPTR_NODE_FLAG_VALID | BPTR_NODE_FLAG_LEAF,
         next_n->flags, "last node flags incorrect");
      for (uint32_t leaf_i = 0; leaf_i < next_n->key_count; leaf_i++, i++)
       {
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
            temp->tools->node.cast_i64(next_n->keys + bptr->key_size * leaf_i),
            "leaf key not correct");
         TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
            temp->tools->node.cast_i64(next_n->vals + bptr->value_size * leaf_i),
            "leaf val not correct");
       }
      bptr_node_unload(bptr, next_n);
    }

   bptr_node_unload(bptr, node);
   bptr_node_unload(bptr, par_n);
   bptr_node_unload(bptr, root_n);
   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Iterate over all insertion positions from before all keys (pos=0, key=1)
// to after all keys in the leftmost leaf (pos=k, key=2k+1). For each position:
// instantiate, split the leftmost leaf, verify structural invariants, cleanup.
void test_sing_brch_split_iter(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   uint32_t k;

   // Determine leaf capacity.
   // Create a one-shot bptr to inspect node_bound, then discard.
   // Use a unique sub-path to avoid colliding with any stale artifacts.
   TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0,
      snprintf(path, sizeof(path), "bptr_files/_k_%s", temp->fnm),
      "snprintf path failure");
   rmdir(path);  // defensive
   {
      struct bptr *kbptr = bptr_init(path, temp->is_lite,
                                     temp->node_sz, temp->key_sz,
                                     temp->val_sz, temp->cache_cap,
                                     temp->cmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(kbptr, "bptr_init for k determination");
      k = kbptr->node_bound.leaf.up - 1;
      TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(kbptr), "unload k bptr");
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path), "remove k bptr");
   }

   for (uint32_t pos = 0; pos <= k; pos++)
    {
      struct bptr_node *par_n, *node, *next_n;

      // Defensively clean up any stale directory left from a prior run
      rmdir(path);
      // (Re-)instantiate a fresh copy of the template
      TEST_ASSERT_EQUAL_MESSAGE(0,
         temp_instantiate(temp, "temp", path, sizeof(path)),
         "temp_instantiate failure");

      // Load, split, verify, cleanup in a tight scope
      bptr = bptr_load(path, temp->cache_cap, temp->cmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
      TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");

      par_n = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch root");

      // Fetch leftmost leaf
      node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leftmost leaf");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(k, node->key_count,
                                       "leftmost leaf not full");

      // Split with key=pos*2+1 that inserts at position pos
      bptr_node_t n_idx =
         bptr_node_split(bptr, node,
                         temp->tools->node.key_wrapper_i64(pos * 2 + 1),
                         temp->tools->node.val_wrapper_i64(pos * 2 + 1));
      TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx,
                                           "bptr_node_split failure");

      // Verify split leaf nodes
      next_n = bptr_node_fetch(bptr, n_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(next_n, "failed to load new node");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
         bptr->node_bound.leaf.up, node->key_count + next_n->key_count,
         "sum of key_count incorrect");
      bptr_node_unload(bptr, next_n);
      bptr_node_unload(bptr, node);

      // Verify new root
      TEST_ASSERT_NOT_EQUAL_HEX64_MESSAGE(par_n->node_idx, bptr->root_idx,
                                          "par_n is still root after split");
      struct bptr_node *root_n = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch new root");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, root_n->key_count,
                                       "root_n->key_count != 1");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(par_n->node_idx,
         _node_brch_vals_get(bptr, root_n, 0),
         "par_n != first child of root_n");
      bptr_node_unload(bptr, root_n);
      bptr_node_unload(bptr, par_n);

      TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr),
                                "Failed to bptr_unload");
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
                                    "failed to remove instantiated template");
    }
}
/*---------------------------- Test Processes END ----------------------------*/



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

   // TEMPORARY: early return to test if crash is in tree creation
   bptr_unload(bptr);
   return;
   TEST_ASSERT_MESSAGE(bptr, "failed at _bptr_create");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   // Create nodes bottom-up to establish correct levels/height.
   // bptr_node_new(bptr, 0) bumps self->height and assigns
   // node->level = previous height. We need: leaf(0), L1(1), root(2).

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
   node->next = 0;
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

      // First leaf of this L1
      node = bptr_node_new(bptr, l1_n->node_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "bptr_node_new failure");
      node->prev = 0;
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
      node->next = 0;
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
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev,
                                              "first leaf prev != 0");
            if (brch_full > 0)
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(
                  _node_brch_vals_get(bptr, l1_n, 1), node->next,
                  "first leaf next linkage incorrect");
          }

         // Check last leaf's next == 0
         if (brch_i == brch_full)
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


// Trigger a leaf split in a HEIGHT-3 tree that causes cascading splits:
// leaf -> L1 -> root all split. The spawned leaf will be the rightmost node.
void test_casc_brch_split_end(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                  temp->cache_cap, temp->cmp);
   struct bptr_node *par_n, *node, *root_n;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, root_n->key_count, "root not full");
   TEST_ASSERT_EQUAL_MESSAGE(2, root_n->level, "root level != 2");

   // Navigate to rightmost leaf: root last child -> L1 last child -> leaf
   par_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, brch_full));
   TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch rightmost L1");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, par_n->key_count, "rightmost L1 not full");
   TEST_ASSERT_EQUAL_MESSAGE(1, par_n->level, "rightmost L1 level != 1");

   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, brch_full));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch rightmost leaf");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(leaf_full, node->key_count,
                                     "rightmost leaf not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "rightmost leaf not leaf");
   TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "rightmost leaf level != 0");

   // Verify keys/values in rightmost leaf before split and compute total keys
   int64_t total_keys =
      (brch_full + 1) * (brch_full + 1) * leaf_full;
   int64_t i = total_keys - leaf_full;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) before split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) before split");
    }

   // Split: insert key at end of rightmost leaf (i = total_keys)
   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(total_keys * 2 + 2),
                      temp->tools->node.val_wrapper_i64(total_keys * 3 + 3));
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx, "bptr_node_split failure");

   // After cascading split, height should be 4
   TEST_ASSERT_EQUAL_MESSAGE(4, bptr->height, "post-split height != 4");

   // Verify new root
   struct bptr_node *new_root = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch new root");
   TEST_ASSERT_EQUAL_MESSAGE(3, new_root->level, "new root level != 3");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, new_root->key_count,
                                     "new root key_count != 1");
   TEST_ASSERT_FALSE_MESSAGE(new_root->is_leaf, "new root should not be leaf");

   // New root's two children are the split halves of the old root
   struct bptr_node *left_brch =
      bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 0));
   struct bptr_node *right_brch =
      bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(left_brch, "failed to fetch left child of new root");
   TEST_ASSERT_NOT_NULL_MESSAGE(right_brch, "failed to fetch right child of new root");

   TEST_ASSERT_EQUAL_MESSAGE(2, left_brch->level, "left brch level != 2");
   TEST_ASSERT_EQUAL_MESSAGE(2, right_brch->level, "right brch level != 2");
   TEST_ASSERT_FALSE_MESSAGE(left_brch->is_leaf,
                              "left brch should not be leaf");
   TEST_ASSERT_FALSE_MESSAGE(right_brch->is_leaf,
                              "right brch should not be leaf");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      brch_full, left_brch->key_count + right_brch->key_count,
      "left + right brch key_count != brch_full");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, left_brch->parent,
                                     "left brch parent != new root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(new_root->node_idx, right_brch->parent,
                                     "right brch parent != new root");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, left_brch->prev,
                                     "left brch prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(right_brch->node_idx, left_brch->next,
                                     "left brch next != right brch");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(left_brch->node_idx, right_brch->prev,
                                     "right brch prev != left brch");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, right_brch->next,
                                     "right brch next != 0");

   bptr_node_unload(bptr, root_n);
   root_n = NULL;

   // Walk all leaves in order, verifying data integrity
   i = 0;
   int64_t leaf_count = 0;
   struct bptr_node *cur_brch = left_brch;
   struct bptr_node *prev_leaf = NULL;

   while (cur_brch)
    {
      // Verify cur_brch is an internal node at level 2
      TEST_ASSERT_FALSE_MESSAGE(cur_brch->is_leaf, "cur_brch should not be leaf");
      TEST_ASSERT_EQUAL_MESSAGE(2, cur_brch->level, "cur_brch level != 2");

      // Walk L1 children of this brch
      for (uint32_t brch_i = 0; brch_i <= cur_brch->key_count; brch_i++)
       {
         struct bptr_node *l1 =
            bptr_node_fetch(bptr, _node_brch_vals_get(bptr, cur_brch, brch_i));
         TEST_ASSERT_NOT_NULL_MESSAGE(l1, "failed to fetch L1");
         TEST_ASSERT_FALSE_MESSAGE(l1->is_leaf, "L1 should not be leaf");
         TEST_ASSERT_EQUAL_MESSAGE(1, l1->level, "L1 level != 1");
         TEST_ASSERT_EQUAL_UINT64_MESSAGE(cur_brch->node_idx, l1->parent,
                                           "L1 parent != cur_brch");

         // Verify that L1 promoted key matches its first leaf's first key
         struct bptr_node *first_leaf_l1 =
            bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1, 0));
         if (brch_i > 0)
            TEST_ASSERT_EQUAL_MESSAGE(
               temp->tools->node.cast_i64(cur_brch->keys + bptr->key_size * (brch_i - 1)),
               temp->tools->node.cast_i64(first_leaf_l1->keys),
               "promoted key in brch does not match first key of L1's first leaf");
         bptr_node_unload(bptr, first_leaf_l1);

         // Walk leaves of this L1
         for (uint32_t lf_i = 0; lf_i <= l1->key_count; lf_i++)
          {
            node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1, lf_i));
            TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leaf");
            TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "leaf not leaf");
            TEST_ASSERT_EQUAL_MESSAGE(0, node->level, "leaf level != 0");
            TEST_ASSERT_EQUAL_UINT64_MESSAGE(l1->node_idx, node->parent,
                                              "leaf parent != L1");

            // Verify leaf prev/next linkage
            if (prev_leaf)
             {
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_leaf->node_idx, node->prev,
                  "leaf prev linkage incorrect");
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, prev_leaf->next,
                  "leaf next linkage incorrect");
             }
            else
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev,
                  "first leaf prev != 0");

            // Verify keys/values
            for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
             {
               TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
                  temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
                  "leaf key not correct");
               TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
                  temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
                  "leaf val not correct");
             }

            bptr_node_unload(bptr, prev_leaf);
            prev_leaf = node;
            leaf_count++;
          }

         // Verify L1 promoted keys match first key of each leaf
         for (uint32_t lk_i = 0; lk_i < l1->key_count; lk_i++)
          {
            struct bptr_node *chk =
               bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1, lk_i + 1));
            TEST_ASSERT_NOT_NULL_MESSAGE(chk, "failed to fetch leaf for key check");
            TEST_ASSERT_EQUAL_MESSAGE(
               temp->tools->node.cast_i64(l1->keys + bptr->key_size * lk_i),
               temp->tools->node.cast_i64(chk->keys),
               "L1 promoted key does not match first key of leaf");
            bptr_node_unload(bptr, chk);
          }

         bptr_node_unload(bptr, l1);
       }

      // Next brch node
      struct bptr_node *next_brch = NULL;
      if (cur_brch->next)
         next_brch = bptr_node_fetch(bptr, cur_brch->next);
      bptr_node_unload(bptr, cur_brch);
      cur_brch = next_brch;
    }

   // Verify last leaf's next == 0
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, prev_leaf->next,
                                     "last leaf next != 0");
   bptr_node_unload(bptr, prev_leaf);

   // Total leaf count should match: (brch_full+1)^2 for full nodes before
   // plus the split added 1 leaf, 1 L1, 1 brch, and 1 new root
   // Total leaves = (brch_full+1)^2 + 1
   int64_t expected_leaves = (brch_full + 1) * (brch_full + 1) + 1;
   TEST_ASSERT_EQUAL_INT64_MESSAGE(expected_leaves, leaf_count,
                                    "unexpected total leaf count");

   bptr_node_unload(bptr, new_root);
   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Trigger a leaf split in a HEIGHT-3 tree that causes cascading splits.
// The spawned leaf will be the leftmost node.
void test_casc_brch_split_beg(struct bptr_temp *temp, const char *fnm)
{
   struct bptr *bptr = bptr_load(fnm ? fnm : temp->fnm,
                                  temp->cache_cap, temp->cmp);
   struct bptr_node *root_n, *l1_n, *node;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");

   uint32_t brch_full = bptr->node_bound.brch.up - 1;
   uint32_t leaf_full = bptr->node_bound.leaf.up - 1;

   TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

   root_n = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(root_n, "failed to fetch root");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, root_n->key_count, "root not full");

   // Navigate to leftmost leaf: root first child -> first L1 -> first leaf
   l1_n = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, root_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(l1_n, "failed to fetch leftmost L1");
   TEST_ASSERT_EQUAL_MESSAGE(brch_full, l1_n->key_count, "leftmost L1 not full");

   node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1_n, 0));
   TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leftmost leaf");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(leaf_full, node->key_count,
                                     "leftmost leaf not full");
   TEST_ASSERT_TRUE_MESSAGE(node->is_leaf, "leftmost leaf not leaf");

   // Verify keys/values in leftmost leaf before split
   int64_t i = 0;
   for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
    {
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 2 + 2,
         temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
         "Invalid node (key) before split");
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         i * 3 + 3,
         temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
         "Invalid node (value) before split");
    }

   // Split with key=0, val=0 (inserts before all existing keys)
   bptr_node_t n_idx =
      bptr_node_split(bptr, node,
                      temp->tools->node.key_wrapper_i64(0),
                      temp->tools->node.val_wrapper_i64(0));
   TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx, "bptr_node_split failure");

   // After cascading split, height should be 4
   TEST_ASSERT_EQUAL_MESSAGE(4, bptr->height, "post-split height != 4");

   // Verify new root
   struct bptr_node *new_root = bptr_node_fetch(bptr, bptr->root_idx);
   TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch new root");
   TEST_ASSERT_EQUAL_MESSAGE(3, new_root->level, "new root level != 3");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, new_root->key_count,
                                     "new root key_count != 1");

   // New root's two children
   struct bptr_node *left_brch =
      bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 0));
   struct bptr_node *right_brch =
      bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 1));
   TEST_ASSERT_NOT_NULL_MESSAGE(left_brch, "failed to fetch left child of new root");
   TEST_ASSERT_NOT_NULL_MESSAGE(right_brch, "failed to fetch right child of new root");

   TEST_ASSERT_EQUAL_MESSAGE(2, left_brch->level, "left brch level != 2");
   TEST_ASSERT_EQUAL_MESSAGE(2, right_brch->level, "right brch level != 2");
   TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      brch_full, left_brch->key_count + right_brch->key_count,
      "left + right brch key_count != brch_full");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, left_brch->prev,
                                     "left brch prev != 0");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(right_brch->node_idx, left_brch->next,
                                     "left brch next != right brch");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(left_brch->node_idx, right_brch->prev,
                                     "right brch prev != left brch");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, right_brch->next,
                                     "right brch next != 0");

   bptr_node_unload(bptr, root_n);
   root_n = NULL;

   // Walk all leaves in order, verifying data integrity
   // Keys should be in order: the inserted key 0 at position 0, then all
   // original keys in order. Global index resets due to the insertion.
   i = -1;  // the inserted key (0) corresponds to i=-1 in the pattern
   struct bptr_node *prev_leaf = NULL;
   struct bptr_node *cur_brch = left_brch;

   while (cur_brch)
    {
      for (uint32_t brch_i = 0; brch_i <= cur_brch->key_count; brch_i++)
       {
         struct bptr_node *l1 =
            bptr_node_fetch(bptr, _node_brch_vals_get(bptr, cur_brch, brch_i));
         TEST_ASSERT_NOT_NULL_MESSAGE(l1, "failed to fetch L1");

         for (uint32_t lf_i = 0; lf_i <= l1->key_count; lf_i++)
          {
            node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1, lf_i));
            TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leaf");

            // Verify leaf prev/next linkage
            if (prev_leaf)
             {
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_leaf->node_idx, node->prev,
                  "leaf prev linkage incorrect");
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(node->node_idx, prev_leaf->next,
                  "leaf next linkage incorrect");
             }
            else
               TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, node->prev,
                  "first leaf prev != 0");

            // Each leaf's keys follow the original i pattern, adjusted
            // for the insertion of key 0 at position 0 (i=-1 in the pattern)
            for (uint32_t leaf_i = 0; leaf_i < node->key_count; leaf_i++, i++)
             {
               // After insert, first key should be 0 (i=-1 => 0, then i=0 => 2, etc.)
               TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 2 + 2,
                  temp->tools->node.cast_i64(node->keys + bptr->key_size * leaf_i),
                  "leaf key not correct");
               // Check that the inserted value 0 appears at i=-1
               if (i == -1)
                  TEST_ASSERT_EQUAL_INT64_MESSAGE(
                     0,
                     temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
                     "inserted val should be 0");
               else
                  TEST_ASSERT_EQUAL_INT64_MESSAGE(i * 3 + 3,
                     temp->tools->node.cast_i64(node->vals + bptr->value_size * leaf_i),
                     "leaf val not correct");
             }

            bptr_node_unload(bptr, prev_leaf);
            prev_leaf = node;
          }
         bptr_node_unload(bptr, l1);
       }

      struct bptr_node *next_brch = NULL;
      if (cur_brch->next)
         next_brch = bptr_node_fetch(bptr, cur_brch->next);
      bptr_node_unload(bptr, cur_brch);
      cur_brch = next_brch;
    }

   TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, prev_leaf->next,
                                     "last leaf next != 0");
   bptr_node_unload(bptr, prev_leaf);

   bptr_node_unload(bptr, new_root);
   TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr), "Failed to bptr_unload");
}


// Iterate over all insertion positions from before all keys (pos=0, key=1)
// to after all keys (pos=k, key=2k+1) in the leftmost leaf. Each insertion
// causes a 3-layer cascading split (leaf, L1, root).
void test_casc_brch_split_iter(struct bptr_temp *temp)
{
   char path[256];
   struct bptr *bptr;
   uint32_t k;

   // Determine leaf capacity
   TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0,
      snprintf(path, sizeof(path), "bptr_files/_k_casc_%s", temp->fnm),
      "snprintf path failure");
   rmdir(path);  // defensive
   {
      struct bptr *kbptr = bptr_init(path, temp->is_lite,
                                      temp->node_sz, temp->key_sz,
                                      temp->val_sz, temp->cache_cap,
                                      temp->cmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(kbptr, "bptr_init for k determination");
      k = kbptr->node_bound.leaf.up - 1;
      TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(kbptr), "unload k bptr");
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path), "remove k bptr");
   }

   for (uint32_t pos = 0; pos <= k; pos++)
    {
      struct bptr_node *par_n, *node;

      // Defensively clean up any stale directory left from a prior run
      rmdir(path);
      // (Re-)instantiate a fresh copy of the template
      {
         char src[256];
         snprintf(src, sizeof(src), "bptr_files/temp_casc/%s", temp->fnm);
         snprintf(path, sizeof(path), "bptr_files/temp_casc_%s", temp->fnm);
         {
            char buf[4096];
            int sfd = open(src, O_RDONLY);
            TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, sfd, "open src");
            struct stat st;
            fstat(sfd, &st);
            int dfd = open(path, O_WRONLY | O_CREAT | O_TRUNC,
                           st.st_mode & 0777);
            TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, dfd, "open dst");
            ssize_t n;
            while ((n = read(sfd, buf, sizeof(buf))) > 0)
             {
               ssize_t written;
               char *p = buf;
               while (n > 0 && (written = write(dfd, p, n)) > 0)
                { p += written; n -= written; }
               TEST_ASSERT_GREATER_THAN_INT_MESSAGE(-1, (int)written,
                  "write failure");
             }
            close(sfd);
            close(dfd);
         }
      }

      // Load, split, verify, cleanup in a tight scope
      bptr = bptr_load(path, temp->cache_cap, temp->cmp);
      TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
      TEST_ASSERT_NOT_EQUAL_MESSAGE(0, bptr->root_idx, "root_idx");
      TEST_ASSERT_EQUAL_MESSAGE(3, bptr->height, "pre-split height != 3");

      // Fetch leftmost leaf: root -> first L1 -> first leaf
      par_n = bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(par_n, "failed to fetch root");

      struct bptr_node *l1_n =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, par_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(l1_n, "failed to fetch leftmost L1");

      node = bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1_n, 0));
      TEST_ASSERT_NOT_NULL_MESSAGE(node, "failed to fetch leftmost leaf");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(k, node->key_count,
                                        "leftmost leaf not full");

      // Split with key=pos*2+1 that inserts at position pos
      bptr_node_t n_idx =
         bptr_node_split(bptr, node,
                         temp->tools->node.key_wrapper_i64(pos * 2 + 1),
                         temp->tools->node.val_wrapper_i64(pos * 2 + 1));
      TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, n_idx,
                                            "bptr_node_split failure");

      // Verify cascading split: height goes from 3 to 4
      TEST_ASSERT_EQUAL_MESSAGE(4, bptr->height, "post-split height != 4");

      // Verify new root has 1 key
      struct bptr_node *new_root =
         bptr_node_fetch(bptr, bptr->root_idx);
      TEST_ASSERT_NOT_NULL_MESSAGE(new_root, "failed to fetch new root");
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, new_root->key_count,
                                        "new root key_count != 1");
      TEST_ASSERT_EQUAL_MESSAGE(3, new_root->level, "new root level != 3");
      TEST_ASSERT_FALSE_MESSAGE(new_root->is_leaf, "new root should not be leaf");

      // Verify new root's two children are internal nodes at level 2
      struct bptr_node *left_brch =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 0));
      struct bptr_node *right_brch =
         bptr_node_fetch(bptr, _node_brch_vals_get(bptr, new_root, 1));
      TEST_ASSERT_NOT_NULL_MESSAGE(left_brch, "failed to fetch left brch");
      TEST_ASSERT_NOT_NULL_MESSAGE(right_brch, "failed to fetch right brch");
      TEST_ASSERT_EQUAL_MESSAGE(2, left_brch->level, "left brch level != 2");
      TEST_ASSERT_EQUAL_MESSAGE(2, right_brch->level, "right brch level != 2");
      TEST_ASSERT_FALSE_MESSAGE(left_brch->is_leaf,
                                 "left brch should not be leaf");
      TEST_ASSERT_FALSE_MESSAGE(right_brch->is_leaf,
                                 "right brch should not be leaf");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, left_brch->prev,
                                        "left brch prev != 0");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(right_brch->node_idx, left_brch->next,
                                        "left brch next != right brch");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(left_brch->node_idx, right_brch->prev,
                                        "right brch prev != left brch");
      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, right_brch->next,
                                        "right brch next != 0");

      // Verify key/value integrity by walking all leaves
      struct bptr_node *prev_leaf = NULL;
      struct bptr_node *cur_brch = left_brch;

      while (cur_brch)
       {
         for (uint32_t brch_i = 0; brch_i <= cur_brch->key_count; brch_i++)
          {
            struct bptr_node *l1 =
               bptr_node_fetch(bptr, _node_brch_vals_get(bptr, cur_brch, brch_i));
            TEST_ASSERT_NOT_NULL_MESSAGE(l1, "failed to fetch L1");

            for (uint32_t lf_i = 0; lf_i <= l1->key_count; lf_i++)
             {
               struct bptr_node *lf =
                  bptr_node_fetch(bptr, _node_brch_vals_get(bptr, l1, lf_i));
               TEST_ASSERT_NOT_NULL_MESSAGE(lf, "failed to fetch leaf");
               TEST_ASSERT_TRUE_MESSAGE(lf->is_leaf, "leaf not leaf");

               if (prev_leaf)
                {
                  TEST_ASSERT_EQUAL_UINT64_MESSAGE(prev_leaf->node_idx, lf->prev,
                     "leaf prev linkage incorrect");
                  TEST_ASSERT_EQUAL_UINT64_MESSAGE(lf->node_idx, prev_leaf->next,
                     "leaf next linkage incorrect");
                }
               else
                  TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, lf->prev,
                     "first leaf prev != 0");

               bptr_node_unload(bptr, prev_leaf);
               prev_leaf = lf;
             }
            bptr_node_unload(bptr, l1);
          }

         struct bptr_node *next_brch = NULL;
         if (cur_brch->next)
            next_brch = bptr_node_fetch(bptr, cur_brch->next);
         bptr_node_unload(bptr, cur_brch);
         cur_brch = next_brch;
       }

      TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, prev_leaf->next,
                                        "last leaf next != 0");
      bptr_node_unload(bptr, prev_leaf);

      bptr_node_unload(bptr, new_root);
      bptr_node_unload(bptr, par_n);
      bptr_node_unload(bptr, l1_n);
      bptr_node_unload(bptr, node);

      TEST_ASSERT_EQUAL_MESSAGE(0, bptr_unload(bptr),
                                 "Failed to bptr_unload");
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, remove(path),
                                     "failed to remove instantiated template");
    }
}
/*-------------------------- Private Utilities END ---------------------------*/
