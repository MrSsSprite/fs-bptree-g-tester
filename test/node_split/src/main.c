/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
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
 * Building a fixture is a precondition shared by the cases, not a case of its
 * own: this is a plain function returning the status of `temp_full_generate'
 * (`temp_full_strerror' names it), so that a case can turn a non-OK status into
 * a failure of its own instead of the generator asserting on its behalf.
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
   puts("Test Unit: node_split");
   UNITY_BEGIN();

   RUN_TEST(test_temp);
   //RUN_TEST(...);

   return UNITY_END();
}


void test_temp(void)
{
   struct bptr *bptr;
   int status = gen_full_fixtures();

   TEST_ASSERT_EQUAL_INT_MESSAGE(TEMP_FULL_OK, status,
                                 temp_full_strerror(status));

   bptr = bptr_load("bptr_files/temp/full/1-0-16.bptr", 256, &cmp_i64);
   TEST_ASSERT_NOT_NULL_MESSAGE(bptr, "failed to load bptr");
   temp_full_verify(bptr, 1, 0, 0x10, 0, 0, 0);
   TEST_ASSERT_EQUAL(BPTR_E_SUCCESS, bptr_unload(bptr));
}
/*--------------------------------- MAIN END ---------------------------------*/
