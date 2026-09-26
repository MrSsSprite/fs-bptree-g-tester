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


/*------------------------------ Private Macros ------------------------------*/
/* node cache capacity of the generated tree: one node stays pinned per level
 * while its subtree is filled, plus one spare slot for the level list sibling
 * fetched while a node is appended, so a taller tree cannot be generated */
#define FULL_GEN_CACHE_CAP 256u

/* width-aware counterpart of `_node_brch_vals_get' in `bptr_node.h' */
#define _node_brch_vals_set(self, node, idx, val) do \
{ \
   if ((self)->is_lite) \
      *((BPTR_LITE_PTR_TYPE*)(node)->vals + (idx)) = \
         (BPTR_LITE_PTR_TYPE)(val); \
   else \
      *((BPTR_NORM_PTR_TYPE*)(node)->vals + (idx)) = \
         (BPTR_NORM_PTR_TYPE)(val); \
} while (0)
/*---------------------------- Private Macros END ----------------------------*/


/*---------------------------- Private Variables -----------------------------*/
/* fixture being written by `temp_full_generate'; "" when none is in flight */
static char _gen_path[PATH_MAX];
/*-------------------------- Private Variables END ---------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static long long ensure_par_dirs(char *path, mode_t mode);
static int64_t _find_correct_key(struct bptr *self, struct bptr_node *node,
                                 uint32_t idx);
static int _copy_file(const char *dst, const char *src);
static int _gen_path_drop(void);
static int _fixture_matches(const char *path, unsigned int lay_cnt,
                            _Bool is_lite, uint32_t node_size);
static void _node_fill_leaf(struct bptr *self, struct bptr_node *node,
                            int64_t *st, int64_t interval);
static void _level_chain_push(struct bptr *self, bptr_node_t *prev_at_level,
                              struct bptr_node *node);
static void _node_fill(struct bptr *self, bptr_node_t *prev_at_level,
                       struct bptr_node *node, int64_t *st, int64_t interval,
                       int64_t *lmk);
static struct bptr_node *create_child(struct bptr *self,
                                      bptr_node_t *prev_at_level,
                                      struct bptr_node *par_n,
                                      int64_t *st, int64_t interval,
                                      int64_t *lmk);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Build a perfectly full tree of @p lay_cnt levels and write it to
 *          `bptr_files/temp/full/<lay_cnt>-<st>-<interval>.bptr'
 *
 * Keys start at @p st and step by @p interval; each value is the key times
 * two.  Every leaf holds `leaf.up - 1' keys, every internal node holds
 * `brch.up - 1' keys and `brch.up' children, and key i of an internal node is
 * the 0th key of the leftmost descendant leaf of its (i + 1)th child.  Every
 * level forms one doubly linked list crossing parent boundaries.
 *
 * @param[in] lay_cnt    number of levels; 1 yields a single leaf
 * @param[in] st         first key
 * @param[in] interval   distance between two successive keys
 * @param[in] is_lite    use the 4-byte child pointer layout
 * @param[in] node_size  size of a node in bytes
 *
 * @return  0 on success; non-0 on failure, with the partial fixture removed
 *
 * @note    Returns 0 without touching a file that already exists and matches
 *          the requested layout; a file that does not match, be it a partial
 *          image left by an aborted run or one built with other parameters,
 *          is reported as a failure so that it cannot be served silently.
 * @note    A failure reported through a Unity assertion does not return, so
 *          the caller has to drop the fixture through @c temp_full_discard ;
 *          every other failure is cleaned up here.
 */
int temp_full_generate(unsigned int lay_cnt, int64_t st, int64_t interval,
                       _Bool is_lite, uint32_t node_size)
{
   struct stat fst;
   char path[PATH_MAX] = "bptr_files/temp/full/";
   struct bptr *bptr;
   struct bptr_node *node;
   bptr_node_t *prev_at_level;
   uint32_t ptr_size;
   long long len;
   int64_t st_it = st, lmk;

   /* a node level is stored in a uint16_t; the node cache also caps the height
    * (see `FULL_GEN_CACHE_CAP') */
   if (lay_cnt == 0 || lay_cnt > FULL_GEN_CACHE_CAP - 2u)
    { perror("lay_cnt out of range"); return 1; }
   /* the smallest node `bptr_init' accepts: metadata and one key plus the two
    * child pointers of its minimum fanout, so that a request which could
    * never build a tree is refused before any file is consulted or created */
   ptr_size = is_lite ? BPTR_LITE_PTR_BYTE : BPTR_NORM_PTR_BYTE;
   if (node_size < BPTR_NODE_METADATA_BYTE + ptr_size +
                   2 * ((uint32_t)sizeof (int64_t) + ptr_size))
    { perror("node_size out of range"); return 1; }

   /* drop a fixture left armed by an aborted call before writing anything */
   if (_gen_path_drop())
    {
      fprintf(stderr, "temp_full_generate: cannot drop %s\n", _gen_path);
      return 1;
    }

   len = ensure_par_dirs(path, 0755);
   if (len == -1) { perror("mkdir_parents"); return 1; }
   sprintf(path + len, "%u-%" PRIi64 "-%" PRIi64 ".bptr",
           lay_cnt, st, interval);

   if (stat(path, &fst) == 0 && S_ISREG(fst.st_mode))
    {
      /* a fixture written by an earlier run is reused as is, but only when it
       * really is one: a partial image left by an aborted run, or an image
       * built with some other layout, must not be served silently */
      switch (_fixture_matches(path, lay_cnt, is_lite, node_size))
       {
      case 1  : return 0;
      case 0  :
         fprintf(stderr, "temp_full_generate: %s is not a complete fixture of "
                         "this layout; delete it and retry\n", path);
         return 1;
      default :
         fprintf(stderr, "temp_full_generate: cannot read %s\n", path);
         return 1;
       }
    }
   bptr = bptr_init(path, is_lite, node_size,
                    sizeof(int64_t), sizeof(int64_t), FULL_GEN_CACHE_CAP,
                    cmp_i64);
   if (bptr == NULL) { perror("bptr_init"); remove(path); return 1; }
   /* a full tree cannot carry a key if a leaf holds none */
   if (bptr->node_bound.leaf.up < 2)
    {
      perror("node_size too small: leaf.up < 2");
      bptr_unload(bptr);
      remove(path);
      return 1;
    }

   /* `prev_at_level[i]' holds the index of the node most recently created at
    * level i; nodes are laid down from left to right, so it is the left
    * sibling of the next node created at that level. */
   prev_at_level = calloc(lay_cnt, sizeof (bptr_node_t));
   if (prev_at_level == NULL)
    {
      perror("calloc");
      bptr_unload(bptr);
      remove(path);
      return 1;
    }

   /* the fixture now exists but is incomplete: remember it, so that a Unity
    * assertion, which does not return, can still be unwound into
    * `temp_full_discard' by the caller */
   strcpy(_gen_path, path);

   /* The node layout (is_leaf, flags and the keys/vals split) is derived from
    * the node level at creation.  `bptr_node_new' increments `height' when the
    * root is created; pre-set it to the target height - 1 so that the root is
    * born at level `lay_cnt - 1', and thence with the layout of an internal
    * node, rather than the leaf layout of a fresh tree. */
   bptr->height = lay_cnt - 1;
   node = bptr_node_new(bptr, 0);
   if (node == NULL) { perror("bptr_node_new"); goto GEN_ERR; }
   bptr->node_cnt = 1;
   bptr->root_idx = node->node_idx;
   _level_chain_push(bptr, prev_at_level, node);
   _node_fill(bptr, prev_at_level, node, &st_it, interval, &lmk);
   bptr_node_unload(bptr, node);

   free(prev_at_level);
   if (bptr_unload(bptr))
    {
      perror("bptr_unload");
      remove(path);
      _gen_path[0] = '\0';
      return 1;
    }
   _gen_path[0] = '\0';
   return 0;

   /*-------------------------- Error Handling Zone --------------------------*/
GEN_ERR:
   free(prev_at_level);
   bptr_unload(bptr);   /* best effort: flush what has been written ... */
   remove(path);        /* ... then drop the partial fixture */
   _gen_path[0] = '\0';
   return 1;
}


void temp_full_discard(void)
{
   (void)_gen_path_drop();
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


/**
 * @brief   Check whether an existing file is the fixture that was asked for
 *
 * The header is read directly (see `core/docs/header_bin_layout.md'): a
 * partial image left by an aborted run still carries the header written when
 * the file was created, whence a height of 0 and no node count.  The checks
 * prove that the file is consistent with the request, not that this generator
 * wrote it: `st' and `interval' live only in the file name.
 *
 * @param[in] path       file to inspect
 * @param[in] lay_cnt    number of levels the caller asked for
 * @param[in] is_lite    child pointer layout the caller asked for
 * @param[in] node_size  node size the caller asked for
 *
 * @return  1 if @p path is a complete fixture of that layout, 0 if it is not,
 *          and -1 if it cannot be read.
 */
static int _fixture_matches(const char *path, unsigned int lay_cnt,
                            _Bool is_lite, uint32_t node_size)
{
   unsigned char hdr[64];
   struct stat fst;
   uint32_t version, stored_node_size, height;
   uint16_t key_size, value_size;
   uint_fast64_t node_cnt, blocks;
   FILE *file;
   size_t rd;

   file = fopen(path, "rb");
   if (file == NULL) return -1;
   /* measure through the open handle: the file must not change between the
    * size and the content check */
   if (fstat(fileno(file), &fst) == -1) { fclose(file); return -1; }
   rd = fread(hdr, 1, sizeof hdr, file);
   fclose(file);
   if (rd < sizeof hdr) return 0;

   memcpy(&version, hdr + 4, sizeof version);
   memcpy(&stored_node_size, hdr + 8, sizeof stored_node_size);
   memcpy(&key_size, hdr + 12, sizeof key_size);
   memcpy(&value_size, hdr + 14, sizeof value_size);
   memcpy(&height, hdr + 24, sizeof height);
   if (memcmp(hdr, BPTR_MAGIC_STR, 4) ||
       (version & 0x7Fu) != BPTR_CURRENT_VERSION ||
       ((version & 0x80u) ? 1 : 0) != (is_lite ? 1 : 0) ||
       stored_node_size != node_size ||
       key_size != sizeof (int64_t) || value_size != sizeof (int64_t) ||
       height != lay_cnt)
      return 0;

   /* node_cnt is the 4th pointer-sized field of the header */
   if (is_lite)
    {
      uint32_t cnt;
      memcpy(&cnt, hdr + 28 + 3 * BPTR_LITE_PTR_BYTE, sizeof cnt);
      node_cnt = cnt;
    }
   else
    {
      uint64_t cnt;
      memcpy(&cnt, hdr + 28 + 3 * BPTR_NORM_PTR_BYTE, sizeof cnt);
      node_cnt = cnt;
    }
   if (node_cnt == 0) return 0;

   /* unsigned and overflow free: `node_cnt' is read from the file, and the
    * block count cannot exceed the size of the file */
   blocks = (uint_fast64_t)fst.st_size / (uint_fast64_t)node_size;
   if ((uint_fast64_t)fst.st_size % (uint_fast64_t)node_size) return 0;
   return blocks == node_cnt + 1;
}


/**
 * @brief   Append @p node to the doubly linked list of its level
 *
 * Every level of the tree forms a single list crossing parent boundaries: the
 * rightmost child of a node is linked to the leftmost child of the next node
 * of the parent layer.  Nodes are laid down from left to right, so the node
 * created last at a level is the left sibling of the next node created there.
 *
 * @param[in,out] self           bptr obj.
 * @param[in,out] prev_at_level  one entry per level; holds the index of the
 *                               node most recently created at that level, or
 *                               0 if none
 * @param[in,out] node           node to append; @c prev and @c next are set
 *
 * @note  A failed fetch is reported through a Unity assertion, which does not
 *        return.
 */
static void _level_chain_push(struct bptr *self, bptr_node_t *prev_at_level,
                              struct bptr_node *node)
{
   struct bptr_node *prev_n;

   node->next = 0;
   node->prev = prev_at_level[node->level];
   if (prev_at_level[node->level] == 0)
    {
      prev_at_level[node->level] = node->node_idx;
      return;
    }

   prev_n = bptr_node_fetch(self, prev_at_level[node->level]);
   TEST_ASSERT_NOT_NULL_MESSAGE(prev_n, "_level_chain_push: bptr_node_fetch");
   prev_n->next = node->node_idx;
   prev_n->is_dirty = 1;
   bptr_node_unload(self, prev_n);

   prev_at_level[node->level] = node->node_idx;
}


/**
 * @brief   Fill @p node and its whole subtree with a perfect tree
 *
 * @param[in,out] self           bptr obj.
 * @param[in,out] prev_at_level  level list bookkeeping, see @c _level_chain_push
 * @param[in,out] node           node to fill; its level decides whether it is
 *                               a leaf (base case) or an internal node
 * @param[in,out] st             key cursor; advances as keys are laid down
 * @param[in]     interval       distance between two successive keys
 * @param[out]    lmk            leftmost key of the filled subtree, i.e., the
 *                               0th key of its leftmost descendant leaf
 *
 * @note  Failures are reported through Unity assertions inside
 *        @c create_child ; this function does not return on failure.
 */
static void _node_fill(struct bptr *self, bptr_node_t *prev_at_level,
                       struct bptr_node *node, int64_t *st, int64_t interval,
                       int64_t *lmk)
{
   struct bptr_node *child;
   int64_t iter_lmk;

   // base case
   if (node->is_leaf)
    {
      _node_fill_leaf(self, node, st, interval);
      *lmk = ((int64_t*)node->keys)[0];
      return;
    }

   /* leftmost child; its leftmost key is also the one of `node' */
   child = create_child(self, prev_at_level, node, st, interval, lmk);
   _node_brch_vals_set(self, node, 0, child->node_idx);
   bptr_node_unload(self, child);

   for (; node->key_count < self->node_bound.brch.up - 1; node->key_count++)
    {
      child = create_child(self, prev_at_level, node, st, interval, &iter_lmk);
      /* the ith key of an internal node is the 0th key of the leftmost
       * descendant leaf of its (i + 1)th child */
      ((int64_t*)node->keys)[node->key_count] = iter_lmk;
      _node_brch_vals_set(self, node, node->key_count + 1, child->node_idx);
      bptr_node_unload(self, child);
    }
}


/**
 * @brief   Create a child of @p par_n and fill its whole subtree
 *
 * @param[in,out] self           bptr obj.; @c node_cnt is incremented
 * @param[in,out] prev_at_level  level list bookkeeping, see @c _level_chain_push
 * @param[in]     par_n          parent node; the new node's level, whence its
 *                               layout, is derived from it
 * @param[in,out] st             key cursor; advances as keys are laid down
 * @param[in]     interval       distance between two successive keys
 * @param[out]    lmk            leftmost key of the created subtree
 *
 * @return  the created, still loaded node
 *
 * @note  A failed node creation is reported through a Unity assertion, which
 *        does not return.
 */
static struct bptr_node *create_child(struct bptr *self,
                                      bptr_node_t *prev_at_level,
                                      struct bptr_node *par_n,
                                      int64_t *st, int64_t interval,
                                      int64_t *lmk)
{
   struct bptr_node *node = bptr_node_new(self, par_n->node_idx);

   TEST_ASSERT_NOT_NULL_MESSAGE(node, "create_child: bptr_node_new");
   self->node_cnt++;

   _level_chain_push(self, prev_at_level, node);
   _node_fill(self, prev_at_level, node, st, interval, lmk);

   return node;
}


/**
 * @brief   Drop the fixture armed by a `temp_full_generate' call
 *
 * The path stays armed when the file could not be removed for any reason but
 * a missing one, so that a later call can retry it.
 *
 * @return  0 when no path is armed or the file was removed; non-0 otherwise.
 */
static int _gen_path_drop(void)
{
   if (_gen_path[0] == '\0') return 0;

   if (remove(_gen_path) && errno != ENOENT) return 1;
   _gen_path[0] = '\0';
   return 0;
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
