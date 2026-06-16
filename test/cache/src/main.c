/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include "unity.h"
#include "cache.h"
/*--------------------------- Private Includes END ---------------------------*/

/*------------------------------- Unity Setup --------------------------------*/
void setUp(void) { }
void tearDown(void) { }
/*----------------------------- Unity Setup END ------------------------------*/


/*----------------------------------- MAIN -----------------------------------*/
int main(void)
{
   puts("Test Unit: cache");
   UNITY_BEGIN();

   RUN_TEST(test_cache_init);
   RUN_TEST(test_cache_node_lifecycle);
   RUN_TEST(test_cache_data_persistence);
   RUN_TEST(test_cache_capacity_boundaries);

   return UNITY_END();
}
/*--------------------------------- MAIN END ---------------------------------*/
