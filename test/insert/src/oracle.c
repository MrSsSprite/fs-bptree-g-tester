#include "oracle.h"
#include "bptr_internal.h"
#include "bptr_node.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void insert_model_init(struct insert_model *model, size_t key_size,
                       size_t value_size,
                       int (*compare)(const void *, const void *))
{
   memset(model, 0, sizeof *model);
   model->key_size = key_size;
   model->value_size = value_size;
   model->compare = compare;
}

void insert_model_free(struct insert_model *model)
{
   free(model->records);
   memset(model, 0, sizeof *model);
}

const unsigned char *insert_model_key(const struct insert_model *model,
                                      size_t index)
{
   return index < model->count ?
      model->records + index * (model->key_size + model->value_size) : NULL;
}

const unsigned char *insert_model_value(const struct insert_model *model,
                                        size_t index)
{
   const unsigned char *key = insert_model_key(model, index);
   return key == NULL ? NULL : key + model->key_size;
}

int insert_model_add(struct insert_model *model, const void *key,
                      const void *value)
{
   size_t lo = 0, hi = model->count, stride, capacity;
   unsigned char *record, *grown;

   if (model->key_size == 0 || model->value_size == 0 ||
       model->key_size > SIZE_MAX - model->value_size ||
       model->compare == NULL) return BPTR_E_FN_INPUT;
   stride = model->key_size + model->value_size;
   while (lo < hi)
    {
      size_t mid = lo + (hi - lo) / 2;
      int cmp = model->compare(key, insert_model_key(model, mid));
      if (cmp == 0) return BPTR_E_KEY_EXIST;
      if (cmp < 0) hi = mid;
      else lo = mid + 1;
    }
   if (model->count == SIZE_MAX / stride) return BPTR_E_OOM;
   record = malloc(stride);
   if (record == NULL) return BPTR_E_OOM;
   memcpy(record, key, model->key_size);
   memcpy(record + model->key_size, value, model->value_size);
   if (model->count == model->capacity)
    {
      capacity = model->capacity == 0 ? 8 : model->capacity;
      if (capacity <= SIZE_MAX / 2) capacity *= 2;
      if (capacity > SIZE_MAX / stride) capacity = SIZE_MAX / stride;
      if (capacity <= model->count)
       {
         free(record);
         return BPTR_E_OOM;
       }
      grown = realloc(model->records, capacity * stride);
      if (grown == NULL)
       {
         free(record);
         return BPTR_E_OOM;
       }
      model->records = grown;
      model->capacity = capacity;
    }
   memmove(model->records + (lo + 1) * stride, model->records + lo * stride,
            (model->count - lo) * stride);
   memcpy(model->records + lo * stride, record, stride);
   free(record);
   model->count++;
   return BPTR_E_SUCCESS;
}

struct snapshot
{
   bptr_node_t index, parent, prev, next;
   uint32_t count;
   uint32_t level;
   size_t first_child;
   unsigned char *keys, *values;
   const unsigned char *minimum;
};

struct observer
{
   struct bptr *tree;
   const struct insert_model *model;
   char *reason;
   size_t reason_size;
   struct snapshot *nodes;
   size_t count, bound;
   bptr_node_t *seen;
   size_t seen_size;
   bptr_node_t max_index;
};

static int fail(struct observer *observer, const char *format, ...)
{
   va_list args;
   va_start(args, format);
   if (observer->reason != NULL && observer->reason_size != 0)
      vsnprintf(observer->reason, observer->reason_size, format, args);
   va_end(args);
   return 1;
}

static int enqueue(struct observer *observer, bptr_node_t index,
                    bptr_node_t parent, uint32_t level)
{
   size_t slot;
   struct snapshot *node;

   if (index == 0 || index > observer->max_index)
      return fail(observer, "live child index %" PRIuFAST64
                  " outside [1,%" PRIuFAST64 "]", index,
                  observer->max_index);
   if (observer->count == observer->bound)
      return fail(observer, "live traversal exceeds %zu nodes for %zu records",
                  observer->bound, observer->model->count);
   /* Multiplication is unsigned, intentionally wrapping as a hash. */
   slot = (size_t)(index * UINT64_C(11400714819323198485)) &
          (observer->seen_size - 1);
   while (observer->seen[slot] != 0)
    {
      if (observer->seen[slot] == index)
         return fail(observer, "live cycle or duplicate child ownership at node %"
                     PRIuFAST64, index);
      slot = (slot + 1) & (observer->seen_size - 1);
    }
   observer->seen[slot] = index;
   node = &observer->nodes[observer->count++];
   node->index = index;
   node->parent = parent;
   node->level = level;
   return 0;
}

static int same_bytes(struct observer *observer, const unsigned char *actual,
                       const unsigned char *expected, size_t width,
                       bptr_node_t index, size_t slot, size_t record,
                       const char *kind)
{
   for (size_t byte = 0; byte < width; byte++)
      if (actual[byte] != expected[byte])
         return fail(observer, "live node %" PRIuFAST64
                     " slot %zu record %zu %s byte %zu: expected %02x actual %02x",
                     index, slot, record, kind, byte,
                     (unsigned)expected[byte], (unsigned)actual[byte]);
   return 0;
}

static int capture(struct observer *observer, size_t position,
                    size_t leaf_capacity, size_t branch_capacity)
{
   struct bptr *tree = observer->tree;
   struct snapshot *copy = &observer->nodes[position];
   struct bptr_node *node = bptr_node_fetch(tree, copy->index);
   size_t capacity, minimum, key_bytes, value_bytes, pointer_bytes;
   int status = 0;

   if (node == NULL)
      return fail(observer, "live fetch node %" PRIuFAST64 " failed: %d",
                  copy->index, bptr_errno);
   capacity = copy->level == 0 ? leaf_capacity : branch_capacity;
   minimum = position == 0 ? 1 :
      (copy->level == 0 ? (leaf_capacity + 1) / 2 : branch_capacity / 2);
   if (node->node_idx != copy->index || node->parent != copy->parent)
      status = fail(observer, "live node %" PRIuFAST64
                    " identity/parent: actual index %" PRIuFAST64
                    " parent %" PRIuFAST64 ", expected parent %" PRIuFAST64,
                    copy->index, node->node_idx, node->parent, copy->parent);
   else if ((node->flags & BPTR_NODE_FLAG_VALID) == 0 ||
            node->level != copy->level ||
            node->is_leaf != (copy->level == 0) ||
            !!(node->flags & BPTR_NODE_FLAG_LEAF) != (copy->level == 0))
      status = fail(observer, "live node %" PRIuFAST64
                    " flags/level: flags %u leaf %u level %u, expected level %u",
                    copy->index, (unsigned)node->flags, (unsigned)node->is_leaf,
                    (unsigned)node->level, (unsigned)copy->level);
   else if (node->key_count < minimum || node->key_count > capacity)
      status = fail(observer, "live node %" PRIuFAST64
                    " occupancy %u outside [%zu,%zu]", copy->index,
                    node->key_count, minimum, capacity);
   if (status != 0)
    {
      bptr_node_unload(tree, node);
      return status;
    }
   copy->count = node->key_count;
   copy->prev = node->prev;
   copy->next = node->next;
   pointer_bytes = tree->is_lite ? 4 : 8;
   key_bytes = copy->count * observer->model->key_size;
   value_bytes = copy->level == 0 ?
      copy->count * observer->model->value_size :
      ((size_t)copy->count + 1) * pointer_bytes;
   copy->keys = malloc(key_bytes);
   copy->values = malloc(value_bytes);
   if (copy->keys == NULL || copy->values == NULL)
      status = fail(observer, "live snapshot allocation failed at node %"
                    PRIuFAST64, copy->index);
   else if (node->vals == NULL)
      status = fail(observer, "live node %" PRIuFAST64 " has NULL values",
                    copy->index);
   else
    {
      memcpy(copy->keys, node->keys, key_bytes);
      memcpy(copy->values, node->vals, value_bytes);
    }
   bptr_node_unload(tree, node);
   if (status != 0) return status;
   for (size_t i = 1; i < copy->count; i++)
      if (observer->model->compare(
             copy->keys + (i - 1) * observer->model->key_size,
             copy->keys + i * observer->model->key_size) >= 0)
         return fail(observer, "live node %" PRIuFAST64
                     " keys are not strictly ordered at slot %zu", copy->index,
                     i);
   if (copy->level != 0)
    {
      copy->first_child = observer->count;
      for (size_t i = 0; i <= copy->count; i++)
       {
         bptr_node_t child;
         if (tree->is_lite)
          {
            uint32_t stored;
            memcpy(&stored, copy->values + i * pointer_bytes, sizeof stored);
            child = stored;
          }
         else
          {
            uint64_t stored;
            memcpy(&stored, copy->values + i * pointer_bytes, sizeof stored);
            child = stored;
          }
         if (enqueue(observer, child, copy->index, copy->level - 1) != 0)
            return 1;
       }
    }
   return 0;
}

int insert_live_verify(struct bptr *tree, const struct insert_model *model,
                        char *reason, size_t reason_size)
{
   struct observer observer;
   size_t leaf_capacity, branch_capacity, pointer_bytes, record = 0;
   int status = 1;

   memset(&observer, 0, sizeof observer);
   observer.tree = tree;
   observer.model = model;
   observer.reason = reason;
   observer.reason_size = reason_size;
   if (reason != NULL && reason_size != 0) reason[0] = '\0';
   if (tree == NULL || model == NULL)
      return fail(&observer, "live observer requires a tree and reference model");
   if (model->compare == NULL || tree->key_size != model->key_size ||
       tree->value_size != model->value_size || model->key_size == 0 ||
       model->value_size == 0 ||
       model->key_size > SIZE_MAX - model->value_size)
      return fail(&observer, "live tree/model key or value configuration differs");
   if (tree->record_cnt != model->count)
      return fail(&observer, "live record count: expected %zu actual %"
                  PRIuFAST64, model->count, tree->record_cnt);
   if (model->count == 0)
    {
      if (tree->root_idx != 0 || tree->height != 0 || tree->node_cnt != 0)
         return fail(&observer, "live empty tree has root %" PRIuFAST64
                     " height %" PRIuFAST32 " nodes %" PRIuFAST64,
                     tree->root_idx, tree->height, tree->node_cnt);
      return 0;
    }
   pointer_bytes = tree->is_lite ? 4 : 8;
   if (tree->node_size < 64 + pointer_bytes || tree->height == 0 ||
       tree->height > UINT16_MAX + UINT32_C(1) ||
       tree->height > model->count || model->count > (SIZE_MAX - 1) / 2)
      return fail(&observer, "live tree has impossible geometry or height");
   leaf_capacity = (tree->node_size - 64) /
                   (model->key_size + model->value_size);
   branch_capacity = (tree->node_size - 64 - pointer_bytes) /
                     (model->key_size + pointer_bytes);
   if (leaf_capacity < 1 || branch_capacity < 2)
      return fail(&observer, "live layout cannot hold a valid tree");
   /* Every internal node has at least two children and every leaf a record. */
   observer.bound = model->count * 2 - 1;
   if (tree->node_cnt == 0 || tree->node_cnt > observer.bound ||
       tree->free_list.cnt > UINT_FAST64_MAX - tree->node_cnt)
      return fail(&observer, "live node/free counts cannot describe the model");
   observer.max_index = tree->node_cnt + tree->free_list.cnt;
   observer.seen_size = 8;
   while (observer.seen_size / 2 < observer.bound)
    {
      if (observer.seen_size > SIZE_MAX / 2)
         return fail(&observer, "live traversal index allocation overflow");
      observer.seen_size *= 2;
    }
   observer.nodes = calloc(observer.bound, sizeof *observer.nodes);
   observer.seen = calloc(observer.seen_size, sizeof *observer.seen);
   if (observer.nodes == NULL || observer.seen == NULL)
    {
      fail(&observer, "live traversal allocation failed");
      goto cleanup;
    }
   if (enqueue(&observer, tree->root_idx, 0, (uint32_t)(tree->height - 1)) != 0)
      goto cleanup;
   for (size_t i = 0; i < observer.count; i++)
      if (capture(&observer, i, leaf_capacity, branch_capacity) != 0)
         goto cleanup;
   if (observer.count != tree->node_cnt)
    {
      fail(&observer, "live reachable nodes: expected %" PRIuFAST64
            " actual %zu", tree->node_cnt, observer.count);
      goto cleanup;
    }
   for (size_t i = 0; i < observer.count; i++)
    {
      struct snapshot *node = &observer.nodes[i];
      bptr_node_t prev = i != 0 &&
         observer.nodes[i - 1].level == node->level ?
         observer.nodes[i - 1].index : 0;
      bptr_node_t next = i + 1 != observer.count &&
         observer.nodes[i + 1].level == node->level ?
         observer.nodes[i + 1].index : 0;
      if (node->prev != prev || node->next != next)
       {
         fail(&observer, "live node %" PRIuFAST64
               " sibling links: expected prev %" PRIuFAST64
               " next %" PRIuFAST64 ", actual prev %" PRIuFAST64
               " next %" PRIuFAST64, node->index, prev, next,
               node->prev, node->next);
         goto cleanup;
       }
      if (node->level != 0) continue;
      for (size_t slot = 0; slot < node->count; slot++, record++)
       {
         if (record >= model->count)
          {
            fail(&observer, "live node %" PRIuFAST64
                  " slot %zu contains extra record %zu", node->index,
                  slot, record);
            goto cleanup;
          }
         if (same_bytes(&observer, node->keys + slot * model->key_size,
                         insert_model_key(model, record), model->key_size,
                         node->index, slot, record, "key") != 0 ||
             same_bytes(&observer, node->values + slot * model->value_size,
                         insert_model_value(model, record), model->value_size,
                         node->index, slot, record, "value") != 0)
            goto cleanup;
       }
    }
   if (record != model->count)
    {
      fail(&observer, "live leaf records: expected %zu actual %zu",
            model->count, record);
      goto cleanup;
    }
   /* Children occur later in breadth-first order, so minima flow backward. */
   for (size_t i = observer.count; i-- != 0;)
    {
      struct snapshot *node = &observer.nodes[i];
      if (node->level == 0) node->minimum = node->keys;
      else
       {
         node->minimum = observer.nodes[node->first_child].minimum;
         for (size_t slot = 0; slot < node->count; slot++)
            if (model->compare(node->keys + slot * model->key_size,
                  observer.nodes[node->first_child + slot + 1].minimum) != 0)
             {
               fail(&observer, "live node %" PRIuFAST64
                     " separator slot %zu differs from right subtree minimum",
                     node->index, slot);
               goto cleanup;
             }
       }
    }
   status = 0;

cleanup:
   for (size_t i = 0; observer.nodes != NULL && i < observer.count; i++)
    {
      free(observer.nodes[i].keys);
      free(observer.nodes[i].values);
    }
   free(observer.nodes);
   free(observer.seen);
   return status;
}
