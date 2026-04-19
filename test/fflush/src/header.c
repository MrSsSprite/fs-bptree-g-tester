/*----------------------------- Private Includes -----------------------------*/
#include "header.h"
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"
#include <stdio.h>
/*--------------------------- Private Includes END ---------------------------*/


/*----------------------------- Private Structs ------------------------------*/
struct bptr_temp
{
   const char *fnm;
   _Bool is_lite;
   uint32_t node_sz;
   uint16_t key_sz, val_sz;
   int (*cmp)(const void *, const void *);
};
/*--------------------------- Private Structs END ----------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
int cmp_u32(const void *lhs, const void *rhs)
{ return (const uint32_t *)lhs - (const uint32_t *)rhs; }
int cmp_u64(const void *lhs, const void *rhs)
{ return (const uint64_t *)lhs - (const uint64_t *)rhs; }
void _bptr_create(struct bptr_temp *template);
void _bptr_load_check(struct bptr_temp *template);
/*-------------------- Private Function Declarations END ---------------------*/


/*---------------------------- Private Variables -----------------------------*/
struct bptr_temp lite_temps[] =
{
   { "lite_DEF_u32_u32.bptr", 1, BPTR_NODE_BYTE_DEFAULT, sizeof(uint32_t), sizeof(uint32_t), cmp_u32 },
   { "lite_128_u64_u64.bptr", 1, 128, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
   { "lite_1024_u64_u64.bptr", 1, 1024, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
   { "lite_2048_u64_u64.bptr", 1, 2048, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
};
size_t lite_temps_sz = sizeof(lite_temps)/sizeof(*lite_temps);
struct bptr_temp norm_temps[] =
{
   { "norm_DEF_u32_u32.bptr", 0, BPTR_NODE_BYTE_DEFAULT, sizeof(uint32_t), sizeof(uint32_t), cmp_u32 },
   { "norm_128_u64_u64.bptr", 0, 128, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
   { "norm_1024_u64_u64.bptr", 0, 1024, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
   { "norm_2048_u64_u64.bptr", 0, 2048, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
};
size_t norm_temps_sz = sizeof(norm_temps)/sizeof(*norm_temps);
/*-------------------------- Private Variables END ---------------------------*/


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
   for (size_t ti = 0; ti < sizeof(temp_matrix)/sizeof(*temp_matrix); ti++)
    {
      struct bptr_temp *temp_it = temp_matrix[ti];
      puts(mes[ti]);
      for (size_t i = 0; i < temp_matrix_sz[ti]; i++)
       {
         printf("\t\tStarting Test: %s...", temp_it[i].fnm);
         _bptr_create(temp_it + i);
         puts("Succeeded.");
       }
    }

   // Load and check
   for (size_t ti = 0; ti < sizeof(temp_matrix)/sizeof(*temp_matrix); ti++)
    {
      struct bptr_temp *temp_it = temp_matrix[ti];
      puts(mes[ti]);
      for (size_t i = 0; i < temp_matrix_sz[ti]; i++)
       {
         _bptr_load_check(temp_it + i);
         TEST_ASSERT_MESSAGE(remove(temp_it[i].fnm) == 0,
                             "file remove failure");
       }
    }
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
void _bptr_create(struct bptr_temp *template)
{
   struct bptr *bptr = bptr_init(template->fnm, template->is_lite,
                                 template->node_sz, template->key_sz,
                                 template->val_sz, template->cmp);
   TEST_ASSERT(bptr);
   TEST_ASSERT(bptr_unload(bptr) == BPTR_E_SUCCESS);
}


void _bptr_load_check(struct bptr_temp *template)
{
   struct bptr *bptr = bptr_load(template->fnm, template->cmp);

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
