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
/*-------------------- Private Function Declarations END ---------------------*/


/*---------------------------- Private Variables -----------------------------*/
struct bptr_temp lite_temps[] =
{
   { "lite_DEF_u32_u32.bptr", 1, BPTR_NODE_BYTE_DEFAULT, sizeof(uint32_t), sizeof(uint32_t), cmp_u32 },
   { "lite_128_u64_u64.bptr", 1, 128, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
   { "lite_1024_u64_u64.bptr", 1, 1024, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
   { "lite_2048_u64_u64.bptr", 1, 2048, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
};
struct bptr_temp norm_temps[] =
{
   { "lite_DEF_u32_u32.bptr", 0, BPTR_NODE_BYTE_DEFAULT, sizeof(uint32_t), sizeof(uint32_t), cmp_u32 },
   { "lite_128_u64_u64.bptr", 0, 128, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
   { "lite_1024_u64_u64.bptr", 0, 1024, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
   { "lite_2048_u64_u64.bptr", 0, 2048, sizeof(uint64_t), sizeof(uint64_t), cmp_u64 },
};
/*-------------------------- Private Variables END ---------------------------*/


/*----------------------------- Public Functions -----------------------------*/
void test_bptr_create(void)
{
   puts("\tLite Templates:");
   for (size_t i = 0; i < sizeof(lite_temps)/sizeof(*lite_temps); i++)
    {
      printf("\t\tStarting Test: %s...", lite_temps[i].fnm);
      _bptr_create(lite_temps + i);
      puts("Succeeded.");
      TEST_ASSERT(remove(lite_temps[i].fnm) == 0);
    }
   puts("\tNorm Templates:");
   for (size_t i = 0; i < sizeof(norm_temps)/sizeof(*norm_temps); i++)
    {
      printf("\t\tStarting Test: %s...", norm_temps[i].fnm);
      _bptr_create(norm_temps + i);
      puts("Succeeded.");
      TEST_ASSERT(remove(norm_temps[i].fnm) == 0);
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
/*-------------------------- Private Functions END ---------------------------*/
