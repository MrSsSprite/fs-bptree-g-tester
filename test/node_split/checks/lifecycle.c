/* Link the real split case and sampler against instrumented tree/tool calls.
 * Each process exercises one scenario; expected injected Unity failures are
 * successful regression checks only when the state and call counts match. */
#include "../src/temp_split.h"
#include "../src/templates.h"
#include "../src/tools.h"
#include "../src/progress.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include "unity.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct template tmpl;
static struct bptr tree;
static struct bptr_node leaf;
static const char *scenario;
static _Bool tree_open, leaf_loaded, instance_exists, verified;
static unsigned int copies, unloads, verifies, removals;
int bptr_errno;

void setUp(void) { }
void tearDown(void)
{
   progress_break();
   temp_split_cleanup();
}

const struct template *templates_get(size_t *count)
{ *count = 1; return &tmpl; }

int cmp_i64(const void *lhs, const void *rhs)
{
   int64_t a = *(const int64_t*)lhs, b = *(const int64_t*)rhs;
   return (a > b) - (a < b);
}

void _bptr_path(char *dest, size_t size, const char *name)
{ snprintf(dest, size, "bptr_files/%s", name); }

int tools_instantiate(const char *src, const char *dst)
{
   (void)dst;
   assert(!strcmp(src, tmpl.path));
   /* The previous instance must be closed AND removed before another copy. */
   assert(!tree_open && !instance_exists);
   copies++;
   instance_exists = 1;
   verified = 0;
   return 0;
}

struct bptr *bptr_load(const char *path, uint64_t capacity,
                       int (*compare)(const void*, const void*))
{
   (void)capacity;
   (void)compare;
   assert(!tree_open);
   if (strcmp(path, tmpl.path)) assert(instance_exists);
   tree_open = 1;
   memset(&tree, 0, sizeof tree);
   tree.height = tmpl.lay_cnt;
   tree.root_idx = 1;
   tree.node_bound.leaf.up = 5;
   tree.node_bound.brch.up = 6;
   tree.record_cnt = 4;
   for (unsigned int i = 1; i < tmpl.lay_cnt; i++) tree.record_cnt *= 6;
   return &tree;
}

struct bptr_node *bptr_node_fetch(struct bptr *self, bptr_node_t index)
{
   assert(self == &tree && tree_open && index == 1);
   assert(!leaf_loaded);
   leaf_loaded = 1;
   memset(&leaf, 0, sizeof leaf);
   return &leaf;
}

void bptr_node_unload(struct bptr *self, struct bptr_node *node)
{
   assert(self == &tree && node == &leaf && leaf_loaded);
   leaf_loaded = 0;
}

bptr_node_t bptr_node_split(struct bptr *self, struct bptr_node *node,
                            const void *key, const void *val)
{
   (void)key;
   (void)val;
   assert(self == &tree && tree_open && node == &leaf && leaf_loaded);
   leaf.next = 2;
   tree.height++;
   tree.record_cnt++;
   return 2;
}

int bptr_unload(struct bptr *self)
{
   assert(self == &tree && tree_open && !leaf_loaded);
   if (instance_exists)
    {
      unloads++;
      if (!strcmp(scenario, "unload-failure") && copies == 3)
         return BPTR_E_FACCESS;
    }
   tree_open = 0;
   return BPTR_E_SUCCESS;
}

int tools_verify(unsigned int height, int64_t st, int64_t interval,
                  _Bool has_new_kv, int64_t key, int64_t val, const char *path)
{
   (void)path;
   assert(height == tmpl.lay_cnt && st == tmpl.st && interval == tmpl.interval);
   assert(has_new_kv && val == key * 2);
   assert(instance_exists && !tree_open && !leaf_loaded);
   assert(unloads == copies && verifies == copies - 1);
   /* The fake unload is the observable flush/close boundary. */
   assert(tree.height == tmpl.lay_cnt + 1);
   verifies++;
   if (!strcmp(scenario, "verify-failure") && copies == 3) return 1;
   verified = 1;
   return 0;
}

void tools_strstatus(char *buf, size_t size, const char *tool, int status)
{ snprintf(buf, size, "%s: injected status %d", tool, status); }

int remove(const char *path)
{
   (void)path;
   if (!instance_exists)
    { errno = ENOENT; return -1; }   /* pre-copy cleanup */
   assert(verified && !tree_open);
   if (!strcmp(scenario, "remove-failure") && copies == 3)
    { errno = EACCES; return -1; }
   removals++;
   instance_exists = 0;
   return 0;
}

int main(int argc, char **argv)
{
   int failures;
   unsigned int count;

   assert(argc == 3);
   scenario = argv[1];
   tmpl.lay_cnt = (unsigned int)strtoul(argv[2], NULL, 10);
   assert(tmpl.lay_cnt == 1 || tmpl.lay_cnt == 4);
   tmpl.st = -11;
   tmpl.interval = 5;
   snprintf(tmpl.path, sizeof tmpl.path, "fixture/%u--11-5.bptr", tmpl.lay_cnt);
   count = tmpl.lay_cnt == 1 ? 5 : 77;

   UNITY_BEGIN();
   RUN_TEST(test_full_split);
   failures = UNITY_END();

   if (!strcmp(scenario, "success"))
    {
      assert(failures == 0);
      assert(copies == count && unloads == count && verifies == count);
      assert(removals == count && !instance_exists && !tree_open);
    }
   else
    {
      assert(failures == 1 && copies == 3 && unloads == 3);
      assert(instance_exists && removals == 2);
      if (!strcmp(scenario, "unload-failure"))
         assert(verifies == 2 && tree_open);
      else
       {
         assert(verifies == 3 && !tree_open);
         if (!strcmp(scenario, "verify-failure")) assert(!verified);
         else
          {
            assert(!strcmp(scenario, "remove-failure"));
            assert(verified);
          }
       }
    }
   return 0;
}
