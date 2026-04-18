#include "header.h"
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"


int cmp_i32(const void *lhs, const void *rhs)
{ return (const uint32_t *)lhs - (const uint32_t *)rhs; }


void test_direct_flush(void)
{
   struct bptr *bptr = bptr_init("direct_flush.bptr", 1, BPTR_NODE_BYTE_DEFAULT,
                                 sizeof(uint32_t), sizeof(uint32_t), cmp_i32);
   TEST_ASSERT(bptr);
   TEST_ASSERT(bptr_unload(bptr));
}
