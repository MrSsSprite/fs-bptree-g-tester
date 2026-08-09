#ifndef __TEMP_FULL_H
#define __TEMP_FULL_H

/*----------------------------- Public Includes ------------------------------*/
#include <stdint.h>
#include "bptree.h"
/*--------------------------- Public Includes END ----------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int temp_full_generate(unsigned int lay_cnt, int64_t st, int64_t interval,
                       _Bool is_lite, uint32_t node_size);
int temp_instantiate(const char *path, const char *temp);
void temp_full_verify(struct bptr *bptr,
                      unsigned int lay_cnt, int64_t st, int64_t interval,
                      _Bool has_new_kv, int64_t key, int64_t val);

int cmp_i64(const void *lhs, const void *rhs);
/*--------------------------- Public Functions END ---------------------------*/

#endif
