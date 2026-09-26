/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include <stdlib.h>
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
 * @brief   Generate the full tree fixtures the cases below load
 *
 * A tester utility rather than a case of its own: `main' runs it directly,
 * before `UNITY_BEGIN', and stops the run when it fails.  It reports through
 * its return value -- named by `temp_full_strerror' -- instead of through a
 * Unity assertion, which would need an abort frame that does not exist outside
 * a case.
 *
 * @return  TEMP_FULL_OK when every fixture exists; the status of the first
 *          generation that failed otherwise
 */
static int gen_full_fixtures(void)
{
   /* the images the split cases are built on: 1, 2 and 3 levels tall, keys
    * starting at 0 and stepping by 0x10, in the default lite 512-byte layout */
   for (unsigned int lay_cnt = 1; lay_cnt <= 3; lay_cnt++)
    {
      int status = temp_full_generate(lay_cnt, 0, 0x10, 1, 512);

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
   RUN_TEST(test_temp);
   //RUN_TEST(...);

   return UNITY_END();
}


void test_temp(void)
{
   struct bptr *bptr = bptr_load("bptr_files/temp/full/1-0-16.bptr", 256,
                                 &cmp_i64);

   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   temp_full_verify(bptr, 1, 0, 0x10, 0, 0, 0);
   TEST_ASSERT_EQUAL(BPTR_E_SUCCESS, bptr_unload(bptr));
}
/*--------------------------------- MAIN END ---------------------------------*/
