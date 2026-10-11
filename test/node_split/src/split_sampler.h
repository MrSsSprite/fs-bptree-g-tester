#ifndef SPLIT_SAMPLER_H
#define SPLIT_SAMPLER_H

#include <stddef.h>
#include <stdint.h>

/* Capacities come from the loaded tree, not a second capacity formula. */
struct split_layout
{
   unsigned int lay_cnt;
   unsigned int leaf_up;
   unsigned int brch_up;
   uint64_t rec_cnt;
};

struct split_sampler
{
   struct split_layout lay;
   size_t count;
   uint64_t *gaps;   /* only taller trees allocate a compact sample list */
};

enum split_sample_status
{
   SPLIT_SAMPLE_OK,
   SPLIT_SAMPLE_E_LAYOUT,
   SPLIT_SAMPLE_E_OVERFLOW,
   SPLIT_SAMPLE_E_ALLOC,
   SPLIT_SAMPLE_E_KEY
};

/* Initialize an empty sampler. On failure it remains safe to destroy.
 * Heights 1--3 retain their original sequence, including repeated positions;
 * heights >=4 use distinct projected positions, with both endpoints first. */
int split_sampler_init(struct split_sampler *sampler,
                       const struct split_layout *lay);
size_t split_sampler_count(const struct split_sampler *sampler);
/* idx must be less than split_sampler_count(sampler). */
uint64_t split_sampler_gap_at(const struct split_sampler *sampler, size_t idx);
void split_sampler_destroy(struct split_sampler *sampler);

/* Compute an inserted record without overflowing signed arithmetic.
 * A positive interval of at least 2 is required. Outputs change only on success. */
int split_sampler_kv(int64_t st, int64_t interval, uint64_t gap,
                     int64_t *key, int64_t *val);
const char *split_sampler_strerror(int status);

#endif
