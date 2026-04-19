/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include "unity.h"
#include "header.h"
#include "unity_internals.h"
/*--------------------------- Private Includes END ---------------------------*/

/*------------------------------- Unity Setup --------------------------------*/
void setUp(void) { }
void tearDown(void) { }
/*----------------------------- Unity Setup END ------------------------------*/


/*----------------------------------- MAIN -----------------------------------*/
int main(void)
{
   puts("Test Unit: fflush");
   UNITY_BEGIN();

   RUN_TEST(test_bptr_create);

   return UNITY_END();
}
/*--------------------------------- MAIN END ---------------------------------*/
