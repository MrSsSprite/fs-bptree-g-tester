/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include "unity.h"
#include "simple.h"
#include "brch_sp.h"
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

   return UNITY_END();
}
/*--------------------------------- MAIN END ---------------------------------*/
