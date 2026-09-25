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
/**
 * @brief   Drop the fixture a failed `temp_full_generate' left behind
 *
 * A generation failure that is reported through a Unity assertion does not
 * return, so the generator's own error path cannot remove the partially
 * written image.  Call this from the test runner's `tearDown'; the
 * exists-short-circuit of `temp_full_generate' would otherwise serve that
 * partial file as a good fixture, and every later run would pass.
 *
 * @note  No-op when the last `temp_full_generate' call succeeded or wrote
 *        nothing.
 */
void temp_full_discard(void);
void temp_full_verify(struct bptr *bptr,
                      unsigned int lay_cnt, int64_t st, int64_t interval,
                      _Bool has_new_kv, int64_t key, int64_t val);

int cmp_i64(const void *lhs, const void *rhs);
/*--------------------------- Public Functions END ---------------------------*/

#endif
