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

/*----------------------------------- MAIN -----------------------------------*/
int main(void)
{
   puts("Test Unit: node_split");
   UNITY_BEGIN();

   if (temp_full_generate(1, 0, 0x10, 1, 512))
      perror("temp_full_generate");
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
