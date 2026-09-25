/*----------------------------- Private Includes -----------------------------*/
#include "temp_full.h"
#include "bptr_internal.h"
#include "bptree.h"
#include "bptr_node.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>
#include "unity.h"
/*--------------------------- Private Includes END ---------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static long long ensure_par_dirs(char *path, mode_t mode);
static int _node_fill(struct bptr *self, struct bptr_node *node,
                      int64_t *st, int64_t interval);
static int64_t _find_correct_key(struct bptr *self, struct bptr_node *node,
                                 uint32_t idx);
static int _copy_file(const char *dst, const char *src);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int temp_full_generate(unsigned int lay_cnt, int64_t st, int64_t interval,
                       _Bool is_lite, uint32_t node_size)
{
   struct stat fst;
   char path[PATH_MAX] = "bptr_files/temp/full/";
   struct bptr *bptr;
   struct bptr_node *node;
   long long len;
   int64_t st_it = st;

   if (lay_cnt == 0) { perror("lay_cnt == 0"); return 1; }

   len = ensure_par_dirs(path, 0755);
   if (len == -1) { perror("mkdir_parents"); return 1; }
   sprintf(path + len, "%u-%" PRIi64 "-%" PRIi64 ".bptr",
           lay_cnt, st, interval);

   if (stat(path, &fst) == 0 && S_ISREG(fst.st_mode)) return 0;
   bptr = bptr_init(path, is_lite, node_size,
                    sizeof(int64_t), sizeof(int64_t), 256, &cmp_i64);
   if (bptr == NULL) { perror("bptr_init"); return 1; }
   node = bptr_node_new(bptr, 0);
   node->level = lay_cnt - 1;
   bptr->height = lay_cnt;
   bptr->node_cnt = 1;
   bptr->root_idx = node->node_idx;
   if (_node_fill(bptr, node, &st_it, interval))
    { perror("_node_fill"); return 1; }

   if (bptr_unload(bptr)) { perror("bptr_unload"); return 1; }
   return 0;
}


int temp_instantiate(const char *path, const char *temp)
{
   char fullpath[PATH_MAX] = "bptr_files/";

   strcat(fullpath, path);
   if (ensure_par_dirs(fullpath, 0755) == -1)
    { perror("mkdir_parents"); return -1; }

   switch (_copy_file(fullpath, temp))
    {
   case 0  : break;
   case -1 : perror("_copy_file"); return -1;
   case 1  : return 1;
   default : perror("Unreachable"); return -2;
    }

   return 0;
}


void temp_full_verify(struct bptr *bptr,
                      unsigned int lay_cnt, int64_t st, int64_t interval,
                      _Bool has_new_kv, int64_t key, int64_t val)
{
   struct bptr_node *node, *par_n, *next_n;
   _Bool has_met_new_kv = 0;
   uint_fast64_t leaf_cnt = 0;

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "bptr == NULL");
   if (lay_cnt == 0) return;
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
   if (has_new_kv)
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         ((leaf_cnt - 1) * (bptr->node_bound.leaf.up - 1) + 1) * interval,
         st,
         "record count (derived from st) not correct");
   else
      TEST_ASSERT_EQUAL_INT64_MESSAGE(
         leaf_cnt * (bptr->node_bound.leaf.up - 1) * interval,
         st,
         "record count (derived from st) not correct");
   TEST_ASSERT_EQUAL_UINT64_MESSAGE(st / interval, bptr->record_cnt,
                                    "record count does not match st");

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

      node = prior_n;  // extreme node of this layer, still loaded
    }
   if (bptr->height > 1) bptr_node_unload(bptr, node);
   // TODO: verify the remaining stats of `bptr`
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static long long ensure_par_dirs(char *path, mode_t mode)
{
   long long cnt = 0;

   for (char *p = path; *p; p++, cnt++)
    {
      struct stat st;
      if (*p != '/') continue;

      *p = '\0';
      if (mkdir(path, mode) == -1 && errno != EEXIST)
         return -1;

      if (stat(path, &st) == -1 || !S_ISDIR(st.st_mode))
       { if (errno == 0) errno = ENOTDIR; return -1; }

      *p = '/';
    }

   return cnt;
}


int cmp_i64(const void *lhs, const void *rhs)
{
   int64_t diff = *(const int64_t *)lhs - *(const int64_t *)rhs;
   return diff < 0 ? -1 : diff > 0 ? 1 : 0;
}


static void _node_fill_leaf(struct bptr *self, struct bptr_node *node,
                            int64_t *st, int64_t interval)
{
   for (uint32_t k_mx = self->node_bound.leaf.up - 1;
        node->key_count < k_mx; node->key_count++, *st += interval)
    {
      ((int64_t*)node->keys)[node->key_count] = *st;
      ((int64_t*)node->vals)[node->key_count] = *st * 2;
    }
   self->record_cnt += node->key_count;
}


static int _node_fill(struct bptr *self, struct bptr_node *node,
                      int64_t *st, int64_t interval)
#define _find_lmk(T) do { \
   struct bptr_node *c_it[2]; int c_i = 0; \
   c_it[c_i] = bptr_node_fetch(self, *(T*)child->vals); \
   if (c_it[c_i] == NULL) { perror("_node_fill: bptr_node_new"); return 1; } \
   for (; !c_it[c_i]->is_leaf; c_i ^= 1) \
   { \
      c_it[c_i ^ 1] = bptr_node_fetch(self, *(T*)c_it[c_i]->vals); \
      if (c_it[c_i ^ 1] == NULL) \
       { perror("_node_fill: bptr_node_new"); return 1; } \
      bptr_node_unload(self, c_it[c_i]); \
   } \
   lmk = *(int64_t*)c_it[c_i]->keys; \
} while (0)
#define _set_kv(T) do { \
   ((int64_t*)node->keys)[node->key_count] = lmk; \
   ((T*)node->vals)[node->key_count + 1] = child->node_idx; \
} while (0)
{
   struct bptr_node *child, *prev_child;

   // base case
   if (node->is_leaf) { _node_fill_leaf(self, node, st, interval); return 0; }

   // internal node
   child = bptr_node_new(self, node->node_idx);
   if (child == NULL) { perror("_node_fill: bptr_node_new"); return 1; }
   if (_node_fill(self, child, st, interval)) return 1;
   if (self->is_lite) ((BPTR_LITE_PTR_TYPE*)node->vals)[0] = child->node_idx;
   else               ((BPTR_NORM_PTR_TYPE*)node->vals)[0] = child->node_idx;
   self->node_cnt++;
   child->prev = 0;
   prev_child = child;

   for (uint32_t key_mx = self->node_bound.brch.up - 1;
        node->key_count < key_mx; node->key_count++)
    {
      child = bptr_node_new(self, node->node_idx);
      if (child == NULL) { perror("_node_fill: bptr_node_new"); return 1; }
      prev_child->next = child->node_idx;
      child->prev = prev_child->node_idx;
      bptr_node_unload(self, prev_child);
      if (_node_fill(self, child, st, interval)) return 1;
      int64_t lmk;
      if (self->is_lite)
         { _find_lmk(BPTR_LITE_PTR_TYPE); _set_kv(BPTR_LITE_PTR_TYPE); }
      else
         { _find_lmk(BPTR_NORM_PTR_TYPE); _set_kv(BPTR_NORM_PTR_TYPE); }
      self->node_cnt++;
      prev_child = child;
    }

   child->next = 0;
   bptr_node_unload(self, child);

   return 0;
#undef  _find_lmk
#undef  _set_kv
}


static int _copy_file(const char *dst, const char *src)
{
   int sfd, dfd;
   struct stat st;
   char buf[4096];
   ssize_t n;

   if (access(dst, F_OK) == 0) return 1;

   sfd = open(src, O_RDONLY);
   if (sfd == -1) { perror("open src"); return -1; }
   if (fstat(sfd, &st) == -1)
    { perror("fstat sfd"); close(sfd); return -1; }

   dfd = open(dst, O_WRONLY | O_CREAT, st.st_mode & 0777);
   if (dfd == -1) { perror("open dfd"); close(sfd); return -1; }

   while ((n = read(sfd, buf, sizeof(buf))) > 0)
    {
      char *p = buf;
      ssize_t written;
      while (n > 0 && (written = write(dfd, p, n) > 0))
       { p += written; n -= written; }
      if (written < 0 || n != 0)
       { perror("write dfd"); close(dfd); close(sfd); return -1; }
    }
   close(dfd);
   close(sfd);
   return 0;
}


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
