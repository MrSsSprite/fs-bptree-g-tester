/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "unity.h"
#include "unity_internals.h"
#include "temp_full.h"
#include "bptree.h"
/*--------------------------- Private Includes END ---------------------------*/

/*------------------------------- Unity Setup --------------------------------*/
void setUp(void) { }

void tearDown(void) { }
/*----------------------------- Unity Setup END ------------------------------*/

void test_temp(void);

/*----------------------------- Fixture Utility ------------------------------*/
/**
 * @brief   One template the split cases are built on
 *
 * The shape a template is generated with.  `gen_full_fixtures' writes one
 * image per entry and `test_temp' verifies one per entry, so the two cannot
 * drift apart: every template a case may instantiate is a template that has
 * been checked first.
 */
struct full_fixture
{
   unsigned int lay_cnt;   /* number of levels; 1 yields a single leaf */
   int64_t      st;        /* first key */
   int64_t      interval;  /* distance between two successive keys */
   _Bool        is_lite;   /* use the 4-byte child pointer layout */
   uint32_t     node_size; /* size of a node in bytes */
};

/* the images of the unit: 1, 2 and 3 levels tall, keys starting at 0 and
 * stepping by 0x10, in the default lite 512-byte layout */
static const struct full_fixture FULL_FIXTURES[] =
{
   { 1, 0, 0x10, 1, 512 },
   { 2, 0, 0x10, 1, 512 },
   { 3, 0, 0x10, 1, 512 },
};

#define FULL_FIXTURE_CNT (sizeof (FULL_FIXTURES) / sizeof (FULL_FIXTURES[0]))

/**
 * @brief   Generate every template the cases below load
 *
 * A tester utility rather than a case of its own: `main' runs it directly,
 * before `UNITY_BEGIN', and stops the run when it fails.  It reports through
 * its return value -- named by `temp_full_strerror' -- instead of through a
 * Unity assertion, which would need an abort frame that does not exist outside
 * a case.
 *
 * @return  TEMP_FULL_OK when every template exists; the status of the first
 *          generation that failed otherwise
 */
static int gen_full_fixtures(void)
{
   for (size_t i = 0; i < FULL_FIXTURE_CNT; i++)
    {
      const struct full_fixture *fx = &FULL_FIXTURES[i];
      int status = temp_full_generate(fx->lay_cnt, fx->st, fx->interval,
                                      fx->is_lite, fx->node_size);

      if (status != TEMP_FULL_OK) return status;
    }

   return TEMP_FULL_OK;
}
/*--------------------------- Fixture Utility END ----------------------------*/

/*----------------------------------- MAIN -----------------------------------*/
int main(void)
{
   int status;

   puts("Test Unit: node_split");

   /* the fixtures the cases load are an input of this unit, not a case: build
    * them here, and stop the run when they cannot be built rather than let a
    * case discover a missing image */
   status = gen_full_fixtures();
   if (status != TEMP_FULL_OK)
    {
      fprintf(stderr, "node_split: cannot generate the fixtures: %s\n",
              temp_full_strerror(status));
      return EXIT_FAILURE;
    }

   UNITY_BEGIN();
   /* the template guard: it proves the images the cases below instantiate, so
    * it has to stay the first case of the unit */
   RUN_TEST(test_temp);
   //RUN_TEST(...);

   return UNITY_END();
}


/**
 * @brief   Check every generated template before a modification is attempted
 *
 * The guard on the generator.  A split case is only meaningful once the image
 * it instantiates is known to be correct, so each template is loaded and walked
 * by `temp_full_verify' here, ahead of the cases that will modify one.  A
 * defect aborts the case -- and with it the run -- before anything is
 * instantiated.
 *
 * @note  Loading and unloading an image rewrites its header block: the library
 *        flushes that whole block from the shared scratch buffer it marshals
 *        nodes through, so the bytes past the header end up holding whatever
 *        node image was left there -- they change as soon as that leftover
 *        differs, though a load that fetches no node leaves the file alone.
 *        The header itself and every node block are stable; a fixture must not
 *        be pinned by the hash of the whole file.
 */
void test_temp(void)
{
   char path[PATH_MAX], msg[PATH_MAX + 32];

   for (size_t i = 0; i < FULL_FIXTURE_CNT; i++)
    {
      const struct full_fixture *fx = &FULL_FIXTURES[i];
      struct bptr *bptr;

      temp_full_path(path, sizeof path, fx->lay_cnt, fx->st, fx->interval);
      snprintf(msg, sizeof msg, "failed to load %s", path);
      /* loader cache only: the verifier reloads nodes as it walks the image */
      bptr = bptr_load(path, 256, &cmp_i64);
      TEST_ASSERT_NOT_NULL_MESSAGE(bptr, msg);

      /* name the template under check: `temp_full_verify' asserts with fixed
       * messages, so this line is what tells which image carried a defect */
      printf("  template %s\n", path);
      temp_full_verify(bptr, fx->lay_cnt, fx->st, fx->interval, 0, 0, 0);
      TEST_ASSERT_EQUAL(BPTR_E_SUCCESS, bptr_unload(bptr));
    }
}
/*--------------------------------- MAIN END ---------------------------------*/
