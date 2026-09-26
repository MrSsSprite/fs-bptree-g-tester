/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include "unity.h"
#include "unity_internals.h"
#include "temp_full.h"
/*--------------------------- Private Includes END ---------------------------*/

/*------------------------------- Unity Setup --------------------------------*/
void setUp(void) { }

void tearDown(void)
{
   /* a Unity assertion inside `temp_full_generate' aborts the test without
    * running the generator's own error path, so drop whatever fixture it left
    * behind: the exists-short-circuit would otherwise serve it as a good one
    * and every later run would pass */
   temp_full_discard();
}
/*----------------------------- Unity Setup END ------------------------------*/

void test_temp_generate(void);

/*----------------------------------- MAIN -----------------------------------*/
int main(void)
{
   puts("Test Unit: node_split");
   UNITY_BEGIN();

   RUN_TEST(test_temp_generate);
   //RUN_TEST(...);

   return UNITY_END();
}


void test_temp_generate(void)
{
   puts("Generating full tree fixtures...");

   TEST_ASSERT_EQUAL_INT_MESSAGE(0, temp_full_generate(1, 0, 0x10, 1, 512),
                                 "temp_full_generate: lay_cnt = 1");
   TEST_ASSERT_EQUAL_INT_MESSAGE(0, temp_full_generate(2, 0, 0x10, 1, 512),
                                 "temp_full_generate: lay_cnt = 2");
   TEST_ASSERT_EQUAL_INT_MESSAGE(0, temp_full_generate(3, 0, 0x10, 1, 512),
                                 "temp_full_generate: lay_cnt = 3");

   puts("done.");
}
/*--------------------------------- MAIN END ---------------------------------*/
