/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include "unity.h"
#include "cache.h"
#include "fixtures.h"
/*--------------------------- Private Includes END ---------------------------*/

/*------------------------------- Unity Setup --------------------------------*/
void setUp(void) { }
void tearDown(void)
{
   int fixture_status = cache_fixture_discard();
   int collision_status = cache_collisions_cleanup();
   int persistence_status = cache_persistence_cleanup();

   if (fixture_status || collision_status || persistence_status)
    {
      fputs("cache: fixture cleanup failed\n", stderr);
      if (!Unity.CurrentTestFailed && !Unity.CurrentTestIgnored)
         TEST_FAIL_MESSAGE("fixture cleanup failed");
    }
}
/*----------------------------- Unity Setup END ------------------------------*/


/*----------------------------------- MAIN -----------------------------------*/
int main(void)
{
   int result;

   if (cache_fixtures_begin() != 0)
      return EXIT_FAILURE;
   puts("Test Unit: cache");
   UNITY_BEGIN();

   RUN_TEST(test_cache_init);
   RUN_TEST(test_cache_node_lifecycle);
   RUN_TEST(test_cache_data_persistence);
   RUN_TEST(test_cache_fetch_persistence);
   RUN_TEST(test_cache_capacity_boundaries);
   RUN_TEST(test_cache_fifo_eviction);
   RUN_TEST(test_cache_hash_collisions);
   RUN_TEST(test_cache_hash_collision_deletion);

   result = UNITY_END();
   if (cache_fixtures_end() != 0 && result == 0)
      result = EXIT_FAILURE;
   return result;
}
/*--------------------------------- MAIN END ---------------------------------*/
