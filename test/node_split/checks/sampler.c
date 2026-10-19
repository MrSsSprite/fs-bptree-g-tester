#include "../src/split_sampler.h"
#include "unity.h"
#include <limits.h>

static struct split_sampler samples, again;
static struct temp_shape shape;

void setUp(void) { }
void tearDown(void)
{
   split_sampler_destroy(&samples);
   split_sampler_destroy(&again);
   temp_shape_free(&shape);
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

static void override(const char *spec)
{ TEST_ASSERT_EQUAL_INT(0, temp_shape_add(&shape, spec)); }

static struct split_layout custom_layout(unsigned int height, _Bool lite,
                                          uint32_t node_size)
{
   struct temp_shape_counts counts, leaf, two;
   struct split_layout lay;

   TEST_ASSERT_EQUAL_INT(0, temp_shape_count(&shape, height, lite, node_size, &counts));
   TEST_ASSERT_EQUAL_INT(0, temp_shape_count(NULL, 1, lite, node_size, &leaf));
   TEST_ASSERT_EQUAL_INT(0, temp_shape_count(NULL, 2, lite, node_size, &two));
   lay = (struct split_layout){ height, (unsigned int)leaf.records + 1,
                               (unsigned int)(two.records / leaf.records),
                               counts.records };
   return lay;
}

static void custom_init(unsigned int height, _Bool lite, uint32_t node_size)
{
   struct split_layout lay = custom_layout(height, lite, node_size);

   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK,
      split_sampler_init_shape(&samples, &lay, &shape, lite, node_size));
}

static void assert_expect(uint64_t gap, unsigned int height, uint64_t nodes,
                           uint64_t leaves, unsigned int stop)
{
   struct temp_verify_expect actual = {0};
   unsigned int depth = 123;

   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK,
      split_sampler_expect(&samples, gap, &actual, &depth));
   TEST_ASSERT_EQUAL_UINT(height, actual.height);
   TEST_ASSERT_EQUAL_UINT64(nodes, actual.nodes);
   TEST_ASSERT_EQUAL_UINT64(leaves, actual.leaves);
   TEST_ASSERT_EQUAL_UINT(stop, depth);
}

static _Bool has_gap(uint64_t gap)
{
   for (size_t i = 0; i < samples.count; i++)
      if (split_sampler_gap_at(&samples, i) == gap) return 1;
   return 0;
}

/* Independent compact fixture totals, including a full cascade outside the
 * partially occupied child, lock the expectation calculator's arithmetic. */
static void test_stopped_cascade_expectations(void)
{
   const struct {
      unsigned int height;
      _Bool lite;
      const char *spec;
      uint64_t records, nodes, leaves, stopped_nodes;
      unsigned int stop;
   } cases[] = {
      { 2, 1, "root=4", 20, 6, 5, 7, 0 },
      { 2, 0, "root=2", 12, 4, 3, 5, 0 },
      { 3, 1, "0=4", 140, 42, 35, 43, 1 },
      { 3, 0, "0=2", 60, 20, 15, 21, 1 },
      { 4, 1, "0=4", 840, 252, 210, 254, 1 },
      { 4, 0, "0=2", 240, 80, 60, 82, 1 }
   };

   for (size_t c = 0; c < sizeof cases / sizeof *cases; c++)
    {
      override(cases[c].spec);
      custom_init(cases[c].height, cases[c].lite, 128);
      TEST_ASSERT_EQUAL_UINT64(cases[c].records, samples.original.records);
      TEST_ASSERT_EQUAL_UINT64(cases[c].nodes, samples.original.nodes);
      TEST_ASSERT_EQUAL_UINT64(cases[c].leaves, samples.original.leaves);
      TEST_ASSERT_EQUAL_UINT64(0, samples.nonfull_leaves);
      assert_expect(1, cases[c].height, cases[c].stopped_nodes,
                     cases[c].leaves + 1, cases[c].stop);
      if (cases[c].height > 2)
         assert_expect(cases[c].records, cases[c].height + 1,
                        cases[c].nodes + cases[c].height + 1,
                        cases[c].leaves + 1, UINT_MAX);
      split_sampler_destroy(&samples);
      temp_shape_free(&shape);
    }
}

static void test_custom_exhaustion_and_boundary_ownership(void)
{
   struct temp_verify_expect expected = { 8, 9, 10 };
   unsigned int stop = 17;

   override("root=1");
   override("0=2");
   custom_init(2, 1, 128);
   TEST_ASSERT_NULL(samples.gaps);
   TEST_ASSERT_EQUAL_UINT64(1, samples.nonfull_leaves);
   TEST_ASSERT_EQUAL_UINT(4, samples.count);
   for (size_t i = 0; i < samples.count; i++)
      TEST_ASSERT_EQUAL_UINT64(i + 3, split_sampler_gap_at(&samples, i));
   /* Gap 2 is immediately before the first key of the full right leaf, but
    * the search routes that key into the non-full left leaf. */
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_E_LAYOUT,
      split_sampler_expect(&samples, 2, &expected, &stop));
   TEST_ASSERT_EQUAL_UINT(8, expected.height);
   TEST_ASSERT_EQUAL_UINT64(9, expected.nodes);
   TEST_ASSERT_EQUAL_UINT64(10, expected.leaves);
   TEST_ASSERT_EQUAL_UINT(17, stop);
   assert_expect(3, 2, 4, 3, 0);
   split_sampler_destroy(&samples);
   temp_shape_free(&shape);

   override("root=1");
   override("1=2");
   custom_init(2, 1, 128);
   TEST_ASSERT_EQUAL_UINT(5, samples.count);
   for (size_t i = 0; i < samples.count; i++)
      TEST_ASSERT_EQUAL_UINT64(i, split_sampler_gap_at(&samples, i));
   assert_expect(4, 2, 4, 3, 0);
}

static void test_no_full_leaves_and_root_exception(void)
{
   override("root=1");
   custom_init(1, 1, 128);
   TEST_ASSERT_EQUAL_UINT64(1, samples.original.records);
   TEST_ASSERT_EQUAL_UINT64(1, samples.nonfull_leaves);
   TEST_ASSERT_EQUAL_UINT(0, samples.count);
   split_sampler_destroy(&samples);
   temp_shape_free(&shape);

   override("root=1");
   override("0=2");
   override("1=3");
   custom_init(2, 1, 128);
   TEST_ASSERT_EQUAL_UINT64(2, samples.nonfull_leaves);
   TEST_ASSERT_EQUAL_UINT(0, samples.count);
   split_sampler_destroy(&samples);
   temp_shape_free(&shape);

   override("root=4");
   custom_init(1, 1, 128);
   TEST_ASSERT_EQUAL_UINT(5, samples.count);
   assert_expect(0, 2, 3, 2, UINT_MAX);
   assert_expect(4, 2, 3, 2, UINT_MAX);
}

static void test_nested_anchors_and_leaf_neighbors(void)
{
   struct split_layout lay;
   _Bool stopped_deep = 0, stopped_high = 0, new_root = 0;

   /* Child 1 is absent from the five roles of a six-child full branch.
    * The deep anchor is consequently needed to sample path 1.1.1. */
   override("1=4");
   override("1.1.1=4");
   override("1.1.1.1=2");
   custom_init(5, 1, 128);
   TEST_ASSERT_EQUAL_UINT64(5034, samples.original.records);
   TEST_ASSERT_EQUAL_UINT64(1511, samples.original.nodes);
   TEST_ASSERT_EQUAL_UINT64(1259, samples.original.leaves);
   TEST_ASSERT_EQUAL_UINT64(1, samples.nonfull_leaves);
   TEST_ASSERT_FALSE(has_gap(1037));
   TEST_ASSERT_FALSE(has_gap(1038));
   TEST_ASSERT_TRUE(has_gap(1034));
   TEST_ASSERT_TRUE(has_gap(1035));
   TEST_ASSERT_TRUE(has_gap(1036));
   TEST_ASSERT_TRUE(has_gap(1040));
   TEST_ASSERT_TRUE(has_gap(1041));
   TEST_ASSERT_TRUE(has_gap(1042));
   for (size_t i = 0; i < samples.count; i++)
    {
      uint64_t gap = split_sampler_gap_at(&samples, i);
      if (gap > 1032 && gap <= 1050)
       { assert_expect(gap, 5, 1512, 1260, 3); stopped_deep = 1; }
      else if (gap > 864 && gap <= 1578)
       { assert_expect(gap, 5, 1514, 1260, 1); stopped_high = 1; }
      else
       { assert_expect(gap, 6, 1517, 1260, UINT_MAX); new_root = 1; }
      for (size_t j = 0; j < i; j++)
         TEST_ASSERT_NOT_EQUAL_UINT64(gap, split_sampler_gap_at(&samples, j));
    }
   TEST_ASSERT_TRUE(stopped_deep && stopped_high && new_root);
   lay = custom_layout(5, 1, 128);
   TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK,
      split_sampler_init_shape(&again, &lay, &shape, 1, 128));
   TEST_ASSERT_EQUAL_UINT(samples.count, again.count);
   for (size_t i = 0; i < samples.count; i++)
      TEST_ASSERT_EQUAL_UINT64(split_sampler_gap_at(&samples, i),
                               split_sampler_gap_at(&again, i));
}

static void test_one_key_custom_leaves(void)
{
   override("root=1");
   override("1.0=1");
   custom_init(5, 1, 92);
   TEST_ASSERT_EQUAL_UINT64(0, samples.nonfull_leaves);
   TEST_ASSERT_TRUE(has_gap(0));
   for (size_t i = 0; i < samples.count; i++)
    {
      struct temp_verify_expect out;
      unsigned int depth;
      TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK,
         split_sampler_expect(&samples, split_sampler_gap_at(&samples, i),
                               &out, &depth));
   }
   TEST_ASSERT_TRUE(samples.count < 77);
}

static void test_sparse_shape_outgrows_full_geometry(void)
{
   _Bool wide = 0;
   const char *specs[] = { "root=1", "0=2", "1=2", "0.0=2", "0.1=2",
                           "0.2=2", "1.0=2", "1.1=2", "1.2=2" };

   for (size_t i = 0; i < sizeof specs / sizeof *specs; i++) override(specs[i]);
   custom_init(26, 1, 128);
   TEST_ASSERT_EQUAL_UINT64(UINT64_C(9476762676643233792), samples.original.records);
   TEST_ASSERT_EQUAL_UINT64(UINT64_C(2843028802992970143), samples.original.nodes);
   TEST_ASSERT_EQUAL_UINT64(UINT64_C(2369190669160808448), samples.original.leaves);
   TEST_ASSERT_TRUE(samples.count < 4000);
   for (size_t i = 0; i < samples.count; i++)
    {
      struct temp_verify_expect out;
      unsigned int depth;
      uint64_t gap = split_sampler_gap_at(&samples, i);
      if (gap > UINT32_MAX) wide = 1;
      TEST_ASSERT_EQUAL_INT(SPLIT_SAMPLE_OK,
         split_sampler_expect(&samples, gap, &out, &depth));
      TEST_ASSERT_EQUAL_UINT(2, depth);
      TEST_ASSERT_EQUAL_UINT(26, out.height);
      TEST_ASSERT_EQUAL_UINT64(samples.original.nodes + 23, out.nodes);
      TEST_ASSERT_EQUAL_UINT64(samples.original.leaves + 1, out.leaves);
   }
   TEST_ASSERT_TRUE(wide);
}

static void test_full_expectations_remain_available(void)
{
   struct split_layout lay = layout(3, 6, 4);

   init(&lay);
   TEST_ASSERT_EQUAL_UINT64(43, samples.original.nodes);
   TEST_ASSERT_EQUAL_UINT64(36, samples.original.leaves);
   assert_expect(0, 4, 47, 37, UINT_MAX);
   assert_expect(144, 4, 47, 37, UINT_MAX);
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
   RUN_TEST(test_stopped_cascade_expectations);
   RUN_TEST(test_custom_exhaustion_and_boundary_ownership);
   RUN_TEST(test_no_full_leaves_and_root_exception);
   RUN_TEST(test_nested_anchors_and_leaf_neighbors);
   RUN_TEST(test_one_key_custom_leaves);
   RUN_TEST(test_sparse_shape_outgrows_full_geometry);
   RUN_TEST(test_full_expectations_remain_available);
   return UNITY_END();
}
