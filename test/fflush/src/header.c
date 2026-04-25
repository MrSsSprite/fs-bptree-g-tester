/*----------------------------- Private Includes -----------------------------*/
#include "header.h"
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"
#include "test_bptr_temp.h"
#include "test_bptr_setup.h"
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
void _bptr_load_check(struct bptr_temp *template);
/*-------------------- Private Function Declarations END ---------------------*/


/*----------------------------- Public Functions -----------------------------*/
void test_bptr_create(void)
{
   struct bptr_temp *temp_matrix[] =
    { lite_temps, norm_temps, };
   size_t temp_matrix_sz[] =
    { lite_temps_sz, norm_temps_sz, };
   const char *mes[sizeof(temp_matrix)/sizeof(*temp_matrix)] =
    { "\tLite Templates:", "\tNorm Templates:" };

   // Initialize Tree
   puts("Initialize Tree:");
   for (size_t ti = 0; ti < sizeof(temp_matrix)/sizeof(*temp_matrix); ti++)
    {
      struct bptr_temp *temp_it = temp_matrix[ti];
      struct bptr *bptr;
      puts(mes[ti]);
      for (size_t i = 0; i < temp_matrix_sz[ti]; i++)
       {
         printf("\t\tStarting Test: %s...", temp_it[i].fnm);
         bptr = _bptr_create(temp_it + i);
         TEST_ASSERT_MESSAGE(bptr_unload(bptr) == BPTR_E_SUCCESS,
                             "bptr_unload failure");
         puts("Succeeded.");
       }
    }

   // Load and check
   puts("Load Tree:");
   for (size_t ti = 0; ti < sizeof(temp_matrix)/sizeof(*temp_matrix); ti++)
    {
      struct bptr_temp *temp_it = temp_matrix[ti];
      puts(mes[ti]);
      for (size_t i = 0; i < temp_matrix_sz[ti]; i++)
       {
         char path[256];
         printf("\t\tStarting Test: %s...", temp_it[i].fnm);
         _bptr_load_check(temp_it + i);
         puts("Succeeded.");
         _bptr_path(path, sizeof(path), temp_it[i].fnm);
         TEST_ASSERT_MESSAGE(remove(path) == 0,
                             "file remove failure");
       }
    }
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
void _bptr_load_check(struct bptr_temp *template)
{
   char path[256];
   _bptr_path(path, sizeof(path), template->fnm);
   struct bptr *bptr = bptr_load(path, template->cmp);

   TEST_ASSERT_MESSAGE(bptr, "bptr_load failure");
   TEST_ASSERT_EQUAL(bptr->compare, template->cmp);
   TEST_ASSERT_EQUAL(bptr->free_list.cnt, 0);
   TEST_ASSERT_EQUAL(bptr->is_lite, template->is_lite);
   TEST_ASSERT_EQUAL(bptr->node_size, template->node_sz);
   TEST_ASSERT_EQUAL(bptr->key_size, template->key_sz);
   TEST_ASSERT_EQUAL(bptr->value_size, template->val_sz);

   TEST_ASSERT_MESSAGE(bptr_unload(bptr) == 0, "bptr_unload failure");
}
/*-------------------------- Private Functions END ---------------------------*/
