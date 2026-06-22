#ifndef TEST_BPTR_TEMP_H
#define TEST_BPTR_TEMP_H

/*----------------------------- Public Includes ------------------------------*/
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "bptr_node.h"
/*--------------------------- Public Includes END ----------------------------*/

/*------------------------------ Public Structs ------------------------------*/
struct bptr_temp
{
   const char *fnm;
   _Bool is_lite;
   uint32_t node_sz;
   uint16_t key_sz, val_sz;
   uint64_t cache_cap;
   int (*cmp)(const void *, const void *);
   struct bptr_temp_tools *tools;
};

struct bptr_temp_tools
{
   struct
    {
      void (*key_ins_i64)(struct bptr_node*, int64_t, size_t);
      void (*val_ins_i64)(struct bptr_node*, int64_t, size_t);
      void (*key_ers)(struct bptr_node*, size_t);
      void (*val_ers)(struct bptr_node*, size_t);
      void *(*key_wrapper_i64)(int64_t);
      void *(*val_wrapper_i64)(int64_t);
      int64_t (*cast_i64)(void*);
    } node;
};
/*---------------------------- Public Structs END ----------------------------*/

/*----------------------- Public Variable Declarations -----------------------*/
extern struct bptr_temp lite_temps[];
extern size_t lite_temps_sz;
extern struct bptr_temp norm_temps[];
extern size_t norm_temps_sz;

extern struct bptr_temp lite_temps_iu[];
extern size_t lite_temps_iu_sz;
extern struct bptr_temp norm_temps_iu[];
extern size_t norm_temps_iu_sz;
/*--------------------- Public Variable Declarations END ---------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Insert a child pointer into a branch node's vals array with the
 *          correct pointer size (lite=4, norm=8).
 */
static inline
void _bptr_val_ins_ptr(struct bptr_node *node, int64_t ptr_val, size_t idx,
                       _Bool is_lite)
{
   size_t val_cnt = _node_val_cnt(node);
   size_t ptr_size = is_lite ? 4 : 8;
   if (idx < val_cnt)
      memmove((char*)node->vals + (idx + 1) * ptr_size,
              (char*)node->vals + idx * ptr_size,
              (val_cnt - idx) * ptr_size);
   if (is_lite)
      ((uint32_t*)node->vals)[idx] = (uint32_t)ptr_val;
   else
      ((uint64_t*)node->vals)[idx] = (uint64_t)ptr_val;
}

static inline
void _bptr_kv_ins_i64(struct bptr_node *node, struct bptr_temp_tools *tools,
                      int64_t key, int64_t val, size_t idx, _Bool is_lite)
{
   tools->node.key_ins_i64(node, key, idx);
   if (node->is_leaf)
      tools->node.val_ins_i64(node, val, idx);
   else
      _bptr_val_ins_ptr(node, val, idx + 1, is_lite);
   node->key_count++;
}

static inline
void _bptr_kv_ers(struct bptr_node *node, struct bptr_temp_tools *tools,
                  size_t idx)
{
   tools->node.key_ers(node, idx);
   tools->node.val_ers(node, idx);
   node->key_count--;
}
/*--------------------------- Public Functions END ---------------------------*/

#endif
