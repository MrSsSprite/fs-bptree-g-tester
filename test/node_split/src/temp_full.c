/*----------------------------- Private Includes -----------------------------*/
#include "temp_full.h"
#include "bptr_internal.h"
#include "bptree.h"
#include "bptr_node.h"
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>
/*--------------------------- Private Includes END ---------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static long long ensure_par_dirs(char *path, mode_t mode);
static int cmp_i64(const void *lhs, const void *rhs);
static int _node_fill(struct bptr *self, struct bptr_node *node,
                      int64_t *st, int64_t interval);
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
                    sizeof(int64_t), sizeof(int64_t), 256, cmp_i64);
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


static int cmp_i64(const void *lhs, const void *rhs)
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
/*-------------------------- Private Functions END ---------------------------*/
