#include "split_sampler.h"
#include "../../../utils/temp/src/temp_full.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SPLIT_ROLE_CNT 5u
#define SPLIT_LEAF_CNT 3u

/* This mirror retains only explicitly overridden nodes and their ancestors.
 * The shared shape deliberately hides its trie, so its canonical formatter is
 * used to enumerate overrides once. Totals and selections aggregate untouched
 * child runs; no operation walks the potentially enormous generated tree. */
struct split_shape_node
{
   uint32_t child, keys;
   _Bool explicit_keys;
   struct temp_shape_counts totals;
   uint64_t full_leaves;
   struct split_shape_node *children, *next;
};

struct split_shape_data
{
   struct split_shape_node *root;
   struct temp_shape_counts full[255];
   _Bool full_valid[255];
   _Bool first_full;
   size_t capacity;
};

static void split_shape_free(struct split_shape_node *node)
{
   while (node != NULL)
    {
      struct split_shape_node *next = node->next;
      split_shape_free(node->children);
      free(node);
      node = next;
    }
}

static int split_add_scaled(uint64_t *dst, uint64_t value, uint64_t factor)
{
   if (factor != 0 && value > (UINT64_MAX - *dst) / factor) return 0;
   *dst += value * factor;
   return 1;
}

static int split_counts_add(struct temp_shape_counts *dst,
                            const struct temp_shape_counts *src,
                            uint64_t factor)
{
   return split_add_scaled(&dst->records, src->records, factor) &&
          split_add_scaled(&dst->leaves, src->leaves, factor) &&
          split_add_scaled(&dst->nodes, src->nodes, factor);
}

static void split_full_counts(struct split_shape_data *data,
                               const struct split_layout *lay)
{
   data->full[1] = (struct temp_shape_counts){ lay->leaf_up - 1, 1, 1 };
   data->full_valid[1] = 1;
   for (unsigned int h = 2; h <= lay->lay_cnt; h++)
    {
      data->full[h].nodes = 1;
      data->full_valid[h] = data->full_valid[h - 1] &&
         split_counts_add(&data->full[h], &data->full[h - 1], lay->brch_up);
    }
}

static int split_shape_read(struct split_shape_data *data,
                            const struct temp_shape *shape,
                            const struct split_layout *lay)
{
   int len = temp_path(NULL, 0, "", lay->lay_cnt, 0, 2, shape);
   char *name, *at;

   if (len < 0 || (size_t)len == SIZE_MAX) return SPLIT_SAMPLE_E_OVERFLOW;
   name = malloc((size_t)len + 1);
   if (name == NULL) return SPLIT_SAMPLE_E_ALLOC;
   temp_path(name, (size_t)len + 1, "", lay->lay_cnt, 0, 2, shape);
   data->root = calloc(1, sizeof *data->root);
   if (data->root == NULL)
    { free(name); return SPLIT_SAMPLE_E_ALLOC; }
   at = strstr(name, "--keys-") + 7;
   while (*at != '\0')
    {
      struct split_shape_node *node = data->root;

      if (!strncmp(at, "root=", 5)) at += 4;
      else do
       {
         char *end;
         uint32_t child = (uint32_t)strtoul(at, &end, 10);
         struct split_shape_node **slot = &node->children;

         while (*slot != NULL && (*slot)->child < child) slot = &(*slot)->next;
         if (*slot == NULL || (*slot)->child != child)
          {
            struct split_shape_node *created = calloc(1, sizeof *created);
            if (created == NULL)
             { free(name); return SPLIT_SAMPLE_E_ALLOC; }
            created->child = child;
            created->next = *slot;
            *slot = created;
          }
         node = *slot;
         at = end;
         if (*at != '.') break;
         at++;
       } while (1);
      node->keys = (uint32_t)strtoul(at + 1, &at, 10);
      node->explicit_keys = 1;
      if (*at != ',') break;
      at++;
    }
   free(name);
   return SPLIT_SAMPLE_OK;
}

static uint32_t split_node_keys(const struct split_sampler *sampler,
                                const struct split_shape_node *node,
                                unsigned int height)
{
   if (node != NULL && node->explicit_keys) return node->keys;
   return height == 1 ? sampler->lay.leaf_up - 1 : sampler->lay.brch_up - 1;
}

static int split_shape_totals(struct split_sampler *sampler,
                              struct split_shape_node *node,
                              unsigned int height)
{
   struct split_shape_data *data = sampler->shape_data;
   uint32_t keys = split_node_keys(sampler, node, height);
   uint64_t explicit_children = 0;

   if (height == 1)
    {
      node->totals = (struct temp_shape_counts){ keys, 1, 1 };
      node->full_leaves = keys == sampler->lay.leaf_up - 1;
      return SPLIT_SAMPLE_OK;
    }
   node->totals.nodes = 1;
   for (struct split_shape_node *child = node->children; child != NULL;
        child = child->next)
    {
      int status = split_shape_totals(sampler, child, height - 1);
      if (status != SPLIT_SAMPLE_OK) return status;
      if (!split_counts_add(&node->totals, &child->totals, 1) ||
          !split_add_scaled(&node->full_leaves, child->full_leaves, 1))
         return SPLIT_SAMPLE_E_OVERFLOW;
      explicit_children++;
    }
   if ((uint64_t)keys + 1 > explicit_children)
    {
      uint64_t remaining = (uint64_t)keys + 1 - explicit_children;
      if (!data->full_valid[height - 1] ||
          !split_counts_add(&node->totals, &data->full[height - 1], remaining) ||
          !split_add_scaled(&node->full_leaves, data->full[height - 1].leaves,
                             remaining)) return SPLIT_SAMPLE_E_OVERFLOW;
    }
   return SPLIT_SAMPLE_OK;
}

/* Find a child's record offset and number of eligible leaves before it.
 * Whole runs of untouched children are consumed by multiplication. */
static const struct split_shape_node *split_child_at(
   const struct split_sampler *sampler, const struct split_shape_node *node,
   unsigned int height, uint32_t index, uint64_t *offset, uint64_t *rank)
{
   const struct temp_shape_counts *full = &sampler->shape_data->full[height - 1];
   const struct split_shape_node *child = node == NULL ? NULL : node->children;
   uint64_t next = 0;

   for (; child != NULL && child->child <= index; child = child->next)
    {
      uint64_t run = child->child - next;
      if (run != 0)
       { *offset += run * full->records; *rank += run * full->leaves; }
      if (child->child == index) return child;
      *offset += child->totals.records;
      *rank += child->full_leaves;
      next = (uint64_t)child->child + 1;
    }
   if (index > next)
    {
      *offset += ((uint64_t)index - next) * full->records;
      *rank += ((uint64_t)index - next) * full->leaves;
    }
   return NULL;
}

/* Record start of the nth full leaf. The caller guarantees nth exists. */
static uint64_t split_full_leaf(const struct split_sampler *sampler,
                                const struct split_shape_node *node,
                                unsigned int height, uint64_t nth,
                                uint64_t offset)
{
   const struct temp_shape_counts *full;
   uint64_t next = 0;

   if (node == NULL) return offset + nth * (sampler->lay.leaf_up - 1);
   if (height == 1) return offset;
   full = &sampler->shape_data->full[height - 1];
   for (const struct split_shape_node *child = node->children; child != NULL;
        child = child->next)
    {
      uint64_t run = child->child - next;
      if (run != 0)
       {
         if (nth < run * full->leaves)
            return offset + nth * (sampler->lay.leaf_up - 1);
         nth -= run * full->leaves;
         offset += run * full->records;
       }
      if (nth < child->full_leaves)
         return split_full_leaf(sampler, child, height - 1, nth, offset);
      nth -= child->full_leaves;
      offset += child->totals.records;
      next = (uint64_t)child->child + 1;
    }
   return offset + nth * (sampler->lay.leaf_up - 1);
}

static int split_custom_append(struct split_sampler *sampler, uint64_t gap)
{
   struct split_shape_data *data = sampler->shape_data;

   for (size_t i = 0; i < sampler->count; i++)
      if (sampler->gaps[i] == gap) return SPLIT_SAMPLE_OK;
   if (sampler->count == data->capacity)
    {
      size_t capacity = data->capacity == 0 ? 32 : data->capacity * 2;
      uint64_t *gaps;

      if (capacity < data->capacity || capacity > SIZE_MAX / sizeof *gaps)
         return SPLIT_SAMPLE_E_OVERFLOW;
      gaps = realloc(sampler->gaps, capacity * sizeof *gaps);
      if (gaps == NULL) return SPLIT_SAMPLE_E_ALLOC;
      sampler->gaps = gaps;
      data->capacity = capacity;
    }
   sampler->gaps[sampler->count++] = gap;
   return SPLIT_SAMPLE_OK;
}

/* Five child positions around a branch split: the left edge, just below the
 * promotion point, the point itself, just above it, and the right edge. */
static unsigned int split_role(const struct split_layout *lay,
                               unsigned int role)
{
   unsigned int half = lay->brch_up / 2;

   switch (role)
    {
   case 0  : return 0;
   case 1  : return half - 1;
   case 2  : return half;
   case 3  : return half + 1;
   default : return lay->brch_up - 1;
    }
}

static unsigned int split_leaf_pos(const struct split_layout *lay,
                                   unsigned int role)
{
   unsigned int last_below = lay->leaf_up - 1 - lay->leaf_up / 2;

   switch (role)
    {
   case 0  : return last_below;
   case 1  : return last_below + 1;
   default : return lay->leaf_up - 1;
    }
}

/* Original height <=3 sampler: exhaustive below height 3, then the Cartesian
 * product of the root, level-1 and leaf roles. Keep its order and duplicates. */
static uint64_t split_small_gap_at(const struct split_layout *lay, size_t idx)
{
   unsigned int leaf_role, l1_role, root_role;
   uint64_t leaf;

   if (lay->lay_cnt <= 2) return (uint64_t)idx;
   if (idx == 0) return 0;
   if (idx == 1) return lay->rec_cnt;
   idx -= 2;

   leaf_role = (unsigned int)(idx % SPLIT_LEAF_CNT);
   idx /= SPLIT_LEAF_CNT;
   l1_role = (unsigned int)(idx % SPLIT_ROLE_CNT);
   root_role = (unsigned int)(idx / SPLIT_ROLE_CNT);
   leaf = (uint64_t)split_role(lay, root_role) * lay->brch_up
          + split_role(lay, l1_role);
   return leaf * (lay->leaf_up - 1) + split_leaf_pos(lay, leaf_role);
}

/* Base-5 integers order coefficient vectors lexicographically, most
 * significant coordinate first. The first nonzero coordinate must be 1:
 * this selects one representative from each nonzero scalar multiple. */
static _Bool split_canonical(size_t vector)
{
   while (vector >= SPLIT_ROLE_CNT) vector /= SPLIT_ROLE_CNT;
   return vector == 1;
}

static unsigned int split_projection(size_t vector, size_t row)
{
   unsigned int sum = 0;

   while (vector != 0 && row != 0)
    {
      sum += (unsigned int)(vector % SPLIT_ROLE_CNT)
             * (unsigned int)(row % SPLIT_ROLE_CNT);
      vector /= SPLIT_ROLE_CNT;
      row /= SPLIT_ROLE_CNT;
    }
   return sum % SPLIT_ROLE_CNT;
}

static unsigned int split_custom_role(uint32_t children, unsigned int role)
{
   uint32_t half = children / 2;

   switch (role)
    {
   case 0  : return 0;
   case 1  : return half - 1;
   case 2  : return half;
   case 3  : return half + 1 < children ? half + 1 : children - 1;
   default : return children - 1;
    }
}

static int split_leaf_roles(struct split_sampler *sampler, uint64_t offset)
{
   for (unsigned int role = 0; role < SPLIT_LEAF_CNT; role++)
    {
      unsigned int pos = split_leaf_pos(&sampler->lay, role);
      int status;

      /* Position zero at another leaf's start belongs to its left neighbor.
       * In particular, one-key leaves must not leak ineligible boundaries. */
      if (pos == 0 && offset != 0) continue;
      status = split_custom_append(sampler, offset + pos);
      if (status != SPLIT_SAMPLE_OK) return status;
    }
   return SPLIT_SAMPLE_OK;
}

static int split_anchor(struct split_sampler *sampler,
                         const struct split_shape_node *anchor,
                         unsigned int height, uint64_t offset,
                         const size_t *vectors, size_t rows)
{
   unsigned int depth = sampler->lay.lay_cnt - height;
   int status;

   if (anchor->full_leaves == 0) return SPLIT_SAMPLE_OK;
   status = split_leaf_roles(sampler,
      split_full_leaf(sampler, anchor, height, 0, offset));
   if (status != SPLIT_SAMPLE_OK) return status;
   status = split_leaf_roles(sampler,
      split_full_leaf(sampler, anchor, height, anchor->full_leaves - 1, offset));
   if (status != SPLIT_SAMPLE_OK) return status;
   for (size_t row = 0; row < rows; row++)
    {
      const struct split_shape_node *node = anchor;
      uint64_t start = offset;
      for (unsigned int h = height; h > 1; h--)
       {
         uint32_t children = split_node_keys(sampler, node, h) + 1;
         uint32_t child = split_custom_role(children,
            split_projection(vectors[depth + height - h], row));
         uint64_t unused_rank = 0;

         node = split_child_at(sampler, node, h, child, &start, &unused_rank);
       }
      if (split_node_keys(sampler, node, 1) == sampler->lay.leaf_up - 1)
       {
         status = split_leaf_roles(sampler, start);
         if (status != SPLIT_SAMPLE_OK) return status;
       }
    }
   return SPLIT_SAMPLE_OK;
}

static int split_shape_anchors(struct split_sampler *sampler,
                                const struct split_shape_node *node,
                                unsigned int height, uint64_t offset,
                                uint64_t rank, const size_t *vectors,
                                size_t rows)
{
   int status;

   if (height > 1 && (node == sampler->shape_data->root || node->explicit_keys))
    {
      status = split_anchor(sampler, node, height, offset, vectors, rows);
      if (status != SPLIT_SAMPLE_OK) return status;
    }
   if (height == 1)
    {
      uint64_t after = rank + node->full_leaves;
      uint64_t total = sampler->shape_data->root->full_leaves;

      if (node->full_leaves != 0)
       {
         status = split_leaf_roles(sampler, offset);
         if (status != SPLIT_SAMPLE_OK) return status;
       }
      if (rank > 0)
       {
         status = split_leaf_roles(sampler, split_full_leaf(sampler,
            sampler->shape_data->root, sampler->lay.lay_cnt, rank - 1, 0));
         if (status != SPLIT_SAMPLE_OK) return status;
       }
      if (after < total)
         return split_leaf_roles(sampler, split_full_leaf(sampler,
            sampler->shape_data->root, sampler->lay.lay_cnt, after, 0));
      return SPLIT_SAMPLE_OK;
    }
   for (const struct split_shape_node *child = node->children; child != NULL;
        child = child->next)
    {
      uint64_t start = offset, before = rank;

      split_child_at(sampler, node, height, child->child, &start, &before);
      status = split_shape_anchors(sampler, child, height - 1, start, before,
                                    vectors, rows);
      if (status != SPLIT_SAMPLE_OK) return status;
    }
   return SPLIT_SAMPLE_OK;
}

static int split_custom_large_init(struct split_sampler *sampler)
{
   size_t levels = sampler->lay.lay_cnt - 1, rows = 25, used = 0;
   size_t *vectors;
   int status;

   while ((rows - 1) / (SPLIT_ROLE_CNT - 1) < levels)
    {
      if (rows > SIZE_MAX / SPLIT_ROLE_CNT) return SPLIT_SAMPLE_E_OVERFLOW;
      rows *= SPLIT_ROLE_CNT;
    }
   if (levels > SIZE_MAX / sizeof *vectors) return SPLIT_SAMPLE_E_OVERFLOW;
   vectors = malloc(levels * sizeof *vectors);
   if (vectors == NULL) return SPLIT_SAMPLE_E_ALLOC;
   for (size_t vector = 1; used < levels; vector++)
      if (split_canonical(vector)) vectors[used++] = vector;
   if (sampler->shape_data->first_full)
    {
      status = split_custom_append(sampler, 0);
      if (status != SPLIT_SAMPLE_OK)
       { free(vectors); return status; }
    }
   status = split_custom_append(sampler, split_full_leaf(sampler,
      sampler->shape_data->root, sampler->lay.lay_cnt,
      sampler->shape_data->root->full_leaves - 1, 0) + sampler->lay.leaf_up - 1);
   if (status == SPLIT_SAMPLE_OK)
      status = split_shape_anchors(sampler, sampler->shape_data->root,
                                    sampler->lay.lay_cnt, 0, 0, vectors, rows);
   free(vectors);
   return status;
}

/* Decode the owner of a physical gap from actual subtree record totals. */
static uint32_t split_owned_leaf(const struct split_sampler *sampler,
                                 uint64_t gap, uint32_t *route)
{
   const struct split_shape_node *node = sampler->shape_data->root;
   uint64_t record = gap == 0 ? 0 : gap - 1;

   for (unsigned int height = sampler->lay.lay_cnt; height > 1; height--)
    {
      const struct temp_shape_counts *full = &sampler->shape_data->full[height - 1];
      const struct split_shape_node *child = node == NULL ? NULL : node->children;
      uint64_t next = 0;
      _Bool found = 0;

      route[sampler->lay.lay_cnt - height] = split_node_keys(sampler, node, height);
      for (; child != NULL; child = child->next)
       {
         uint64_t run = child->child - next;
         if (run != 0 && record < run * full->records)
          { node = NULL; record %= full->records; found = 1; break; }
         if (run != 0) record -= run * full->records;
         if (record < child->totals.records)
          { node = child; found = 1; break; }
         record -= child->totals.records;
         next = (uint64_t)child->child + 1;
       }
      if (!found)
       { node = NULL; record %= full->records; }
    }
   return split_node_keys(sampler, node, 1);
}

static void split_append(struct split_sampler *sampler, uint64_t gap)
{
   for (size_t i = 0; i < sampler->count; i++)
      if (sampler->gaps[i] == gap) return;
   sampler->gaps[sampler->count++] = gap;
}

/**
 * Build projections independently of the small-tree Cartesian sampler.
 * Distinct canonical coefficient vectors are linearly independent in pairs
 * over F_5. Enumerating all rows therefore covers all 25 role combinations
 * for any two branch levels. Crossing each row with the leaf roles also
 * covers each such pair at each realizable leaf position.
 *
 * Neither generation nor deduplication visits tree nodes or existing keys:
 * their cost depends on height and the compact candidate list only.
 */
static int split_large_init(struct split_sampler *sampler)
{
   const struct split_layout *lay = &sampler->lay;
   size_t levels = lay->lay_cnt - 1, rows = 25, capacity, used = 0;
   size_t *vectors;

   while ((rows - 1) / (SPLIT_ROLE_CNT - 1) < levels)
    {
      if (rows > SIZE_MAX / SPLIT_ROLE_CNT)
         return SPLIT_SAMPLE_E_OVERFLOW;
      rows *= SPLIT_ROLE_CNT;
    }
   if (rows > (SIZE_MAX - 2) / SPLIT_LEAF_CNT ||
       levels > SIZE_MAX / sizeof *vectors)
      return SPLIT_SAMPLE_E_OVERFLOW;
   capacity = 2 + SPLIT_LEAF_CNT * rows;
   if (capacity > SIZE_MAX / sizeof *sampler->gaps)
      return SPLIT_SAMPLE_E_OVERFLOW;

   vectors = malloc(levels * sizeof *vectors);
   if (vectors == NULL) return SPLIT_SAMPLE_E_ALLOC;
   sampler->gaps = malloc(capacity * sizeof *sampler->gaps);
   if (sampler->gaps == NULL)
    { free(vectors); return SPLIT_SAMPLE_E_ALLOC; }

   for (size_t vector = 1; used < levels; vector++)
      if (split_canonical(vector)) vectors[used++] = vector;

   split_append(sampler, 0);
   split_append(sampler, lay->rec_cnt);
   for (size_t row = 0; row < rows; row++)
    {
      uint64_t leaf = 0;

      /* Geometry was checked first: these partial indices and the final
       * leaf offset cannot exceed the uint64_t record count. */
      for (size_t level = 0; level < levels; level++)
         leaf = leaf * lay->brch_up
                + split_role(lay, split_projection(vectors[level], row));
      for (unsigned int role = 0; role < SPLIT_LEAF_CNT; role++)
         split_append(sampler, leaf * (lay->leaf_up - 1)
                               + split_leaf_pos(lay, role));
    }
   free(vectors);
   return SPLIT_SAMPLE_OK;
}

int split_sampler_init(struct split_sampler *sampler,
                       const struct split_layout *lay)
{
   uint64_t records, nodes = 1, leaves = 1;
   int status;

   memset(sampler, 0, sizeof *sampler);
   if (lay->lay_cnt == 0 || lay->leaf_up < 2 ||
       (lay->lay_cnt > 1 && lay->brch_up < 3))
      return SPLIT_SAMPLE_E_LAYOUT;
   records = lay->leaf_up - 1;
   for (unsigned int level = 1; level < lay->lay_cnt; level++)
    {
      if (records > UINT64_MAX / lay->brch_up ||
          nodes > (UINT64_MAX - 1) / lay->brch_up)
         return SPLIT_SAMPLE_E_OVERFLOW;
      records *= lay->brch_up;
      leaves *= lay->brch_up;
      nodes = nodes * lay->brch_up + 1;
    }
   if (records != lay->rec_cnt) return SPLIT_SAMPLE_E_LAYOUT;
   sampler->lay = *lay;
   sampler->original = (struct temp_shape_counts){ records, leaves, nodes };

   if (lay->lay_cnt <= 2)
    {
      if (records >= SIZE_MAX) return SPLIT_SAMPLE_E_OVERFLOW;
      sampler->count = (size_t)records + 1;
      return SPLIT_SAMPLE_OK;
    }
   if (lay->lay_cnt == 3)
    {
      sampler->count = 2 + SPLIT_ROLE_CNT * SPLIT_ROLE_CNT * SPLIT_LEAF_CNT;
      return SPLIT_SAMPLE_OK;
    }
   status = split_large_init(sampler);
   if (status != SPLIT_SAMPLE_OK) split_sampler_destroy(sampler);
   return status;
}

int split_sampler_init_shape(struct split_sampler *sampler,
                             const struct split_layout *lay,
                             const struct temp_shape *shape,
                             _Bool is_lite, uint32_t node_size)
{
   struct temp_shape_counts counts, leaf, branch;
   uint32_t route[254];
   int status;

   if (shape == NULL || shape->override_count == 0)
      return split_sampler_init(sampler, lay);
   memset(sampler, 0, sizeof *sampler);
   status = temp_shape_count(shape, lay->lay_cnt, is_lite, node_size, &counts);
   if (status != TEMP_FULL_OK)
      return status == TEMP_FULL_E_PTR_OVERFLOW ? SPLIT_SAMPLE_E_OVERFLOW :
                                                 SPLIT_SAMPLE_E_LAYOUT;
   if (temp_shape_count(NULL, 1, is_lite, node_size, &leaf) != TEMP_FULL_OK ||
       temp_shape_count(NULL, 2, is_lite, node_size, &branch) != TEMP_FULL_OK ||
       lay->leaf_up < 2 || lay->leaf_up - 1 != leaf.records ||
       lay->brch_up != branch.records / leaf.records ||
       lay->rec_cnt != counts.records) return SPLIT_SAMPLE_E_LAYOUT;
   sampler->lay = *lay;
   sampler->original = counts;
   sampler->custom = 1;
   sampler->shape_data = calloc(1, sizeof *sampler->shape_data);
   if (sampler->shape_data == NULL) return SPLIT_SAMPLE_E_ALLOC;
   split_full_counts(sampler->shape_data, lay);
   status = split_shape_read(sampler->shape_data, shape, lay);
   if (status == SPLIT_SAMPLE_OK)
      status = split_shape_totals(sampler, sampler->shape_data->root, lay->lay_cnt);
   if (status != SPLIT_SAMPLE_OK)
    { split_sampler_destroy(sampler); return status; }
   if (memcmp(&sampler->shape_data->root->totals, &counts, sizeof counts))
    { split_sampler_destroy(sampler); return SPLIT_SAMPLE_E_LAYOUT; }
   sampler->nonfull_leaves = counts.leaves - sampler->shape_data->root->full_leaves;
   sampler->shape_data->first_full =
      split_owned_leaf(sampler, 0, route) == lay->leaf_up - 1;
   if (sampler->shape_data->root->full_leaves == 0) return SPLIT_SAMPLE_OK;
   if (lay->lay_cnt <= 2)
    {
      uint64_t gaps = sampler->shape_data->root->full_leaves;
      if (gaps > (SIZE_MAX - sampler->shape_data->first_full) / (lay->leaf_up - 1))
       { split_sampler_destroy(sampler); return SPLIT_SAMPLE_E_OVERFLOW; }
      sampler->count = (size_t)(gaps * (lay->leaf_up - 1)) +
                       sampler->shape_data->first_full;
      return SPLIT_SAMPLE_OK;
    }
   status = split_custom_large_init(sampler);
   if (status != SPLIT_SAMPLE_OK) split_sampler_destroy(sampler);
   return status;
}

size_t split_sampler_count(const struct split_sampler *sampler)
{ return sampler->count; }

uint64_t split_sampler_gap_at(const struct split_sampler *sampler, size_t idx)
{
   if (sampler->gaps != NULL) return sampler->gaps[idx];
   if (sampler->custom)
    {
      uint64_t keys = sampler->lay.leaf_up - 1;
      if (sampler->shape_data->first_full)
       { if (idx == 0) return 0; idx--; }
      return split_full_leaf(sampler, sampler->shape_data->root,
                             sampler->lay.lay_cnt, idx / keys, 0) + idx % keys + 1;
    }
   return split_small_gap_at(&sampler->lay, idx);
}

void split_sampler_destroy(struct split_sampler *sampler)
{
   free(sampler->gaps);
   if (sampler->shape_data != NULL)
    {
      split_shape_free(sampler->shape_data->root);
      free(sampler->shape_data);
    }
   memset(sampler, 0, sizeof *sampler);
}

int split_sampler_expect(const struct split_sampler *sampler, uint64_t gap,
                          struct temp_verify_expect *out,
                          unsigned int *stop_depth)
{
   uint32_t route[254];
   uint64_t splits = 1, extra;
   unsigned int stop = UINT_MAX;
   struct temp_verify_expect result;

   if (sampler->lay.lay_cnt == 0 || gap > sampler->lay.rec_cnt)
      return SPLIT_SAMPLE_E_LAYOUT;
   if (sampler->custom)
    {
      if (split_owned_leaf(sampler, gap, route) != sampler->lay.leaf_up - 1)
         return SPLIT_SAMPLE_E_LAYOUT;
      for (unsigned int depth = sampler->lay.lay_cnt - 1; depth > 0; depth--)
       {
         if (route[depth - 1] < sampler->lay.brch_up - 1)
          { stop = depth - 1; break; }
         splits++;
       }
    }
   else splits = sampler->lay.lay_cnt;
   extra = splits + (stop == UINT_MAX);
   if (sampler->original.nodes > UINT64_MAX - extra ||
       sampler->original.leaves == UINT64_MAX ||
       sampler->original.records == UINT64_MAX)
      return SPLIT_SAMPLE_E_OVERFLOW;
   result.height = sampler->lay.lay_cnt + (stop == UINT_MAX);
   result.nodes = sampler->original.nodes + extra;
   result.leaves = sampler->original.leaves + 1;
   *out = result;
   *stop_depth = stop;
   return SPLIT_SAMPLE_OK;
}

int split_sampler_kv(int64_t st, int64_t interval, uint64_t gap,
                     int64_t *key, int64_t *val)
{
   __int128 wide_key, wide_val;

   if (interval < 2) return SPLIT_SAMPLE_E_KEY;
   wide_key = (__int128)st - interval / 2 + (__int128)gap * interval;
   if (wide_key < INT64_MIN || wide_key > INT64_MAX)
      return SPLIT_SAMPLE_E_KEY;
   wide_val = wide_key * 2;
   if (wide_val < INT64_MIN || wide_val > INT64_MAX)
      return SPLIT_SAMPLE_E_KEY;
   *key = (int64_t)wide_key;
   *val = (int64_t)wide_val;
   return SPLIT_SAMPLE_OK;
}

const char *split_sampler_strerror(int status)
{
   switch (status)
    {
   case SPLIT_SAMPLE_OK         : return "success";
   case SPLIT_SAMPLE_E_LAYOUT   : return "inconsistent template geometry or ineligible leaf";
   case SPLIT_SAMPLE_E_OVERFLOW : return "template geometry or sample count overflows";
   case SPLIT_SAMPLE_E_ALLOC    : return "cannot allocate split samples";
   case SPLIT_SAMPLE_E_KEY      : return "inserted key or value is not representable";
   default                     : return "unknown sampler status";
    }
}
