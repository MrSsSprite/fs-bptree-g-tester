/*----------------------------- Private Includes -----------------------------*/
#include "temp_full.h"
#include "temp_internal.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include "bptr_io.h"
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
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


/*--------------------------- Forward Declarations ---------------------------*/
static void _gen_drop(const char *path);
static int _gen_fail(int status, const char *path);
static void _gen_report_interval(unsigned int lay_cnt, int64_t st,
                                 int64_t interval, _Bool is_lite,
                                 uint32_t node_size);
static _Bool _gen_ascending(unsigned int lay_cnt, int64_t st, int64_t interval,
                            _Bool is_lite, uint32_t node_size,
                            int64_t *asc_st, int64_t *asc_interval);
static _Bool _gen_record_cnt(unsigned int lay_cnt, _Bool is_lite,
                             uint32_t node_size, int64_t *rec_cnt);
static _Bool _gen_node_count_fits(unsigned int lay_cnt, uint64_t fanout,
                                 uint64_t limit);
static int _gen_check_pointers(unsigned int lay_cnt, _Bool is_lite,
                               uint32_t node_size);
static _Bool _gen_kv_fits(unsigned int lay_cnt, int64_t st, int64_t interval,
                          _Bool is_lite, uint32_t node_size);
static int _fixture_matches(const char *path, unsigned int lay_cnt,
                            _Bool is_lite, uint32_t node_size);
static void _node_fill_leaf(struct bptr *self, struct bptr_node *node,
                            int64_t *st, int64_t interval, uint32_t count);
static int _level_chain_push(struct bptr *self, bptr_node_t *prev_at_level,
                             struct bptr_node *node);
static int _node_fill(struct bptr *self, bptr_node_t *prev_at_level,
                      struct bptr_node *node, int64_t *st, int64_t interval,
                      int64_t *lmk, const struct temp_shape_node *shape);
static int create_child(struct bptr *self, bptr_node_t *prev_at_level,
                        struct bptr_node *par_n, int64_t *st, int64_t interval,
                        int64_t *lmk, struct bptr_node **out,
                        const struct temp_shape_node *shape);
static _Bool _gen_shape_kv_fits(uint64_t records, int64_t st, int64_t interval);
static int _gen_shape_check_pointers(uint64_t nodes, _Bool is_lite,
                                      uint32_t node_size);
static void _gen_shape_report_interval(uint64_t records, int64_t st,
                                        int64_t interval);
static int _shape_fixture_matches(const char *path, unsigned int levels,
                                   int64_t st, int64_t interval, _Bool is_lite,
                                   uint32_t node_size, const struct temp_shape *shape,
                                   const struct temp_shape_counts *counts);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int temp_full_path(char *buf, size_t size, const char *dir, unsigned int lay_cnt,
                   int64_t st, int64_t interval)
{
   return temp_path(buf, size, dir, lay_cnt, st, interval, NULL);
}


/**
 * @brief   Build a perfectly full tree of @p lay_cnt levels and write it to
 *          `<dir>/<lay_cnt>-<st>-<interval>.bptr' (the name is built by
 *          `temp_full_path')
 *
 * Keys start at @p st and step by @p interval; each value is the key times
 * two.  Every leaf holds `leaf.up - 1' keys, every internal node holds
 * `brch.up - 1' keys and `brch.up' children, and key i of an internal node is
 * the 0th key of the leftmost descendant leaf of its (i + 1)th child.  Every
 * level forms one doubly linked list crossing parent boundaries.
 *
 * A plain function rather than a test case: it does not assert, so it does not
 * need a Unity abort frame to be callable, and the caller decides how a failure
 * is surfaced.
 *
 * @param[in] dir        directory of the template, or with a trailing '/'
 * @param[in] lay_cnt    number of levels; 1 yields a single leaf
 * @param[in] st         first key
 * @param[in] interval   distance between two successive keys; must not be
 *                       negative, so that the keys ascend
 * @param[in] is_lite    use the 4-byte child pointer layout
 * @param[in] node_size  size of a node in bytes
 *
 * @return  TEMP_FULL_OK (0) on success; TEMP_FULL_E_LAY_CNT,
 *          TEMP_FULL_E_NODE_SIZE, TEMP_FULL_E_INTERVAL,
 *          TEMP_FULL_E_KV_OVERFLOW or TEMP_FULL_E_PTR_OVERFLOW (negative: the
 *          request cannot be served) or
 *          a positive environment error otherwise.  An
 *          error is named by `temp_full_strerror' and reported on stderr with
 *          the path being worked on, once there is one: the template directory
 *          first, then the template path.
 *
 * @note    A negative @p interval is refused (TEMP_FULL_E_INTERVAL): the keys of
 *          a template have to ascend, because the split of a full node and the
 *          verifier both search a node in the comparator's order, so a
 *          descending lattice is not a tree they can serve.  The refusal is
 *          reported before a path is built -- no directory and no file is
 *          touched -- and it names the ascending request that builds the same
 *          keys: `interval * -1', starting at `st + interval * (record_cnt - 1)',
 *          the last key of the descending lattice.
 * @note    Pointer and file-offset overflow are refused before a directory is
 *          created or an existing template is reused.  The checks cover this
 *          generation only, including the header block, and allow exact maxima.
 *          Users must ensure their later operations on a template do not
 *          overflow; no headroom is reserved for an insertion or split.
 * @note    Key/value overflow is checked before any directory or file is
 *          touched, including reuse.  The first and last keys and their doubled
 *          values may reach the int64_t limits; no space is reserved for later
 *          insertions or splits.  This guarantees key/value arithmetic during
 *          generation only: callers must ensure their later use of the template
 *          does not overflow.
 * @note    Returns TEMP_FULL_OK without touching a file that already exists and
 *          matches the requested layout; a file that does not match, be it a
 *          partial image left by an interrupted run or one built with other
 *          parameters, is reported as TEMP_FULL_E_FIXTURE and left untouched,
 *          so that it cannot be served silently.
 * @note    Every other failure removes the partial image before returning, so a
 *          failed call does not leave a partial template behind.
 * @note    TEMP_FULL_E_INIT, TEMP_FULL_E_ALLOC and TEMP_FULL_E_CHAIN are what a
 *          failure inside the library's own file and cache code looks like from
 *          here -- a header that cannot be written, a node that cannot be
 *          allocated, a flush that fails while a node is evicted -- so an
 *          environment error can be reported under one of them.  `bptr_errno'
 *          then carries the library's reason; `errno' may already have been
 *          overwritten by the cleanup the failure triggered.  Only the final
 *          flush is reported as TEMP_FULL_E_WRITE.
 */
int temp_full_generate(const char *dir, unsigned int lay_cnt, int64_t st,
                       int64_t interval, _Bool is_lite, uint32_t node_size)
{
   return temp_generate(dir, lay_cnt, st, interval, is_lite, node_size, NULL);
}

int temp_generate(const char *dir, unsigned int lay_cnt, int64_t st,
                   int64_t interval, _Bool is_lite, uint32_t node_size,
                   const struct temp_shape *shape)
{
   struct stat fst;
   char path[PATH_MAX];
   struct bptr *bptr;
   struct bptr_node *node;
   bptr_node_t *prev_at_level;
   uint32_t ptr_size;
   int status = TEMP_FULL_OK;
   int64_t st_it = st, lmk;
   struct temp_shape_counts counts = {0};
   _Bool custom = shape != NULL && shape->override_count != 0;
   int path_len;

   /* a node level is stored in a uint16_t; the node cache also caps the height
    * (see `FULL_GEN_CACHE_CAP') */
   if (lay_cnt == 0 || lay_cnt > FULL_GEN_CACHE_CAP - 2u)
      return _gen_fail(TEMP_FULL_E_LAY_CNT, NULL);
   /* the smallest node `bptr_init' accepts: metadata and one key plus the two
    * child pointers of its minimum fanout, so that a request which could
    * never build a tree is refused before any file is consulted or created */
   ptr_size = is_lite ? BPTR_LITE_PTR_BYTE : BPTR_NORM_PTR_BYTE;
   if (node_size < BPTR_NODE_METADATA_BYTE + ptr_size +
                    2 * ((uint32_t)sizeof (int64_t) + ptr_size))
      return _gen_fail(TEMP_FULL_E_NODE_SIZE, NULL);
   /* A negative interval lays the keys down in descending order, and the keys of
    * a template have to ascend: the image is loaded with `cmp_i64' and both the
    * split of a full node and the verifier search it in that order.  Refuse the
    * request before a directory or a file is touched, and name the ascending
    * request that builds the same keys. */
   if (custom)
    {
      status = temp_shape_count(shape, lay_cnt, is_lite, node_size, &counts);
      if (status != TEMP_FULL_OK) return _gen_fail(status, NULL);
    }
   if (interval < 0)
    {
      if (custom) _gen_shape_report_interval(counts.records, st, interval);
      else _gen_report_interval(lay_cnt, st, interval, is_lite, node_size);
      return TEMP_FULL_E_INTERVAL;
    }
   if (custom)
    {
      if (!_gen_shape_kv_fits(counts.records, st, interval))
         return _gen_fail(TEMP_FULL_E_KV_OVERFLOW, NULL);
      status = _gen_shape_check_pointers(counts.nodes, is_lite, node_size);
    }
   else
    {
      if (!_gen_kv_fits(lay_cnt, st, interval, is_lite, node_size))
         return _gen_fail(TEMP_FULL_E_KV_OVERFLOW, NULL);
      status = _gen_check_pointers(lay_cnt, is_lite, node_size);
    }
   if (status != TEMP_FULL_OK) return status;

   path_len = temp_path(path, sizeof path, dir, lay_cnt, st, interval, shape);
   if (path_len < 0 || (size_t)path_len >= sizeof path ||
       strlen(strrchr(path, '/') == NULL ? path : strrchr(path, '/') + 1) > NAME_MAX)
      return _gen_fail(TEMP_FULL_E_NAME, NULL);
   size_t component_len = 0;
   for (const char *part = path; *part != '\0'; part++)
    {
      component_len = *part == '/' ? 0 : component_len + 1u;
      if (component_len > NAME_MAX) return _gen_fail(TEMP_FULL_E_NAME, NULL);
    }
   if (_ensure_par_dirs(path, 0755) == -1)
      return _gen_fail(TEMP_FULL_E_DIR, path);

   if (stat(path, &fst) == 0 && S_ISREG(fst.st_mode))
    {
      /* a template written by an earlier run is reused as is, but only when it
       * really is one: a partial image left by an interrupted run, or an image
       * built with some other layout, must not be served silently */
      status = _fixture_matches(path, lay_cnt, is_lite, node_size);
      if (status == TEMP_FULL_OK && custom)
         status = _shape_fixture_matches(path, lay_cnt, st, interval, is_lite,
                                          node_size, shape, &counts);
      if (status != TEMP_FULL_OK) return _gen_fail(status, path);
      return TEMP_FULL_OK;
    }
   bptr = bptr_init(path, is_lite, node_size,
                    sizeof(int64_t), sizeof(int64_t), FULL_GEN_CACHE_CAP,
                    cmp_i64);
   if (bptr == NULL)
    {
      /* `bptr_init' creates the file before it can still fail: drop whatever it
       * left behind */
      _gen_drop(path);
      return _gen_fail(TEMP_FULL_E_INIT, path);
    }
   /* a full tree cannot carry a key if a leaf holds none; unreachable while the
    * guard above mirrors `bptr_init''s minimum fanout, kept as a belt */
   if (bptr->node_bound.leaf.up < 2)
    {
      status = TEMP_FULL_E_NODE_SIZE;
      goto GEN_ERR;
    }

   /* `prev_at_level[i]' holds the index of the node most recently created at
    * level i; nodes are laid down from left to right, so it is the left
    * sibling of the next node created at that level. */
   prev_at_level = calloc(lay_cnt, sizeof (bptr_node_t));
   if (prev_at_level == NULL)
    {
      status = TEMP_FULL_E_ALLOC;
      goto GEN_ERR;
    }

   /* The node layout (is_leaf, flags and the keys/vals split) is derived from
    * the node level at creation.  `bptr_node_new' increments `height' when the
    * root is created; pre-set it to the target height - 1 so that the root is
    * born at level `lay_cnt - 1', and thence with the layout of an internal
    * node, rather than the leaf layout of a fresh tree. */
   bptr->height = lay_cnt - 1;
   node = bptr_node_new(bptr, 0);
   if (node == NULL)
    {
      status = TEMP_FULL_E_ALLOC;
      goto GEN_ERR_FREE;
    }
   bptr->node_cnt = 1;
   bptr->root_idx = node->node_idx;
   status = _level_chain_push(bptr, prev_at_level, node);
   if (status == TEMP_FULL_OK)
      status = _node_fill(bptr, prev_at_level, node, &st_it, interval, &lmk,
                           shape == NULL ? NULL : shape->root);
   bptr_node_unload(bptr, node);
   if (status != TEMP_FULL_OK) goto GEN_ERR_FREE;

   free(prev_at_level);
   if (bptr_unload(bptr))
    {
      /* the final flush failed: the image is incomplete, drop it */
      _gen_drop(path);
      return _gen_fail(TEMP_FULL_E_WRITE, path);
    }
   return TEMP_FULL_OK;

   /*-------------------------- Error Handling Zone --------------------------*/
GEN_ERR_FREE:
   free(prev_at_level);
GEN_ERR:
   bptr_unload(bptr);   /* best effort: flush what has been written ... */
   _gen_drop(path);     /* ... then drop the incomplete image */
   return _gen_fail(status, path);
}


const char *temp_full_strerror(int status)
{
   switch (status)
    {
   case TEMP_FULL_OK           : return "success";
   case TEMP_FULL_E_SHAPE      : return "node occupancy or path out of range";
   case TEMP_FULL_E_NAME       : return "template name or path is too long";
   case TEMP_FULL_E_LAY_CNT    : return "lay_cnt out of range";
   case TEMP_FULL_E_NODE_SIZE  : return "node_size out of range";
   case TEMP_FULL_E_INTERVAL   : return "interval is negative: a template's "
                                        "keys must ascend";
   case TEMP_FULL_E_PTR_OVERFLOW: return "template node pointers or file "
                                        "offsets would overflow";
   case TEMP_FULL_E_KV_OVERFLOW: return "key/value overflow: generated keys or "
                                        "doubled values exceed int64_t";
   case TEMP_FULL_E_FIXTURE    : return "not a complete template of this layout; "
                                        "delete it and retry";
   case TEMP_FULL_E_UNREADABLE : return "cannot be read";
   case TEMP_FULL_E_DIR        : return "the template directory cannot be "
                                        "created";
   case TEMP_FULL_E_INIT       : return "the template cannot be created";
   case TEMP_FULL_E_ALLOC      : return "out of memory";
   case TEMP_FULL_E_CHAIN      : return "the level list cannot be linked";
   case TEMP_FULL_E_WRITE      : return "the template cannot be written";
   default                     : return "unknown status";
    }
}


int cmp_i64(const void *lhs, const void *rhs)
{
   int64_t left = *(const int64_t *)lhs, right = *(const int64_t *)rhs;
   return left < right ? -1 : left > right ? 1 : 0;
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
/**
 * The perfect tree has 1 + fanout + ... + fanout^(lay_cnt - 1) nodes.
 * Horner's recurrence avoids computing a power or overflowing a sum: test the
 * next `count * fanout + 1' against the inclusive limit before either operation.
 * Callers supply a positive height and fanout, as enforced by layout validation.
 */
static _Bool _gen_node_count_fits(unsigned int lay_cnt, uint64_t fanout,
                                 uint64_t limit)
{
   uint64_t count = 1;

   if (limit == 0) return 0;
   for (unsigned int level = 1; level < lay_cnt; level++)
    {
      if (count > (limit - 1u) / fanout) return 0;
      count = count * fanout + 1u;
    }
   return 1;
}


/**
 * Validate only the nodes and bytes this generation will write.  Node indices
 * start at 1 (block 0 is the header), so the largest child/parent/sibling pointer
 * and the header node count are both the total node count.  A maximum pointer
 * is valid; no space is reserved for a later split or insertion.
 *
 * File I/O uses signed bptr_off_t positions.  The final file position includes
 * the header block and must fit too: `(node_count + 1) * node_size <= off_max'.
 * Divide first to derive the node limit without overflowing either operation.
 * Limit to uint64_t even if a platform has wider offsets, since the library's
 * node-index multiplication must also fit that unsigned type.
 */
static int _gen_check_pointers(unsigned int lay_cnt, _Bool is_lite,
                               uint32_t node_size)
{
   uint64_t ptr_size = is_lite ? BPTR_LITE_PTR_BYTE : BPTR_NORM_PTR_BYTE,
            fanout = ((uint64_t)node_size - BPTR_NODE_METADATA_BYTE - ptr_size) /
                     (sizeof (int64_t) + ptr_size) + 1u,
            ptr_max = is_lite ? UINT32_MAX : UINT64_MAX,
            off_max = UINT64_MAX,
            block_limit;

   if (!_gen_node_count_fits(lay_cnt, fanout, ptr_max))
    {
      (void)_gen_fail(TEMP_FULL_E_PTR_OVERFLOW, NULL);
      fprintf(stderr, "temp_full_generate: node count exceeds the %u-bit "
                      "node-index pointer maximum\n", (unsigned int)ptr_size * 8u);
      return TEMP_FULL_E_PTR_OVERFLOW;
    }

   /* A shift by the width of uint64_t is avoided, including on platforms with
    * offsets wider than 64 bits.  bptr_off_t is signed on every supported port. */
   if (sizeof (bptr_off_t) * CHAR_BIT <= 64u)
      off_max >>= 64u - (sizeof (bptr_off_t) * CHAR_BIT - 1u);
   block_limit = off_max / node_size;
   if (block_limit == 0 ||
       !_gen_node_count_fits(lay_cnt, fanout, block_limit - 1u))
    {
      (void)_gen_fail(TEMP_FULL_E_PTR_OVERFLOW, NULL);
      fprintf(stderr, "temp_full_generate: template file size, including its "
                      "header block, exceeds the file-offset maximum %" PRIu64
                      " bytes\n", off_max);
      return TEMP_FULL_E_PTR_OVERFLOW;
    }
   return TEMP_FULL_OK;
}


/**
 * @brief   Create every parent directory of @p path
 *
 * @param[in,out] path  path to create the parents of; modified in place while it
 *                      is walked and restored before returning, also when it
 *                      fails, so that the caller can still report the path
 * @param[in]     mode  permission bits of a directory that is created
 *
 * @return  a non-negative count of the path separators walked on success; -1 on
 *          failure, with `errno' set
 *
 * @note    A leading '/' is the root directory, which always exists: it is
 *          neither created nor checked, so that an absolute @p path -- as
 *          `--dir /tmp/...' hands over -- is served like a relative one.
 */
long long _ensure_par_dirs(char *path, mode_t mode)
{
   long long cnt = 0;

   for (char *p = path; *p; p++, cnt++)
    {
      struct stat st;
      if (*p != '/') continue;
      if (p == path) continue;

      *p = '\0';
      if (mkdir(path, mode) == -1 && errno != EEXIST)
       { *p = '/'; return -1; }

      if (stat(path, &st) == -1 || !S_ISDIR(st.st_mode))
       {
         if (errno == 0) errno = ENOTDIR;
         *p = '/';
         return -1;
       }

      *p = '/';
    }

   return cnt;
}


static void _node_fill_leaf(struct bptr *self, struct bptr_node *node,
                            int64_t *st, int64_t interval, uint32_t count)
{
   for (uint32_t k_mx = count;
        node->key_count < k_mx; node->key_count++)
    {
      /* Advance only when another key is actually needed.  The unused step
       * after the final key need not fit, even for a valid template. */
      if (self->record_cnt != 0 || node->key_count != 0) *st += interval;
      ((int64_t*)node->keys)[node->key_count] = *st;
      ((int64_t*)node->vals)[node->key_count] = *st * 2;
    }
   self->record_cnt += node->key_count;
}


/**
 * @brief   Check whether an existing file is the template that was asked for
 *
 * The header is read directly (see `core/docs/header_bin_layout.md'): a
 * partial image left by an interrupted run still carries the header written
 * when the file was created, whence a height of 0 and no node count.  The
 * checks prove that the file is consistent with the request, not that this
 * generator wrote it: `st' and `interval' live only in the file name.
 *
 * @param[in] path       file to inspect
 * @param[in] lay_cnt    number of levels the caller asked for
 * @param[in] is_lite    child pointer layout the caller asked for
 * @param[in] node_size  node size the caller asked for
 *
 * @return  TEMP_FULL_OK if @p path is a complete template of that layout,
 *          TEMP_FULL_E_FIXTURE if it is not, and TEMP_FULL_E_UNREADABLE if it
 *          cannot be read.
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
   if (file == NULL) return TEMP_FULL_E_UNREADABLE;
   /* measure through the open handle: the file must not change between the
    * size and the content check */
   if (fstat(fileno(file), &fst) == -1)
    { fclose(file); return TEMP_FULL_E_UNREADABLE; }
   rd = fread(hdr, 1, sizeof hdr, file);
   fclose(file);
   if (rd < sizeof hdr) return TEMP_FULL_E_FIXTURE;

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
      return TEMP_FULL_E_FIXTURE;

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
   if (node_cnt == 0) return TEMP_FULL_E_FIXTURE;

   /* unsigned and overflow free: `node_cnt' is read from the file, and the
    * block count cannot exceed the size of the file */
   blocks = (uint_fast64_t)fst.st_size / (uint_fast64_t)node_size;
   if ((uint_fast64_t)fst.st_size % (uint_fast64_t)node_size)
      return TEMP_FULL_E_FIXTURE;
   return blocks == node_cnt + 1 ? TEMP_FULL_OK : TEMP_FULL_E_FIXTURE;
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
 * @return  TEMP_FULL_OK on success; TEMP_FULL_E_CHAIN when the left sibling
 *          cannot be fetched
 */
static int _level_chain_push(struct bptr *self, bptr_node_t *prev_at_level,
                             struct bptr_node *node)
{
   struct bptr_node *prev_n;

   node->next = 0;
   node->prev = prev_at_level[node->level];
   if (prev_at_level[node->level] == 0)
    {
      prev_at_level[node->level] = node->node_idx;
      return TEMP_FULL_OK;
    }

   prev_n = bptr_node_fetch(self, prev_at_level[node->level]);
   if (prev_n == NULL) return TEMP_FULL_E_CHAIN;
   prev_n->next = node->node_idx;
   prev_n->is_dirty = 1;
   bptr_node_unload(self, prev_n);

   prev_at_level[node->level] = node->node_idx;
   return TEMP_FULL_OK;
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
 * @return  TEMP_FULL_OK on success; the status of the @c create_child call that
 *          failed otherwise
 */
static int _node_fill(struct bptr *self, bptr_node_t *prev_at_level,
                      struct bptr_node *node, int64_t *st, int64_t interval,
                      int64_t *lmk, const struct temp_shape_node *shape)
{
   struct bptr_node *child;
   int64_t iter_lmk;
   int status;

   // base case
   if (node->is_leaf)
    {
      _node_fill_leaf(self, node, st, interval,
                      temp_shape_keys(shape, self->node_bound.leaf.up - 1u));
      *lmk = ((int64_t*)node->keys)[0];
      return TEMP_FULL_OK;
    }

   /* leftmost child; its leftmost key is also the one of `node' */
   status = create_child(self, prev_at_level, node, st, interval, lmk, &child,
                          temp_shape_child(shape, 0));
   if (status != TEMP_FULL_OK) return status;
   _node_brch_vals_set(self, node, 0, child->node_idx);
   bptr_node_unload(self, child);

   for (uint32_t count = temp_shape_keys(shape, self->node_bound.brch.up - 1u);
        node->key_count < count; node->key_count++)
    {
      status = create_child(self, prev_at_level, node, st, interval,
                            &iter_lmk, &child,
                            temp_shape_child(shape, node->key_count + 1u));
      if (status != TEMP_FULL_OK) return status;
      /* the ith key of an internal node is the 0th key of the leftmost
       * descendant leaf of its (i + 1)th child */
      ((int64_t*)node->keys)[node->key_count] = iter_lmk;
      _node_brch_vals_set(self, node, node->key_count + 1, child->node_idx);
      bptr_node_unload(self, child);
    }

   return TEMP_FULL_OK;
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
 * @param[out]    out            the created, still loaded node on success
 *
 * @return  TEMP_FULL_OK on success; TEMP_FULL_E_ALLOC when the node cannot be
 *          created, and the status of the failed link or fill otherwise
 */
static int create_child(struct bptr *self, bptr_node_t *prev_at_level,
                        struct bptr_node *par_n, int64_t *st, int64_t interval,
                        int64_t *lmk, struct bptr_node **out,
                        const struct temp_shape_node *shape)
{
   struct bptr_node *node = bptr_node_new(self, par_n->node_idx);
   int status;

   if (node == NULL) return TEMP_FULL_E_ALLOC;
   self->node_cnt++;

   status = _level_chain_push(self, prev_at_level, node);
   if (status == TEMP_FULL_OK)
      status = _node_fill(self, prev_at_level, node, st, interval, lmk, shape);
   if (status != TEMP_FULL_OK)
    {
      /* the subtree is incomplete: the top level drops the whole image */
      bptr_node_unload(self, node);
      return status;
    }

   *out = node;
   return TEMP_FULL_OK;
}


/**
 * @brief   Drop the incomplete image a failed generation left behind
 *
 * @param[in] path  template to remove
 *
 * @note    A removal that fails is reported on stderr: the next call would
 *          otherwise report the leftover as TEMP_FULL_E_FIXTURE.
 */
static void _gen_drop(const char *path)
{
   if (remove(path) && errno != ENOENT)
      fprintf(stderr, "temp_full_generate: %s: cannot be removed\n", path);
}


/**
 * @brief   Report a failed generation on stderr and hand back its status
 *
 * @param[in] status  code to report and return
 * @param[in] path    template the call was working on, or NULL when the request
 *                    was refused before a path existed
 *
 * @return  @p status, so that a caller can `return _gen_fail(...)'
 */
static int _gen_fail(int status, const char *path)
{
   if (path == NULL)
      fprintf(stderr, "temp_full_generate: %s\n", temp_full_strerror(status));
   else
      fprintf(stderr, "temp_full_generate: %s: %s\n", path,
              temp_full_strerror(status));

   return status;
}


/**
 * @brief   Report a negative interval and the ascending request that replaces it
 *
 * The keys of a template have to ascend -- the split of a full node and the
 * verifier both search a node in the comparator's order -- so a descending
 * lattice is refused.  The refusal is named by `temp_full_strerror', like every
 * other failure, and is followed by the request that builds the same keys:
 * negate the interval and start at the last key of the descending lattice,
 * `st + interval * (record_cnt - 1)'.
 *
 * The numbers are printed only when they can be worked out: a layout that holds
 * no tree, or an `int64_t' overflow, leaves the recipe symbolic rather than
 * suggesting a start key that would be wrong.
 *
 * @param[in] lay_cnt    levels of the refused request
 * @param[in] st         first key of the refused request
 * @param[in] interval   its interval; negative, and never 0
 * @param[in] is_lite    child pointer layout of the refused request
 * @param[in] node_size  node size of the refused request
 */
static void _gen_report_interval(unsigned int lay_cnt, int64_t st,
                                 int64_t interval, _Bool is_lite,
                                 uint32_t node_size)
{
   int64_t asc_st, asc_interval;

   (void)_gen_fail(TEMP_FULL_E_INTERVAL, NULL);
   if (_gen_ascending(lay_cnt, st, interval, is_lite, node_size, &asc_st,
                      &asc_interval))
      fprintf(stderr, "temp_full_generate: interval %" PRIi64 " descends; "
                      "retry with interval %" PRIi64 " and st %" PRIi64
                      " (interval * -1, from the last key of that lattice)\n",
              interval, asc_interval, asc_st);
   else
      fprintf(stderr, "temp_full_generate: interval %" PRIi64 " descends; "
                      "retry with interval * -1 and the last key of that "
                      "lattice as st (st + interval * (record_cnt - 1))\n",
              interval);
}


/**
 * @brief   The ascending request that builds the same keys as a descending one
 *
 * A lattice laid down by a negative @p interval holds `record_cnt' keys,
 * `st + interval * k' for k in 0 .. record_cnt - 1; read the other way round it
 * is the same key set ascending, starting at the last of them and stepping by
 * `-interval', which is what a request the generator serves looks like.
 *
 * @param[in]  lay_cnt        levels of the refused request
 * @param[in]  st             first key of the refused request
 * @param[in]  interval       its interval; negative, and never 0
 * @param[in]  is_lite        child pointer layout of the refused request
 * @param[in]  node_size      node size of the refused request
 * @param[out] asc_st         start key of the ascending request
 * @param[out] asc_interval   its interval, `-interval'
 *
 * @return  1 when both outputs were computed, 0 when the layout holds no tree or
 *          the arithmetic leaves `int64_t' (the caller then prints the recipe
 *          without numbers)
 */
static _Bool _gen_ascending(unsigned int lay_cnt, int64_t st, int64_t interval,
                            _Bool is_lite, uint32_t node_size,
                            int64_t *asc_st, int64_t *asc_interval)
{
   int64_t rec_cnt;
   uint64_t step, keys_below, mag;

   if (!_gen_record_cnt(lay_cnt, is_lite, node_size, &rec_cnt)) return 0;

   /* `|interval|', spelled so that negating INT64_MIN cannot overflow */
   mag = (uint64_t)(-(interval + 1)) + 1u;
   if (mag > (uint64_t)INT64_MAX) return 0;   /* `-interval' is not an int64_t */
   /* the last key is `st' minus one step per key below it */
   keys_below = (uint64_t)rec_cnt - 1u;
   if (keys_below != 0 && mag > UINT64_MAX / keys_below) return 0;
   step = mag * keys_below;
   /* `st' is at most `(uint64_t)st + 2^63' steps away from INT64_MIN, so a longer
    * drop would leave the range of the start key */
   if (step > (uint64_t)st + ((uint64_t)1 << 63)) return 0;

   *asc_interval = (int64_t)mag;
   *asc_st = (int64_t)((uint64_t)st - step);
   return 1;
}


/**
 * @brief   Number of records a perfectly full template of a layout holds
 *
 * Mirrors the capacity model `bptr_init' derives (`_bptr_bound_set' in
 * `core/src/bptr_core.c'): a leaf holds `rem_sz / (key_size + value_size)' keys
 * and an internal node `(rem_sz - ptr) / (key_size + ptr) + 1' children, and a
 * tree of @p lay_cnt levels is `brch.up^(lay_cnt - 1)' leaves of them.  The
 * generator writes 8-byte keys and values only, so those sizes are fixed here.
 *
 * @param[in]  lay_cnt    number of levels
 * @param[in]  is_lite    4-byte child pointers, 8-byte otherwise
 * @param[in]  node_size  size of a node in bytes
 * @param[out] rec_cnt    record count of that layout
 *
 * @return  1 when @p rec_cnt was computed and fits an `int64_t', 0 when the
 *          layout holds no tree or the product leaves the range
 */
static _Bool _gen_record_cnt(unsigned int lay_cnt, _Bool is_lite,
                             uint32_t node_size, int64_t *rec_cnt)
{
   uint64_t rem_sz = (uint64_t)node_size - BPTR_NODE_METADATA_BYTE,
            ptr_size = is_lite ? BPTR_LITE_PTR_BYTE : BPTR_NORM_PTR_BYTE,
            leaf_keys = rem_sz / (2u * (uint64_t)sizeof (int64_t)),
            brch_up = (rem_sz - ptr_size) /
                      ((uint64_t)sizeof (int64_t) + ptr_size) + 1u,
            cnt = leaf_keys;

   /* a layout `bptr_init' would refuse has no record count to speak of */
   if (leaf_keys == 0 || brch_up < 3) return 0;
   for (unsigned int level = 1; level < lay_cnt; level++)
    {
      if (cnt > UINT64_MAX / brch_up) return 0;
      cnt *= brch_up;
    }
   if (cnt > (uint64_t)INT64_MAX) return 0;

   *rec_cnt = (int64_t)cnt;
   return 1;
}


/**
 * @brief   Check the largest generated key and both value range endpoints
 *
 * Called only after layout validation and the negative-interval refusal.  The
 * layout arithmetic mirrors `_gen_record_cnt', but bounds the count by the
 * number of records the requested lattice can hold, rather than multiplying
 * an arbitrarily tall tree first.  Every product is checked by division before
 * it is evaluated, so the preflight itself cannot overflow.
 *
 * Values are twice their keys.  With a nonnegative interval, the first key
 * determines the minimum value and the last key the maximum value.  The
 * allowed key range is [INT64_MIN / 2, INT64_MAX / 2], inclusive.  A zero
 * interval repeats one key, so its key/value range is independent of the
 * record count (node/pointer limits are a separate concern).
 */
static _Bool _gen_kv_fits(unsigned int lay_cnt, int64_t st, int64_t interval,
                          _Bool is_lite, uint32_t node_size)
{
   uint64_t rem_sz, ptr_size, brch_up, rec_cnt, max_delta, max_records;
   int64_t largest;

   if (st < INT64_MIN / 2 || st > INT64_MAX / 2) return 0;
   if (interval == 0) return 1;

   /* The distance between the allowed key endpoints is INT64_MAX.  Unsigned
    * subtraction also handles a negative start without signed overflow. */
   max_delta = (uint64_t)(INT64_MAX / 2) - (uint64_t)st;
   max_records = max_delta / (uint64_t)interval + 1u;
   rem_sz = (uint64_t)node_size - BPTR_NODE_METADATA_BYTE;
   ptr_size = is_lite ? BPTR_LITE_PTR_BYTE : BPTR_NORM_PTR_BYTE;
   brch_up = (rem_sz - ptr_size) /
             ((uint64_t)sizeof (int64_t) + ptr_size) + 1u;
   rec_cnt = rem_sz / (2u * (uint64_t)sizeof (int64_t));
   if (rec_cnt > max_records) return 0;
   for (unsigned int level = 1; level < lay_cnt; level++)
    {
      if (rec_cnt > max_records / brch_up) return 0;
      rec_cnt *= brch_up;
    }

   /* The bounded count proves this product <= max_delta <= INT64_MAX,
    * and adding it to st stays inside the allowed key range. */
   largest = st + (int64_t)((rec_cnt - 1u) * (uint64_t)interval);
   return largest <= INT64_MAX / 2;
}

/* Shape totals are computed before any filesystem changes. The range of keys
 * whose doubled values fit is inclusive, and division avoids count products. */
static _Bool _gen_shape_kv_fits(uint64_t records, int64_t st, int64_t interval)
{
   if (st < INT64_MIN / 2 || st > INT64_MAX / 2 || records == 0) return 0;
   if (interval == 0) return 1;
   uint64_t distance = (uint64_t)(INT64_MAX / 2) - (uint64_t)st;
   return records - 1u <= distance / (uint64_t)interval;
}
static int _gen_shape_check_pointers(uint64_t nodes, _Bool is_lite,
                                      uint32_t node_size)
{
   uint64_t off_max = UINT64_MAX;
   if (nodes > (is_lite ? UINT32_MAX : UINT64_MAX))
      return _gen_fail(TEMP_FULL_E_PTR_OVERFLOW, NULL);
   if (sizeof (bptr_off_t) * CHAR_BIT <= 64u)
      off_max >>= 64u - (sizeof (bptr_off_t) * CHAR_BIT - 1u);
   uint64_t blocks = off_max / node_size;
   if (blocks == 0 || nodes > blocks - 1u)
      return _gen_fail(TEMP_FULL_E_PTR_OVERFLOW, NULL);
   return TEMP_FULL_OK;
}
static void _gen_shape_report_interval(uint64_t records, int64_t st,
                                        int64_t interval)
{
   uint64_t mag = (uint64_t)(-(interval + 1)) + 1u;
   (void)_gen_fail(TEMP_FULL_E_INTERVAL, NULL);
   if (records != 0 && mag <= INT64_MAX &&
       records - 1u <= UINT64_MAX / mag &&
       mag * (records - 1u) <= (uint64_t)st + (UINT64_C(1) << 63))
      fprintf(stderr, "temp_full_generate: interval %" PRIi64 " descends; "
                      "retry with interval %" PRIi64 " and st %" PRIi64
                      " (interval * -1, from the last key of that lattice)\n",
              interval, (int64_t)mag,
              (int64_t)((uint64_t)st - mag * (records - 1u)));
   else
      fprintf(stderr, "temp_full_generate: interval %" PRIi64 " descends; "
                      "retry with interval * -1 and the last key of that "
                      "lattice as st (st + interval * (record_cnt - 1))\n", interval);
}

struct fixture_reader {
   FILE *file;
   uint32_t node_size, ptr_size, leaf_keys, branch_keys;
   uint64_t nodes, records, seen_nodes, seen_records;
   uint64_t *previous, *next;
   int64_t cursor, interval;
};
static uint64_t fixture_pointer(const unsigned char *p, uint32_t width)
{
   uint64_t value = 0;
   if (width == 4) { uint32_t short_value; memcpy(&short_value, p, 4); value = short_value; }
   else memcpy(&value, p, 8);
   return value;
}
static int fixture_read(struct fixture_reader *r, uint64_t node, uint64_t within,
                          void *buf, size_t size)
{
   if (node > r->nodes || within > r->node_size || size > r->node_size - within)
      return TEMP_FULL_E_FIXTURE;
   uint64_t position = node * (uint64_t)r->node_size + within;
   if (fseeko(r->file, (bptr_off_t)position, SEEK_SET) != 0)
      return TEMP_FULL_E_UNREADABLE;
   if (fread(buf, 1, size, r->file) != size)
      return ferror(r->file) ? TEMP_FULL_E_UNREADABLE : TEMP_FULL_E_FIXTURE;
   return TEMP_FULL_OK;
}
static int fixture_node(struct fixture_reader *r, uint64_t index, uint64_t parent,
                          unsigned int level, const struct temp_shape_node *shape,
                          int64_t *leftmost)
{
   unsigned char metadata[BPTR_NODE_METADATA_BYTE], pointer[8];
   uint16_t flags, stored_level;
   uint32_t keys;
   int status = fixture_read(r, index, 0, metadata, sizeof metadata);
   if (status != TEMP_FULL_OK) return status;
   memcpy(&flags, metadata, 2);
   memcpy(&stored_level, metadata + 2, 2);
   memcpy(&keys, metadata + 4, 4);
   uint32_t requested = temp_shape_keys(shape, level == 0 ? r->leaf_keys : r->branch_keys);
   uint64_t prev = fixture_pointer(metadata + 12 + 2u * r->ptr_size, r->ptr_size);
   uint64_t next = fixture_pointer(metadata + 12 + r->ptr_size, r->ptr_size);
   if (index == 0 || ++r->seen_nodes > r->nodes ||
       flags != (uint16_t)(BPTR_NODE_FLAG_VALID | (level == 0 ? BPTR_NODE_FLAG_LEAF : 0)) ||
       stored_level != level || keys != requested ||
       fixture_pointer(metadata + 12, r->ptr_size) != parent ||
       prev != r->previous[level] ||
       (r->previous[level] != 0 && r->next[level] != index)) return TEMP_FULL_E_FIXTURE;
   r->previous[level] = index; r->next[level] = next;
   if (level == 0) {
      for (uint32_t i = 0; i < keys; i++) {
         int64_t key, value;
         if (r->seen_records != 0) r->cursor += r->interval;
         status = fixture_read(r, index, BPTR_NODE_METADATA_BYTE + (uint64_t)i * 8u, &key, 8);
         if (status != TEMP_FULL_OK) return status;
         status = fixture_read(r, index, BPTR_NODE_METADATA_BYTE + (uint64_t)keys * 8u + (uint64_t)i * 8u, &value, 8);
         if (status != TEMP_FULL_OK) return status;
         if (key != r->cursor || value != key * 2 || ++r->seen_records > r->records)
            return TEMP_FULL_E_FIXTURE;
         if (i == 0) *leftmost = key;
      }
      return TEMP_FULL_OK;
   }
   for (uint32_t i = 0; i <= keys; i++) {
      int64_t child_leftmost, separator;
      status = fixture_read(r, index, BPTR_NODE_METADATA_BYTE + (uint64_t)keys * 8u +
                             (uint64_t)i * r->ptr_size, pointer, r->ptr_size);
      if (status != TEMP_FULL_OK) return status;
      uint64_t child = fixture_pointer(pointer, r->ptr_size);
      status = fixture_node(r, child, index, level - 1u, temp_shape_child(shape, i), &child_leftmost);
      if (status != TEMP_FULL_OK) return status;
      if (i == 0) *leftmost = child_leftmost;
      else {
         status = fixture_read(r, index, BPTR_NODE_METADATA_BYTE + (uint64_t)(i - 1u) * 8u, &separator, 8);
         if (status != TEMP_FULL_OK) return status;
         if (separator != child_leftmost) return TEMP_FULL_E_FIXTURE;
      }
   }
   return TEMP_FULL_OK;
}
static int _shape_fixture_matches(const char *path, unsigned int levels,
                                   int64_t st, int64_t interval, _Bool is_lite,
                                   uint32_t node_size, const struct temp_shape *shape,
                                   const struct temp_shape_counts *counts)
{
   struct fixture_reader reader = {0};
   unsigned char header[64];
   uint64_t records, nodes, root;
   int64_t leftmost;
   int status;
   reader.file = fopen(path, "rb");
   if (reader.file == NULL) return TEMP_FULL_E_UNREADABLE;
   reader.ptr_size = is_lite ? 4u : 8u;
   reader.node_size = node_size;
   reader.nodes = counts->nodes;
   reader.records = counts->records;
   reader.cursor = st; reader.interval = interval;
   reader.leaf_keys = (node_size - BPTR_NODE_METADATA_BYTE) / 16u;
   reader.branch_keys = (node_size - BPTR_NODE_METADATA_BYTE - reader.ptr_size) / (8u + reader.ptr_size);
   status = fixture_read(&reader, 0, 0, header, sizeof header);
   if (status != TEMP_FULL_OK) goto finish;
   memcpy(&records, header + 16, 8);
   root = fixture_pointer(header + 28, reader.ptr_size);
   nodes = fixture_pointer(header + 28 + 3u * reader.ptr_size, reader.ptr_size);
   if (records != counts->records || nodes != counts->nodes ||
       fixture_pointer(header + 28 + reader.ptr_size, reader.ptr_size) != 0 ||
       fixture_pointer(header + 28 + 2u * reader.ptr_size, reader.ptr_size) != 0) {
      status = TEMP_FULL_E_FIXTURE; goto finish;
   }
   reader.previous = calloc(levels, sizeof *reader.previous);
   reader.next = calloc(levels, sizeof *reader.next);
   if (reader.previous == NULL || reader.next == NULL) { status = TEMP_FULL_E_ALLOC; goto finish; }
   status = fixture_node(&reader, root, 0, levels - 1u, shape->root, &leftmost);
   if (status == TEMP_FULL_OK && (reader.seen_nodes != counts->nodes || reader.seen_records != counts->records))
      status = TEMP_FULL_E_FIXTURE;
   for (unsigned int i = 0; status == TEMP_FULL_OK && i < levels; i++)
      if (reader.next[i] != 0) status = TEMP_FULL_E_FIXTURE;
finish:
   free(reader.previous); free(reader.next);
   fclose(reader.file);
   return status;
}

/*-------------------------- Private Functions END ---------------------------*/
