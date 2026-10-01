#ifndef __TEMP_FULL_H
#define __TEMP_FULL_H

/*----------------------------- Public Includes ------------------------------*/
#include <stddef.h>
#include <stdint.h>
#include "bptree.h"
/*--------------------------- Public Includes END ----------------------------*/


/*------------------------------ Public Defines ------------------------------*/
/* Directory `temp_gen' writes to when no `--dir' is given, relative to the
 * working directory.  A template is always
 * `<dir>/<lay_cnt>-<st>-<interval>.bptr'; `temp_full_path' is the single place
 * the name is put together. */
#define TEMP_FULL_DEFAULT_DIR "bptr_files/temp/full/"

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
 * @brief   Path of the template `temp_full_generate' serves a request with
 *
 * The name is `<dir>/<lay_cnt>-<st>-<interval>.bptr' -- relative to the working
 * directory when @p dir is -- and a trailing '/' of @p dir is collapsed, so
 * `bptr_files/temp/full/' and `bptr_files/temp/full' name the same file.  The
 * layout (`is_lite' and `node_size') is not part of it, so requests that differ
 * only there share one name; the short-circuit of `temp_full_generate' is what
 * refuses an existing file that does not match.  Every tool and every caller
 * builds the name here, so a loader cannot look for a name the generator no
 * longer writes.
 *
 * @param[out] buf        destination, always NUL terminated
 * @param[in]  size       capacity of @p buf
 * @param[in]  dir        directory to place the template in; must not be NULL
 * @param[in]  lay_cnt    number of levels
 * @param[in]  st         first key
 * @param[in]  interval   distance between two successive keys
 *
 * @return  the number of characters written, `snprintf' semantics: a truncated
 *          name returns the length it would have needed.
 */
int temp_full_path(char *buf, size_t size, const char *dir, unsigned int lay_cnt,
                   int64_t st, int64_t interval);
/**
 * @brief   Build, or reuse, a perfectly full tree template of @p lay_cnt levels
 *
 * A plain function rather than a test case: it reports success and failure
 * through its return value (`temp_full_strerror' names a code) and on stderr,
 * never through a Unity assertion, so the caller decides how a failure is
 * surfaced.  The definition in `temp_full.c' documents the layout contract.
 *
 * @param[in] dir        directory to write to, or with a trailing '/'
 * @param[in] lay_cnt    number of levels; 1 yields a single leaf
 * @param[in] st         first key
 * @param[in] interval   distance between two successive keys
 * @param[in] is_lite    use the 4-byte child pointer layout
 * @param[in] node_size  size of a node in bytes
 *
 * @return  TEMP_FULL_OK (0) on success; a `TEMP_FULL_*' error code otherwise
 */
int temp_full_generate(const char *dir, unsigned int lay_cnt, int64_t st,
                       int64_t interval, _Bool is_lite, uint32_t node_size);
/**
 * @brief   Name a `TEMP_FULL_*' status
 *
 * @param[in] status  status returned by `temp_full_generate'
 *
 * @return  a static human readable description of @p status
 */
const char *temp_full_strerror(int status);
/**
 * @brief   Copy a template to @p dst, creating its parent directories
 *
 * A split test starts from a pristine template: this copies it to a name of its
 * own instead of loading and modifying the template itself.  @p dst is used as
 * given (relative to the working directory), and it is created exclusively, so
 * an existing destination is never written to.  A copy that fails after the
 * destination was created is removed before returning, so that a retry cannot
 * mistake a truncated image for a complete one.
 *
 * @param[in] dst  destination path, created with the parent directories it needs
 * @param[in] src  image to copy
 *
 * @return  0 when the image was copied, 1 when @p dst already exists (it is
 *          left untouched) and -1 when the copy failed, with `errno' set to the
 *          reason of the failing call.
 */
int temp_instantiate(const char *dst, const char *src);
/**
 * @brief   Assert the shape of a loaded template
 *
 * Asserts with fixed messages, so it is only callable from inside a Unity case
 * (`RUN_TEST' installs the abort frame an assertion longjmps to).  The
 * definition in `temp_verify.c' documents what is checked.
 */
void temp_full_verify(struct bptr *bptr,
                      unsigned int lay_cnt, int64_t st, int64_t interval,
                      _Bool has_new_kv, int64_t key, int64_t val);
/**
 * @brief   Three-way comparator of two `int64_t' keys
 *
 * The comparator a template is built and loaded with.
 */
int cmp_i64(const void *lhs, const void *rhs);
/*--------------------------- Public Functions END ---------------------------*/

#endif
