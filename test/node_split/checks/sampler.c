#include "../src/split_sampler.h"
#include "unity.h"
#include <limits.h>

static struct split_sampler samples, again;

void setUp(void) { }
void tearDown(void)
{
   split_sampler_destroy(&samples);
   split_sampler_destroy(&again);
}

static struct split_layout layout(unsigned int height, unsigned int children,
                                   unsigned int leaf_keys)
{
   struct split_layout lay = { height, leaf_keys + 1, children, leaf_keys };

   for (unsigned int i = 1; i < height; i++) lay.rec_cnt *= children;
   return lay;
}

static void init(const struct split_layout *lay)
{
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK, split_sampler_init(&samples, lay));
}

static void test_exhaustive_small_trees(void)
{
   for (unsigned int height = 1; height <= 2; height++)
    {
      struct split_layout lay = layout(height, 38, 28);

      init(&lay);
      TEST_ASSERT_NULL(samples.gaps);
      TEST_ASSERT_EQUAL_UINT64(lay.rec_cnt + 1, split_sampler_count(&samples));
      for (size_t i = 0; i < split_sampler_count(&samples); i++)
         TEST_ASSERT_EQUAL_UINT64(i, split_sampler_gap_at(&samples, i));
      split_sampler_destroy(&samples);
    }
}

/* Golden sequence from the original default-lite, three-level sampler.
 * Including the repeated final endpoint locks both its ordering and count. */
static void test_three_level_sequence(void)
{
   const uint64_t expected[] = {
      0, 40432,
      14, 15, 28, 518, 519, 532, 546, 547, 560, 574, 575, 588,
      1050, 1051, 1064,
      19166, 19167, 19180, 19670, 19671, 19684, 19698, 19699, 19712,
      19726, 19727, 19740, 20202, 20203, 20216,
      20230, 20231, 20244, 20734, 20735, 20748, 20762, 20763, 20776,
      20790, 20791, 20804, 21266, 21267, 21280,
      21294, 21295, 21308, 21798, 21799, 21812, 21826, 21827, 21840,
      21854, 21855, 21868, 22330, 22331, 22344,
      39382, 39383, 39396, 39886, 39887, 39900, 39914, 39915, 39928,
      39942, 39943, 39956, 40418, 40419, 40432
   };
   struct split_layout lay = layout(3, 38, 28);

   init(&lay);
   TEST_ASSERT_NULL(samples.gaps);
   TEST_ASSERT_EQUAL_UINT(77, split_sampler_count(&samples));
   for (size_t i = 0; i < sizeof expected / sizeof *expected; i++)
      TEST_ASSERT_EQUAL_UINT64(expected[i], split_sampler_gap_at(&samples, i));
}

static void assert_unique_bounds(const struct split_layout *lay)
{
   size_t count = split_sampler_count(&samples);

   TEST_ASSERT_GREATER_THAN_UINT(1, count);
   TEST_ASSERT_EQUAL_UINT64(0, split_sampler_gap_at(&samples, 0));
   TEST_ASSERT_EQUAL_UINT64(lay->rec_cnt, split_sampler_gap_at(&samples, 1));
   for (size_t i = 0; i < count; i++)
    {
      uint64_t gap = split_sampler_gap_at(&samples, i);

      TEST_ASSERT_TRUE(gap <= lay->rec_cnt);
      for (size_t j = 0; j < i; j++)
         TEST_ASSERT_NOT_EQUAL_UINT64(gap, split_sampler_gap_at(&samples, j));
    }
}

/* Decode the physical insertion gap, rather than reproducing the projection
 * generator. A gap just after a leaf's last key belongs to that leaf. */
static unsigned int child_at(uint64_t gap, const struct split_layout *lay,
                              unsigned int depth)
{
   uint64_t leaf = (gap - 1) / (lay->leaf_up - 1);

   for (unsigned int i = depth + 1; i < lay->lay_cnt - 1; i++)
      leaf /= lay->brch_up;
   return (unsigned int)(leaf % lay->brch_up);
}

static void assert_pair_coverage(const struct split_layout *lay)
{
   unsigned int half = lay->brch_up / 2;
   const unsigned int children[] = { 0, half - 1, half, half + 1,
                                     lay->brch_up - 1 };
   unsigned int keys = lay->leaf_up - 1;
   const unsigned int positions[] = { keys - lay->leaf_up / 2,
                                      keys - lay->leaf_up / 2 + 1, keys };

   for (unsigned int a = 0; a < lay->lay_cnt - 1; a++)
      for (unsigned int b = a + 1; b < lay->lay_cnt - 1; b++)
       {
         _Bool seen[5][5][3] = { 0 };

         for (size_t i = 0; i < split_sampler_count(&samples); i++)
          {
            uint64_t gap = split_sampler_gap_at(&samples, i);
            unsigned int pa, pb, pos;

            if (gap == 0) continue;
            pa = child_at(gap, lay, a);
            pb = child_at(gap, lay, b);
            pos = (unsigned int)((gap - 1) % keys) + 1;
            for (unsigned int ra = 0; ra < 5; ra++)
               for (unsigned int rb = 0; rb < 5; rb++)
                  for (unsigned int rp = 0; rp < 3; rp++)
                     if (pa == children[ra] && pb == children[rb] &&
                         pos == positions[rp]) seen[ra][rb][rp] = 1;
          }
         for (unsigned int ra = 0; ra < 5; ra++)
            for (unsigned int rb = 0; rb < 5; rb++)
               for (unsigned int rp = 0; rp < 3; rp++)
                  /* A one-key leaf can only receive a key before its first
                   * key at the tree's left edge; not at every branch pair. */
                  if (positions[rp] != 0) TEST_ASSERT_TRUE(seen[ra][rb][rp]);
       }
}

static void test_pairwise_projection(void)
{
   const unsigned int heights[] = { 4, 7, 8 };

   for (size_t i = 0; i < sizeof heights / sizeof *heights; i++)
    {
      struct split_layout lay = layout(heights[i], 38, 28);

      init(&lay);
      TEST_ASSERT_EQUAL_UINT(heights[i] <= 7 ? 77 : 377,
                             split_sampler_count(&samples));
      assert_unique_bounds(&lay);
      assert_pair_coverage(&lay);
      split_sampler_destroy(&samples);
    }
}

static void test_deterministic_tall_samples(void)
{
   struct split_layout lay = layout(8, 4, 3);

   init(&lay);
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK, split_sampler_init(&again, &lay));
   TEST_ASSERT_EQUAL_UINT(split_sampler_count(&samples),
                          split_sampler_count(&again));
   for (size_t i = 0; i < split_sampler_count(&samples); i++)
      TEST_ASSERT_EQUAL_UINT64(split_sampler_gap_at(&samples, i),
                               split_sampler_gap_at(&again, i));
}

static void test_overlapping_roles(void)
{
   for (unsigned int keys = 1; keys <= 3; keys++)
    {
      struct split_layout lay = layout(5, 3, keys);

      init(&lay);
      TEST_ASSERT_LESS_THAN_UINT(77, split_sampler_count(&samples));
      assert_unique_bounds(&lay);
      assert_pair_coverage(&lay);
      split_sampler_destroy(&samples);
    }
}

static void test_high_dimensions_and_64_bit_gaps(void)
{
   const unsigned int heights[] = { 32, 33, 40 };

   for (size_t i = 0; i < sizeof heights / sizeof *heights; i++)
    {
      struct split_layout lay = layout(heights[i], 3, 1);
      _Bool above_u32 = 0;

      init(&lay);
      TEST_ASSERT_TRUE(split_sampler_count(&samples) <=
                        (heights[i] <= 32 ? 377u : 1877u));
      assert_unique_bounds(&lay);
      for (size_t j = 0; j < split_sampler_count(&samples); j++)
         if (split_sampler_gap_at(&samples, j) > UINT32_MAX) above_u32 = 1;
      TEST_ASSERT_TRUE(above_u32);
      split_sampler_destroy(&samples);
    }
}

static void test_invalid_geometry(void)
{
   struct split_layout lay = layout(4, 38, 28);

   lay.rec_cnt--;
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_LAYOUT, split_sampler_init(&samples, &lay));
   lay = layout(4, 38, 28);
   lay.lay_cnt = 0;
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_LAYOUT, split_sampler_init(&samples, &lay));
   lay.lay_cnt = 4;
   lay.leaf_up = 1;
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_LAYOUT, split_sampler_init(&samples, &lay));
   lay.leaf_up = 29;
   lay.brch_up = 2;
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_LAYOUT, split_sampler_init(&samples, &lay));
   TEST_ASSERT_NULL(samples.gaps);
}

static void test_geometry_overflow(void)
{
   struct split_layout lay = { 42, 2, 3, 0 };

   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_OVERFLOW,
                          split_sampler_init(&samples, &lay));
   TEST_ASSERT_EQUAL_UINT(0, split_sampler_count(&samples));
   TEST_ASSERT_NULL(samples.gaps);
}

static void test_checked_keys(void)
{
   int64_t key = 123, val = 456;

   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK,
                          split_sampler_kv(-11, 5, 17, &key, &val));
   TEST_ASSERT_EQUAL_INT64(72, key);
   TEST_ASSERT_EQUAL_INT64(144, val);
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK,
                          split_sampler_kv(INT64_MIN / 2 + 1, 2, 0, &key, &val));
   TEST_ASSERT_EQUAL_INT64(INT64_MIN, val);
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK,
                          split_sampler_kv(INT64_MAX / 2 + 1, 2, 0, &key, &val));
   TEST_ASSERT_EQUAL_INT64(INT64_MAX - 1, val);
}

static void test_key_overflow_and_invalid_intervals(void)
{
   const int64_t starts[] = { INT64_MIN, INT64_MAX, INT64_MIN / 2,
                              INT64_MAX / 2 + 2 };
   int64_t key = 123, val = 456;

   for (size_t i = 0; i < sizeof starts / sizeof *starts; i++)
      TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_KEY,
                             split_sampler_kv(starts[i], 2, 0, &key, &val));
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_KEY,
                          split_sampler_kv(0, INT64_MAX, UINT64_MAX, &key, &val));
   for (int64_t interval = -1; interval <= 1; interval++)
      TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_KEY,
                             split_sampler_kv(0, interval, 0, &key, &val));
   TEST_ASSERT_EQUAL_INT64(123, key);
   TEST_ASSERT_EQUAL_INT64(456, val);
}

int main(void)
{
   UNITY_BEGIN();
   RUN_TEST(test_exhaustive_small_trees);
   RUN_TEST(test_three_level_sequence);
   RUN_TEST(test_pairwise_projection);
   RUN_TEST(test_deterministic_tall_samples);
   RUN_TEST(test_overlapping_roles);
   RUN_TEST(test_high_dimensions_and_64_bit_gaps);
   RUN_TEST(test_invalid_geometry);
   RUN_TEST(test_geometry_overflow);
   RUN_TEST(test_checked_keys);
   RUN_TEST(test_key_overflow_and_invalid_intervals);
   return UNITY_END();
}
