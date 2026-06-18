/*----------------------------- Private Includes -----------------------------*/
#include "test_bptr_temp.h"
#include "bptr_internal.h"
#include "bptr_node.h"
#include <string.h>
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Private Macros ------------------------------*/
#define _key_ins_i64__generate(T, Suffix) \
void _key_ins_i64__##Suffix(struct bptr_node *node, int64_t key, size_t idx) \
{ \
   if (idx < node->key_count) \
      memmove((T*)node->keys + idx + 1, (T*)node->keys + idx, \
              (node->key_count - idx) * sizeof(T)); \
   ((T*)node->keys)[idx] = key; \
}
#define _val_ins_i64__generate(T, Suffix) \
void _val_ins_i64__##Suffix(struct bptr_node *node, int64_t val, size_t idx) \
{ \
   size_t val_cnt = _node_val_cnt(node); \
   if (idx < val_cnt) \
      memmove((T*)node->vals + idx + 1, (T*)node->vals + idx, \
              (val_cnt - idx) * sizeof(T)); \
   ((T*)node->vals)[idx] = val; \
}

#define _kv_ins_i64__generate(T, S) \
   _key_ins_i64__generate(T, S) _val_ins_i64__generate(T, S)


#define _key_ers__generate(T, S) \
void _key_ers__##S(struct bptr_node *node, size_t idx) \
{ \
   if (idx < node->key_count) \
      memmove((T*)node->keys, (T*)node->keys + 1, \
              (node->key_count - idx - 1) * sizeof(T)); \
}
#define _val_ers__generate(T, S) \
void _val_ers__##S(struct bptr_node *node, size_t idx) \
{ \
   size_t val_cnt = _node_val_cnt(node); \
   if (idx < val_cnt) \
      memmove((T*)node->vals, (T*)node->vals + 1, \
              (val_cnt - idx - 1) * sizeof(T)); \
}

#define _kv_ers__generate(T, S) \
   _key_ers__generate(T, S) _val_ers__generate(T, S)


#define _kv_tools_i64__generate(T, S) \
   _kv_ins_i64__generate(T, S); \
   _kv_ers__generate(T, S);


#define _wrapper_tools_i64__generate(T, S) \
void *_wrapper_tools_i64__##S(int64_t inpt) \
{ \
   static T buffer[2]; \
   static uint_fast8_t buf_it = 0; \
   buffer[buf_it] = inpt; \
   buf_it ^= 1; \
   return &buffer[buf_it ^ 1]; \
}


#define _cast_i64__generate(T, S) \
int64_t _cast_i64__##S(void *ptr) { return *(T*)ptr; }


#define _temp_tools_block(Sk, Sv) (struct bptr_temp_tools) \
{ \
   .node = {   _key_ins_i64__##Sk, _val_ins_i64__##Sv, \
               _key_ers__##Sk, _val_ers__##Sv, \
               _wrapper_tools_i64__##Sk, _wrapper_tools_i64__##Sv, \
               _cast_i64__##Sk, } \
}
#define _temp_tools_block_rpt(S) _temp_tools_block(S, S)
/*---------------------------- Private Macros END ----------------------------*/


/*---------------------- Private Function Declarations -----------------------*/
int cmp_u32(const void *lhs, const void *rhs)
{ return (const uint32_t *)lhs - (const uint32_t *)rhs; }
int cmp_u64(const void *lhs, const void *rhs)
{ return (const uint64_t *)lhs - (const uint64_t *)rhs; }
/*-------------------- Private Function Declarations END ---------------------*/

/*---------------------------- Toolbox Functions -----------------------------*/
_kv_tools_i64__generate(uint32_t, u32);
_kv_tools_i64__generate(uint64_t, u64);
_wrapper_tools_i64__generate(uint32_t, u32);
_wrapper_tools_i64__generate(uint64_t, u64);
_cast_i64__generate(uint32_t, u32);
_cast_i64__generate(uint64_t, u64);
/*-------------------------- Toolbox Functions END ---------------------------*/


/*---------------------------- Toolbox Variables -----------------------------*/
struct bptr_temp_tools
   _tools_u32_u32 = _temp_tools_block_rpt(u32),
   _tools_u64_u64 = _temp_tools_block_rpt(u64);
/*-------------------------- Toolbox Variables END ---------------------------*/


/*----------------------------- Public Variables -----------------------------*/
struct bptr_temp lite_temps[] =
{
   { "lite_DEF_u32_u32.bptr", 1, BPTR_NODE_BYTE_DEFAULT,
      sizeof(uint32_t), sizeof(uint32_t), 256, cmp_u32, &_tools_u32_u32 },
   { "lite_128_u64_u64.bptr", 1, 128,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
   { "lite_1024_u64_u64.bptr", 1, 1024,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
   { "lite_2048_u64_u64.bptr", 1, 2048,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
};
size_t lite_temps_sz = sizeof(lite_temps)/sizeof(*lite_temps);

struct bptr_temp norm_temps[] =
{
   { "norm_DEF_u32_u32.bptr", 0, BPTR_NODE_BYTE_DEFAULT,
      sizeof(uint32_t), sizeof(uint32_t), 256, cmp_u32, &_tools_u32_u32 },
   { "norm_128_u64_u64.bptr", 0, 128,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
   { "norm_1024_u64_u64.bptr", 0, 1024,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
   { "norm_2048_u64_u64.bptr", 0, 2048,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
};
size_t norm_temps_sz = sizeof(norm_temps)/sizeof(*norm_temps);


struct bptr_temp lite_temps_iu[] =
{
   { "lite_DEF_u32_u32.bptr", 1, BPTR_NODE_BYTE_DEFAULT,
      sizeof(uint32_t), sizeof(uint32_t), 256, cmp_u32, &_tools_u32_u32 },
   { "lite_128_u64_u64.bptr", 1, 128,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
   { "lite_1024_u64_u64.bptr", 1, 1024,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
   { "lite_2048_u64_u64.bptr", 1, 2048,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
};
size_t lite_temps_iu_sz = sizeof(lite_temps_iu)/sizeof(*lite_temps_iu);

struct bptr_temp norm_temps_iu[] =
{
   { "norm_DEF_u32_u32.bptr", 0, BPTR_NODE_BYTE_DEFAULT,
      sizeof(uint32_t), sizeof(uint32_t), 256, cmp_u32, &_tools_u32_u32 },
   { "norm_128_u64_u64.bptr", 0, 128,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
   { "norm_1024_u64_u64.bptr", 0, 1024,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
   { "norm_2048_u64_u64.bptr", 0, 2048,
      sizeof(uint64_t), sizeof(uint64_t), 256, cmp_u64, &_tools_u64_u64 },
};
size_t norm_temps_iu_sz = sizeof(norm_temps_iu)/sizeof(*norm_temps_iu);
/*--------------------------- Public Variables END ---------------------------*/
