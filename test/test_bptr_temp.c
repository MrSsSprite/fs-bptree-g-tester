/*----------------------------- Private Includes -----------------------------*/
#include "test_bptr_temp.h"
#include "bptr_internal.h"
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
int cmp_u32(const void *lhs, const void *rhs)
{ return (const uint32_t *)lhs - (const uint32_t *)rhs; }
int cmp_u64(const void *lhs, const void *rhs)
{ return (const uint64_t *)lhs - (const uint64_t *)rhs; }
/*-------------------- Private Function Declarations END ---------------------*/


/*----------------------------- Public Variables -----------------------------*/
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
/*--------------------------- Public Variables END ---------------------------*/
