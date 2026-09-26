#ifndef __TEMP_FULL_H
#define __TEMP_FULL_H

/*----------------------------- Public Includes ------------------------------*/
#include <stdint.h>
#include "bptree.h"
/*--------------------------- Public Includes END ----------------------------*/


/*------------------------------ Public Defines ------------------------------*/
/* Status of `temp_full_generate'.  A request that cannot be served is negative
 * and an environment failure positive, mirroring the `BPTR_E_*' convention of
 * `bptree.h'; only TEMP_FULL_OK means the fixture is complete.  Every code is
 * named by `temp_full_strerror'; `bptr_errno' carries the library's reason where
 * there is one, while `errno' may already have been overwritten by the cleanup
 * the failure triggered. */
#define TEMP_FULL_OK           (0)  /* the fixture is complete */
#define TEMP_FULL_E_LAY_CNT    (-1) /* lay_cnt is 0 or taller than the node cache */
#define TEMP_FULL_E_NODE_SIZE  (-2) /* node_size cannot hold a full node */
#define TEMP_FULL_E_FIXTURE    (1)  /* an existing file is not this fixture */
#define TEMP_FULL_E_UNREADABLE (2)  /* an existing file cannot be read */
#define TEMP_FULL_E_DIR        (3)  /* the fixture directory cannot be created */
#define TEMP_FULL_E_INIT       (4)  /* the image cannot be created */
#define TEMP_FULL_E_ALLOC      (5)  /* out of memory */
#define TEMP_FULL_E_CHAIN      (6)  /* the level list cannot be linked */
#define TEMP_FULL_E_WRITE      (7)  /* the image cannot be written */
/*---------------------------- Public Defines END ----------------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Build, or reuse, a perfectly full tree fixture of @p lay_cnt levels
 *
 * A plain function rather than a test case: it reports success and failure
 * through its return value (`temp_full_strerror' names a code) and on stderr,
 * never through a Unity assertion, so the caller decides how a failure is
 * surfaced.  The definition in `temp_full.c' documents the layout contract.
 *
 * @param[in] lay_cnt    number of levels; 1 yields a single leaf
 * @param[in] st         first key
 * @param[in] interval   distance between two successive keys
 * @param[in] is_lite    use the 4-byte child pointer layout
 * @param[in] node_size  size of a node in bytes
 *
 * @return  TEMP_FULL_OK (0) on success; a `TEMP_FULL_*' error code otherwise
 */
int temp_full_generate(unsigned int lay_cnt, int64_t st, int64_t interval,
                       _Bool is_lite, uint32_t node_size);
/**
 * @brief   Name a `TEMP_FULL_*' status
 *
 * @param[in] status  status returned by `temp_full_generate'
 *
 * @return  a static human readable description of @p status
 */
const char *temp_full_strerror(int status);
int temp_instantiate(const char *path, const char *temp);
void temp_full_verify(struct bptr *bptr,
                      unsigned int lay_cnt, int64_t st, int64_t interval,
                      _Bool has_new_kv, int64_t key, int64_t val);

int cmp_i64(const void *lhs, const void *rhs);
/*--------------------------- Public Functions END ---------------------------*/

#endif
