#ifndef TEMPLATES_H
#define TEMPLATES_H

/*----------------------------- Public Includes ------------------------------*/
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
/*--------------------------- Public Includes END ----------------------------*/


/*------------------------------ Public Defines ------------------------------*/
/* Directory the unit reads its templates from when no directory is given on the
 * command line, relative to the working directory.  The unit never writes
 * there: preparing a template is the caller's job, and a template is named
 * `<lay_cnt>-<st>-<interval>.bptr' -- the name `bin/temp_gen' gives an image. */
#define TEMPLATES_DEFAULT_DIR "bptr_files/temp/full/"

/* Status of `templates_load_dir'.  `TEMPLATES_OK' is the only success; the
 * other codes are request or environment errors (`TEMPLATES_E_DIR' and
 * `TEMPLATES_E_EMPTY' both mean "nothing to test"), and the loader formats its
 * reason into the `reason' buffer the caller passes. */
#define TEMPLATES_OK      (0)  /* every template is loaded */
#define TEMPLATES_E_DIR   (1)  /* the directory cannot be read */
#define TEMPLATES_E_EMPTY (2)  /* the directory holds no template */
#define TEMPLATES_E_NAME  (3)  /* an entry does not follow the name convention */
#define TEMPLATES_E_ALLOC (4)  /* out of memory */
/*---------------------------- Public Defines END ----------------------------*/


/*------------------------------ Public Structs ------------------------------*/
/**
 * @brief   One template the cases work on
 *
 * The shape of the image, read off its file name, and the path it lives at.
 * `main' fills the list by scanning the template directory it was given (or
 * `TEMPLATES_DEFAULT_DIR'), and the cases only ever walk that list through
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


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Load every template @p dir holds
 *
 * Scans the directory for `*.bptr' entries and reads `lay_cnt'-`st'-`interval'
 * off each name; an entry of any other name is ignored, a `.bptr' one that does
 * not follow the convention is an error -- a file that is offered as a template
 * must never be dropped silently.  The name has to be spelled exactly as
 * `bin/temp_gen' writes it: a name that would only parse under a different
 * spelling (a leading zero, a `+') is a stranger, not a second name of an image
 * the caller prepared.  The list is sorted by `lay_cnt', then `st', then
 * `interval', because the order of `readdir' is not defined and the run has to
 * be reproducible.  It replaces the list `templates_get' hands out.
 *
 * A directory that cannot be read and one that holds no template are told
 * apart, but both mean "nothing to test": the unit decides how to report that
 * (it warns and succeeds), the loader only names it.
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
 * The comparator every load in this unit is handed: the templates are raw
 * images of 8-byte signed keys.
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
