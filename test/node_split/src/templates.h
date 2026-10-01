#ifndef TEMPLATES_H
#define TEMPLATES_H

/*----------------------------- Public Includes ------------------------------*/
#include <stddef.h>
#include <stdint.h>
/*--------------------------- Public Includes END ----------------------------*/


/*------------------------------ Public Defines ------------------------------*/
/* Directory the templates of the unit live in when no directory is given on the
 * command line, relative to the working directory.  The name of a template is
 * `<lay_cnt>-<st>-<interval>.bptr' (see `templates_path'). */
#define TEMPLATES_DEFAULT_DIR "bptr_files/temp/full/"
/*---------------------------- Public Defines END ----------------------------*/


/*------------------------------ Public Structs ------------------------------*/
/**
 * @brief   One template the unit is built on
 *
 * The shape a template is generated with.  `gen_full_fixtures' writes one image
 * per entry of `FULL_FIXTURES', `test_temp' verifies one per entry and the
 * split case instantiates one per entry, so the three cannot drift apart: every
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
 * @brief   Path a template of @p lay_cnt levels is named by
 *
 * The name is `<dir>/<lay_cnt>-<st>-<interval>.bptr', relative to the working
 * directory; a @p dir that already ends in a slash does not get a second one.
 * The layout (`is_lite' and `node_size') is not part of it, so two requests that
 * differ only there share one name; the generator refuses an existing file that
 * does not match the request.  The generator tool and the unit both build the
 * name here, so a loader cannot look for a name the generator no longer writes.
 *
 * @param[out] buf        destination, always NUL terminated
 * @param[in]  size       capacity of @p buf
 * @param[in]  dir        directory the template lives in
 * @param[in]  lay_cnt    number of levels
 * @param[in]  st         first key
 * @param[in]  interval   distance between two successive keys
 *
 * @return  the number of characters written, `snprintf' semantics: a truncated
 *          name returns the length it would have needed
 */
int templates_path(char *buf, size_t size, const char *dir,
                   unsigned int lay_cnt, int64_t st, int64_t interval);
/**
 * @brief   Comparator of the key lattice, `int64_t' keys
 *
 * The comparator the templates are built with, and the one every load in this
 * unit is handed: the fixtures are raw images of 8-byte signed keys.
 *
 * @param[in] lhs  first key
 * @param[in] rhs  second key
 *
 * @return  negative, 0 or positive as `*lhs' sorts below, equal to or above
 *          `*rhs'
 */
int cmp_i64(const void *lhs, const void *rhs);
/*--------------------------- Public Functions END ---------------------------*/

#endif
