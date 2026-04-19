#include <stdio.h>
#include "unity.h"
#include "header.h"
#include "unity_internals.h"

void setUp(void) { }

void tearDown(void) { }


int main(void)
{
   puts("Test Unit: fflush");
   UNITY_BEGIN();

   RUN_TEST(test_bptr_create);

   return UNITY_END();
}
