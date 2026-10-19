/* Link the real split case and sampler against instrumented tree/tool calls.
 * Each process exercises one scenario; expected injected Unity failures are
 * successful regression checks only when the state and call counts match. */
#include "../src/temp_split.h"
#include "../src/split_sampler.h"
#include "../src/templates.h"
#include "../src/tools.h"
#include "progress.h"
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
/* Aligned storage for the node's flexible keys, plus independent children. */
struct fake_node {
   union {
      max_align_t alignment;
      unsigned char bytes[sizeof(struct bptr_node) + 6 * sizeof(int64_t)];
   } data;
   BPTR_LITE_PTR_TYPE children[6];
   _Bool loaded;
};
static struct fake_node nodes[512];
static uint64_t original_leaves;
static unsigned int loaded_nodes;
static uint64_t next_record;
static unsigned int next_node;
static const char *scenario;
static _Bool tree_open, instance_exists, verified;
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

static struct bptr_node *fake_at(bptr_node_t index)
{
   assert(index > 0 && index < 512);
   return (struct bptr_node *)nodes[index].data.bytes;
}

static bptr_node_t make_node(unsigned int level, bptr_node_t parent,
                             const struct temp_shape_node *shape)
{
   unsigned int index = ++next_node;
   struct bptr_node *node = fake_at(index);
   node->node_idx = index;
   node->parent = parent;
   node->level = level;
   node->is_leaf = level == 0;
   node->key_count = temp_shape_keys(shape, level == 0 ? 4 : 5);
   if (level == 0)
    {
      next_record += node->key_count;
      original_leaves++;
    }
   else
    {
      node->vals = nodes[index].children;
      for (unsigned int child = 0; child <= node->key_count; child++)
       {
         if (child != 0)
            ((int64_t *)node->keys)[child - 1] = tmpl.st + next_record * tmpl.interval;
         nodes[index].children[child] = make_node(level - 1, index,
                                                  temp_shape_child(shape, child));
       }
    }
   return index;
}

struct bptr *bptr_load(const char *path, uint64_t capacity,
                       int (*compare)(const void*, const void*))
{
   (void)capacity;
   (void)compare;
   assert(!tree_open && loaded_nodes == 0);
   if (strcmp(path, tmpl.path)) assert(instance_exists);
   tree_open = 1;
   memset(&tree, 0, sizeof tree);
   memset(nodes, 0, sizeof nodes);
   next_node = 0;
   next_record = original_leaves = 0;
   tree.height = tmpl.lay_cnt;
   tree.node_size = 128;
   tree.is_lite = 1;
   tree.node_bound.leaf.up = 5;
   tree.node_bound.brch.up = 6;
   tree.root_idx = make_node(tmpl.lay_cnt - 1, 0, tmpl.shape.root);
   tree.record_cnt = next_record;
   tree.node_cnt = next_node;
   return &tree;
}

struct bptr_node *bptr_node_fetch(struct bptr *self, bptr_node_t index)
{
   assert(self == &tree && tree_open);
   struct bptr_node *node = fake_at(index);
   assert(!nodes[index].loaded);
   nodes[index].loaded = 1;
   loaded_nodes++;
   return node;
}

void bptr_node_unload(struct bptr *self, struct bptr_node *node)
{
   assert(self == &tree && nodes[node->node_idx].loaded);
   nodes[node->node_idx].loaded = 0;
   loaded_nodes--;
}

bptr_node_t bptr_node_split(struct bptr *self, struct bptr_node *node,
                            const void *key, const void *val)
{
   (void)key;
   (void)val;
   assert(self == &tree && tree_open && node->is_leaf);
   assert(nodes[node->node_idx].loaded && node->key_count == 4);
   unsigned int additions = 1;
   bptr_node_t parent = node->parent;
   while (parent != 0 && fake_at(parent)->key_count == 5)
    {
      additions++;
      parent = fake_at(parent)->parent;
    }
   if (parent == 0)
    {
      additions++;
      tree.height++;
      tree.root_idx = 500;
    }
   else
      fake_at(parent)->key_count++;
   node->next = 501;
   tree.node_cnt += additions;
   tree.record_cnt++;
   return node->next;
}

int bptr_unload(struct bptr *self)
{
   assert(self == &tree && tree_open && loaded_nodes == 0);
   if (instance_exists)
    {
      unloads++;
      if (!strcmp(scenario, "unload-failure") && copies == 3)
         return BPTR_E_FACCESS;
    }
   tree_open = 0;
   return BPTR_E_SUCCESS;
}

int tools_verify(const struct template *source, _Bool has_new_kv,
                  int64_t key, int64_t val,
                  const struct temp_verify_expect *expected, const char *path)
{
   (void)path;
   assert(source == &tmpl && has_new_kv && val == key * 2);
   assert(instance_exists && !tree_open && loaded_nodes == 0);
   assert(unloads == copies && verifies == copies - 1);
   if (tmpl.shape.override_count != 0)
    {
      assert(expected != NULL);
      assert(tree.height == expected->height && tree.node_cnt == expected->nodes);
      assert(expected->leaves == original_leaves + 1);
    }
   else
    {
      assert(expected == NULL);
      assert(tree.height == tmpl.lay_cnt + 1);
    }
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

   assert(argc == 3 || argc == 4);
   scenario = argv[1];
   tmpl.lay_cnt = (unsigned int)strtoul(argv[2], NULL, 10);
   assert(tmpl.lay_cnt == 1 || tmpl.lay_cnt == 2 || tmpl.lay_cnt == 4);
   tmpl.st = -11;
   tmpl.interval = 5;
   if (argc == 4) assert(temp_shape_add(&tmpl.shape, argv[3]) == 0);
   int length = temp_path(tmpl.path, sizeof tmpl.path, "fixture", tmpl.lay_cnt,
                           tmpl.st, tmpl.interval, &tmpl.shape);
   assert(length > 0 && (size_t)length < sizeof tmpl.path);
   struct split_layout lay = { tmpl.lay_cnt, 5, 6, 0 };
   struct temp_shape_counts totals;
   assert(temp_shape_count(&tmpl.shape, tmpl.lay_cnt, 1, 128, &totals) == 0);
   lay.rec_cnt = totals.records;
   struct split_sampler expected_samples = {0};
   assert(split_sampler_init_shape(&expected_samples, &lay, &tmpl.shape, 1, 128) == 0);
   count = (unsigned int)split_sampler_count(&expected_samples);
   split_sampler_destroy(&expected_samples);

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
   temp_shape_free(&tmpl.shape);
   return 0;
}
