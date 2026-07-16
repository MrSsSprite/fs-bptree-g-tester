/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include "unity.h"
#include "simple.h"
#include "brch_sp.h"
#include "brch_sp_casc.h"
#include "brch_sp_part_casc.h"
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------- Unity Setup --------------------------------*/
void setUp(void) { }
void tearDown(void) { }
/*----------------------------- Unity Setup END ------------------------------*/


/*----------------------------------- MAIN -----------------------------------*/
int main(void)
{
   puts("Test Unit: node_split");
   UNITY_BEGIN();

   RUN_TEST(test_simp_split);
   RUN_TEST(test_brch_split);
   RUN_TEST(test_casc_brch_split);
   RUN_TEST(test_part_casc_brch_split);

   return UNITY_END();
}
/*--------------------------------- MAIN END ---------------------------------*/
