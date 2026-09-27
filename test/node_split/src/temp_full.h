#ifndef __TEMP_FULL_H
#define __TEMP_FULL_H

/*----------------------------- Public Includes ------------------------------*/
#include <stddef.h>
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


/*------------------------------ Public Structs ------------------------------*/
/**
 * @brief   One template the unit is built on
 *
 * The shape a template is generated with.  `gen_full_fixtures' writes one image
 * per entry of `FULL_FIXTURES', `test_temp' verifies one per entry and the
 * split cases instantiate one per entry, so the three cannot drift apart: every
 * template a case may modify is a template that has been checked first.
 */
struct full_fixture
{
   unsigned int lay_cnt;   /* number of levels; 1 yields a single leaf */
   int64_t      st;        /* first key */
   int64_t      interval;  /* distance between two successive keys */
   _Bool        is_lite;   /* use the 4-byte child pointer layout */
   uint32_t     node_size; /* size of a node in bytes */
};
/*---------------------------- Public Structs END ----------------------------*/


/*----------------------- Public Variable Declarations -----------------------*/
/* the images of the unit: 1, 2 and 3 levels tall, keys starting at 0 and
 * stepping by 0x10, in the default lite 512-byte layout */
extern const struct full_fixture FULL_FIXTURES[];
extern const size_t FULL_FIXTURES_SZ;
/*--------------------- Public Variable Declarations END ---------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Path of the fixture `temp_full_generate' serves a request with
 *
 * The name is `bptr_files/temp/full/<lay_cnt>-<st>-<interval>.bptr', relative
 * to the working directory.  The layout (`is_lite' and `node_size') is not part
 * of it, so requests that differ only there share one name; the short-circuit
 * of `temp_full_generate' is what refuses an existing file that does not match.
 * The generator and its callers both build the name here, so a loader cannot
 * look for a name the generator no longer writes.
 *
 * @param[out] buf        destination, always NUL terminated
 * @param[in]  size       capacity of @p buf
 * @param[in]  lay_cnt    number of levels
 * @param[in]  st         first key
 * @param[in]  interval   distance between two successive keys
 *
 * @return  the number of characters written, `snprintf' semantics: a truncated
 *          name returns the length it would have needed.  Even with an extreme
 *          `lay_cnt', `st' and `interval' the name is at most 78 bytes (21 for
 *          the directory, 10 + 20 + 20 for the three fields, 7 for the
 *          separators and the suffix), so a `PATH_MAX' buffer always holds it
 *          whole.
 */
int temp_full_path(char *buf, size_t size, unsigned int lay_cnt, int64_t st,
                   int64_t interval);
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
/**
 * @brief   Copy a canned image to `bptr_files/<path>'
 *
 * The split cases start every insertion from the same pristine template: this
 * copies it to a name of their own instead of loading and modifying the
 * template itself.  The parent directories of @p path are created as needed.
 *
 * @param[in] path  destination, relative to `bptr_files/'
 * @param[in] temp  image to copy, relative to the working directory
 *
 * @return  0 when the image was copied, 1 when @p path already exists (it is
 *          left untouched) and -1 when the copy failed.
 */
int temp_instantiate(const char *path, const char *temp);
void temp_full_verify(struct bptr *bptr,
                      unsigned int lay_cnt, int64_t st, int64_t interval,
                      _Bool has_new_kv, int64_t key, int64_t val);

int cmp_i64(const void *lhs, const void *rhs);
/*--------------------------- Public Functions END ---------------------------*/

#endif
