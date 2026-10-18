/*----------------------------- Private Includes -----------------------------*/
#include "search.h"
#include "tools.h"
#include "progress.h"
#include "unity.h"
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/*--------------------------- Private Includes END ---------------------------*/

const struct template *search_template;
struct search_state search_state;

static void _path_prepare(void);
static void _verify(void);
static void _catalog(void);
static void _remove(void);

/*----------------------------- Public Functions -----------------------------*/
void search_require(_Bool ok, const char *fmt, ...)
{
   char message[PATH_MAX + 512];
   va_list args;

   if (ok) return;
   va_start(args, fmt);
   vsnprintf(message, sizeof message, fmt, args);
   va_end(args);
   progress_break();
   TEST_FAIL_MESSAGE(message);
}


void search_close(void)
{
   struct search_state *s = &search_state;
   int status;

   if (s->tree == NULL) return;
   for (size_t i = 0; i < sizeof s->refs / sizeof *s->refs; i++)
      if (s->refs[i] != NULL)
       {
         bptr_node_unload(s->tree, s->refs[i]);
         s->refs[i] = NULL;
       }
   status = bptr_unload(s->tree);
   if (status == 0 || bptr_errno == BPTR_E_FCLOSE) s->tree = NULL;
   search_require(status == 0, "%s: bptr_unload failed: %d", s->path, status);
}


void search_reopen(void)
{
   struct search_state *s = &search_state;

   search_close();
   bptr_errno = 0;
   s->tree = bptr_load(s->path, BPTR_CACHE_CAPACITY_MIN, cmp_i64);
   search_require(s->tree != NULL, "%s: bptr_load failed: %d", s->path,
                  bptr_errno);
}


void search_prepare(void)
{
   struct search_state *s = &search_state;
   char reason[256];
   int status;

   _path_prepare();
   s->tmpl = search_template;
   progress_update("    prepare %s", s->tmpl->path);
   status = tools_instantiate(s->tmpl->path, s->path);
   tools_strstatus(reason, sizeof reason, TOOLS_TEMP_INST, status);
   search_require(status == 0, "%s: failed to copy to %s: %s", s->tmpl->path,
                  s->path, reason);
   /* Verification also unloads/writes its input: guard the COPY, not source. */
   _verify();
   search_reopen();
   search_require(s->tree->key_size == sizeof(int64_t) &&
                  s->tree->value_size == sizeof(int64_t),
                  "%s: search requires int64_t keys and values", s->tmpl->path);
   search_require(s->tree->height == s->tmpl->lay_cnt && s->tmpl->interval > 0,
                  "%s: invalid template height or interval", s->tmpl->path);
   s->record_count = s->tree->record_cnt;
   _catalog();
   /* Oracle traversal must not warm the cache whose behavior we test. */
   search_reopen();
}


void search_empty_prepare(_Bool lite)
{
   struct search_state *s = &search_state;

   _path_prepare();
   bptr_errno = 0;
   s->tree = bptr_init(s->path, lite, 128, 8, 8, BPTR_CACHE_CAPACITY_MIN,
                       cmp_i64);
   search_require(s->tree != NULL, "%s: empty bptr_init failed: %d", s->path,
                  bptr_errno);
}


void search_finish(void)
{
   search_close();
   _verify();
   _remove();
}


void search_discard(void)
{
   search_close();
   _remove();
}


/* Release references even when an assertion interrupted a query. Retain the
 * image on failure; removing it requires successful explicit finish/discard. */
void search_cleanup(void)
{
   struct search_state *s = &search_state;
   int status = 0;

   progress_break();
   if (s->tree != NULL)
    {
      for (size_t i = 0; i < sizeof s->refs / sizeof *s->refs; i++)
         if (s->refs[i] != NULL) bptr_node_unload(s->tree, s->refs[i]);
      status = bptr_unload(s->tree);
    }
   if (s->path[0] && access(s->path, F_OK) == 0)
      fprintf(stderr, "search: retained failed instance %s\n", s->path);
   else if (s->dir[0])
      rmdir(s->dir);
   free(s->leaves);
   memset(s, 0, sizeof *s);
   search_require(status == 0, "search: teardown bptr_unload failed: %d", status);
}


int64_t search_key(uint64_t offset)
{
   const struct template *tmpl = search_state.tmpl;
   __int128 key = (__int128)tmpl->st + (__int128)offset * tmpl->interval;

   search_require(key >= INT64_MIN && key <= INT64_MAX,
                  "%s: key arithmetic overflow at record %" PRIu64,
                  tmpl->path, offset);
   return (int64_t)key;
}


void search_check(int64_t key, size_t leaf, bptr_node_ki_t idx, _Bool found)
{
   struct search_state *s = &search_state;
   struct bptr_find_res result;
   const struct search_leaf *expected = &s->leaves[leaf];
   int64_t actual_key, actual_value;

   search_require(s->refs[0] == NULL, "query result reference was not released");
   bptr_errno = 0;
   result = bptr_find_node(s->tree, &key);
   s->refs[0] = result.node;
   search_require(result.node != NULL, "%s: query %" PRIi64
                  ": expected leaf %" PRIuFAST64 " index %" PRIu32
                  " found %d; actual leaf NULL errno %d", s->tmpl->path, key,
                  expected->node_idx, idx, found, bptr_errno);
   search_require(result.node->is_leaf &&
                  result.node->node_idx == expected->node_idx &&
                  result.node->key_count == expected->count &&
                  result.idx == idx && result.is_found == found,
                  "%s: query %" PRIi64 ": expected leaf %" PRIuFAST64
                  " index %" PRIu32 " found %d; actual leaf %" PRIuFAST64
                  " index %" PRIu32 " found %d errno %d", s->tmpl->path, key,
                  expected->node_idx, idx, found, result.node->node_idx,
                  result.idx, result.is_found, bptr_errno);
   if (found)
    {
      __int128 value = (__int128)key * 2;
      search_require(idx < result.node->key_count &&
                     value >= INT64_MIN && value <= INT64_MAX,
                     "%s: query %" PRIi64 ": invalid matching slot/value",
                     s->tmpl->path, key);
      memcpy(&actual_key, result.node->keys + idx * sizeof actual_key,
              sizeof actual_key);
      memcpy(&actual_value, (char *)result.node->vals + idx * sizeof actual_value,
              sizeof actual_value);
      search_require(actual_key == key && actual_value == (int64_t)value,
                     "%s: query %" PRIi64 ": expected key/value %" PRIi64
                     "/%" PRIi64 "; actual %" PRIi64 "/%" PRIi64,
                     s->tmpl->path, key, key, (int64_t)value, actual_key,
                     actual_value);
      s->hits++;
    }
   else
      s->misses++;
   bptr_node_unload(s->tree, s->refs[0]);
   s->refs[0] = NULL;
}


void test_search_hits(void)
{
   struct search_state *s = &search_state;
   uint64_t queries;

   search_prepare();
   for (size_t l = 0; l < s->leaf_count; l++)
    {
      const struct search_leaf *leaf = &s->leaves[l];
      progress_update("    hits %s: leaf %zu/%zu", s->tmpl->path, l + 1,
                       s->leaf_count);
      for (uint32_t i = 0; i < leaf->count; i++)
         search_check(search_key(leaf->offset + i), l, i, 1);
    }
   queries = s->hits;
   search_require(queries == s->record_count, "%s: incomplete hit coverage",
                  s->tmpl->path);
   search_finish();
   progress_done("    hits %s: PASS (%" PRIu64 " queries)",
                  search_template->path, queries);
}


void test_search_misses(void)
{
   struct search_state *s = &search_state;
   int64_t first, last;
   size_t final_leaf;
   uint32_t final_count;
   uint64_t queries;

   search_prepare();
   first = search_key(0);
   final_leaf = s->leaf_count - 1;
   final_count = s->leaves[final_leaf].count;
   last = search_key(s->record_count - 1);
   if (first > INT64_MIN)
    {
      search_check(INT64_MIN, 0, 0, 0);
      if (first - 1 != INT64_MIN) search_check(first - 1, 0, 0, 0);
    }
   for (size_t l = 0; l < s->leaf_count; l++)
    {
      const struct search_leaf *leaf = &s->leaves[l];
      progress_update("    misses %s: leaf %zu/%zu", s->tmpl->path, l + 1,
                       s->leaf_count);
      for (uint32_t i = 0; i < leaf->count; i++)
       {
         uint64_t ordinal = leaf->offset + i;
         int64_t a, b;
         if (ordinal == s->record_count - 1) continue;
         a = search_key(ordinal);
         b = search_key(ordinal + 1);
         if ((__int128)b - a <= 1) continue;
         /* This remains THIS leaf at its end, even across a leaf boundary. */
         search_check(a + 1, l, i + 1, 0);
         if (a + 1 != b - 1) search_check(b - 1, l, i + 1, 0);
       }
    }
   if (last < INT64_MAX)
    {
      search_check(last + 1, final_leaf, final_count, 0);
      if (last + 1 != INT64_MAX)
         search_check(INT64_MAX, final_leaf, final_count, 0);
    }
   queries = s->misses;
   search_finish();
   progress_done("    misses %s: PASS (%" PRIu64 " queries)",
                  search_template->path, queries);
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static void _path_prepare(void)
{
   struct search_state *s = &search_state;
   const char *dirs[] = {"bptr_files", "bptr_files/temp", "bptr_files/temp/search"};
   int needed;

   search_require(s->dir[0] == '\0', "previous search fixture is still active");
   for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++)
    {
      int status = mkdir(dirs[i], 0755);
      search_require(status == 0 || errno == EEXIST, "cannot create %s: %s",
                     dirs[i], strerror(errno));
    }
   snprintf(s->dir, sizeof s->dir, "bptr_files/temp/search/run-XXXXXX");
   search_require(mkdtemp(s->dir) != NULL, "cannot create search directory: %s",
                  strerror(errno));
   needed = snprintf(s->path, sizeof s->path, "%s/instance.bptr", s->dir);
   search_require(needed >= 0 && (size_t)needed < sizeof s->path,
                  "search instance path is too long");
}


static void _verify(void)
{
   struct search_state *s = &search_state;
   char reason[256];
   int status = tools_verify(s->tmpl, 0, 0, 0, NULL, s->path);

   tools_strstatus(reason, sizeof reason, TOOLS_TEMP_VERIFY, status);
   search_require(status == 0, "%s: failed to verify %s: %s", s->tmpl->path,
                  s->path, reason);
}


/* Walk child zero and the leaf sibling chain only. No searched branch route,
 * key-search helper, or constant-capacity leaf arithmetic enters the oracle. */
static void _catalog(void)
{
   struct search_state *s = &search_state;
   bptr_node_t index = s->tree->root_idx, previous = 0;
   uint64_t total = 0;
   size_t capacity = 0;
   uint32_t depth = 0;

   search_require(index != 0 && s->record_count != 0,
                  "%s: expected a nonempty template", s->tmpl->path);
   for (;;)
    {
      bptr_node_t child;
      search_require(depth++ < s->tree->height,
                     "%s: child-zero path exceeds tree height", s->tmpl->path);
      bptr_errno = 0;
      s->refs[1] = bptr_node_fetch(s->tree, index);
      search_require(s->refs[1] != NULL, "%s: cannot read oracle node %"
                     PRIuFAST64 ": %d", s->tmpl->path, index, bptr_errno);
      if (s->refs[1]->is_leaf) break;
      child = _node_brch_vals_get(s->tree, s->refs[1], 0);
      bptr_node_unload(s->tree, s->refs[1]);
      s->refs[1] = NULL;
      index = child;
    }

   for (;;)
    {
      struct bptr_node *node = s->refs[1];
      struct search_leaf *leaf;
      bptr_node_t next = node->next;

      search_require(node->is_leaf && node->level == 0 && node->prev == previous &&
                     node->key_count > 0 && total <= s->record_count &&
                     node->key_count <= s->record_count - total &&
                     s->leaf_count < s->tree->node_cnt,
                     "%s: invalid oracle leaf chain at node %" PRIuFAST64,
                     s->tmpl->path, node->node_idx);
      if (s->leaf_count == capacity)
       {
         size_t grown = capacity == 0 ? 16 : capacity * 2;
         struct search_leaf *leaves;
         search_require(grown > capacity && grown <= SIZE_MAX / sizeof *leaves,
                        "%s: oracle catalog is too large", s->tmpl->path);
         leaves = realloc(s->leaves, grown * sizeof *leaves);
         search_require(leaves != NULL, "%s: oracle allocation failed",
                        s->tmpl->path);
         s->leaves = leaves;
         capacity = grown;
       }
      leaf = &s->leaves[s->leaf_count++];
      leaf->node_idx = node->node_idx;
      leaf->count = node->key_count;
      leaf->offset = total;
      memcpy(&leaf->first, node->keys, sizeof leaf->first);
      search_require(leaf->first == search_key(total),
                     "%s: unexpected first key in oracle leaf %" PRIuFAST64,
                     s->tmpl->path, node->node_idx);
      total += node->key_count;
      previous = node->node_idx;
      bptr_node_unload(s->tree, node);
      s->refs[1] = NULL;
      if (next == 0) break;
      bptr_errno = 0;
      s->refs[1] = bptr_node_fetch(s->tree, next);
      search_require(s->refs[1] != NULL, "%s: cannot read next oracle leaf %"
                     PRIuFAST64 ": %d", s->tmpl->path, next, bptr_errno);
    }
   search_require(total == s->record_count, "%s: incomplete oracle leaf chain",
                  s->tmpl->path);
}


static void _remove(void)
{
   struct search_state *s = &search_state;

   search_require(remove(s->path) == 0, "%s: remove failed: %s", s->path,
                  strerror(errno));
   search_require(rmdir(s->dir) == 0, "%s: rmdir failed: %s", s->dir,
                  strerror(errno));
   free(s->leaves);
   memset(s, 0, sizeof *s);
}
/*-------------------------- Private Functions END ---------------------------*/
