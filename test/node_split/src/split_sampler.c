#include "split_sampler.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SPLIT_ROLE_CNT 5u
#define SPLIT_LEAF_CNT 3u

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
   uint64_t records;
   int status;

   memset(sampler, 0, sizeof *sampler);
   if (lay->lay_cnt == 0 || lay->leaf_up < 2 ||
       (lay->lay_cnt > 1 && lay->brch_up < 3))
      return SPLIT_SAMPLE_E_LAYOUT;
   records = lay->leaf_up - 1;
   for (unsigned int level = 1; level < lay->lay_cnt; level++)
    {
      if (records > UINT64_MAX / lay->brch_up)
         return SPLIT_SAMPLE_E_OVERFLOW;
      records *= lay->brch_up;
    }
   if (records != lay->rec_cnt) return SPLIT_SAMPLE_E_LAYOUT;
   sampler->lay = *lay;

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

size_t split_sampler_count(const struct split_sampler *sampler)
{ return sampler->count; }

uint64_t split_sampler_gap_at(const struct split_sampler *sampler, size_t idx)
{
   if (sampler->gaps != NULL) return sampler->gaps[idx];
   return split_small_gap_at(&sampler->lay, idx);
}

void split_sampler_destroy(struct split_sampler *sampler)
{
   free(sampler->gaps);
   memset(sampler, 0, sizeof *sampler);
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
   case SPLIT_SAMPLE_E_LAYOUT   : return "inconsistent full-template geometry";
   case SPLIT_SAMPLE_E_OVERFLOW : return "template geometry or sample count overflows";
   case SPLIT_SAMPLE_E_ALLOC    : return "cannot allocate split samples";
   case SPLIT_SAMPLE_E_KEY      : return "inserted key or value is not representable";
   default                     : return "unknown sampler status";
    }
}
