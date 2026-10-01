#ifndef TEMPLATES_H
#define TEMPLATES_H

/*----------------------------- Public Includes ------------------------------*/
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
/*--------------------------- Public Includes END ----------------------------*/


/*------------------------------ Public Defines ------------------------------*/
/* Directory the templates of the unit live in when no directory is given on the
 * command line, relative to the working directory.  The name of a template is
 * `<lay_cnt>-<st>-<interval>.bptr' (see `templates_path'). */
#define TEMPLATES_DEFAULT_DIR "bptr_files/temp/full/"

/* Status of `templates_parse' and of the two loaders.  `TEMPLATES_OK' is the
 * only success; every other code is a request or environment error, and a
 * loader formats its reason into the `reason' buffer the caller passes. */
#define TEMPLATES_OK      (0)  /* every template is loaded */
#define TEMPLATES_E_DIR   (1)  /* the directory cannot be read */
#define TEMPLATES_E_EMPTY (2)  /* the directory holds no template */
#define TEMPLATES_E_NAME  (3)  /* an entry does not follow the name convention */
#define TEMPLATES_E_ALLOC (4)  /* out of memory */
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

/**
 * @brief   One template the cases work on
 *
 * The shape of the image, read off its file name, and the path it lives at.
 * `main' fills the list either from `FULL_FIXTURES' (no directory given) or by
 * scanning a directory, and the cases only ever walk that list through
 * `templates_get'.
 *
 * An instance image does not have to follow the name convention -- the split
 * case names its copies after the position they are split at -- which is why
 * the shape is handed to the tools explicitly instead of being re-read from the
 * path: the path of a copy says nothing about the lattice.
 */
struct template
{
   unsigned int lay_cnt;         /* number of levels; 1 yields a single leaf */
   int64_t      st;              /* first key */
   int64_t      interval;        /* distance between two successive keys */
   char         path[PATH_MAX];  /* the image, relative to the cwd */
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
 * @brief   Read the shape of a template off its file name
 *
 * @p name is the last path segment, `<lay_cnt>-<st>-<interval>.bptr', spelled
 * exactly as `templates_path' writes it: a name that would only parse under a
 * different spelling (a leading zero, a `+') is a stranger, not a second name of
 * a file the unit knows.
 *
 * @param[in]  name  file name of a template
 * @param[out] out   shape of the template; its `path' holds @p name, the loaders
 *                   replace that with the full path
 *
 * @return  TEMPLATES_OK when the name follows the convention, TEMPLATES_E_NAME
 *          when it does not or when `lay_cnt' is 0 (no image can be generated
 *          for it)
 */
int templates_parse(const char *name, struct template *out);
/**
 * @brief   Load every template @p dir holds
 *
 * Scans the directory for `*.bptr' entries and parses each name with
 * `templates_parse'; an entry of any other name is ignored, a `.bptr' one that
 * does not follow the convention is an error -- a template the unit could not
 * use must never be dropped silently.  The list is sorted by `lay_cnt', then
 * `st', then `interval', because the order of `readdir' is not defined and the
 * run has to be reproducible.  It replaces the list `templates_get' hands out.
 *
 * @param[in]  dir     directory to scan, relative to the working directory
 * @param[out] reason  human readable reason of a failure, always NUL terminated
 * @param[in]  size    capacity of @p reason
 *
 * @return  TEMPLATES_OK when at least one template was loaded; TEMPLATES_E_DIR,
 *          TEMPLATES_E_EMPTY, TEMPLATES_E_NAME or TEMPLATES_E_ALLOC otherwise
 */
int templates_load_dir(const char *dir, char *reason, size_t size);
/**
 * @brief   Load the templates `main' generated from `FULL_FIXTURES'
 *
 * The default directory is not scanned: the unit generated exactly one image
 * per `FULL_FIXTURES' entry, so the table is the list, and a template some
 * other run left in the directory cannot change what this run covers.
 *
 * @param[out] reason  human readable reason of a failure, always NUL terminated
 * @param[in]  size    capacity of @p reason
 *
 * @return  TEMPLATES_OK, or TEMPLATES_E_ALLOC when the list cannot be built
 */
int templates_load_default(char *reason, size_t size);
/**
 * @brief   The templates of the unit, in the order the cases walk them
 *
 * @param[out] cnt  number of templates; may be NULL
 *
 * @return  the list, owned by this module and replaced by the next load
 */
const struct template *templates_get(size_t *cnt);
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
