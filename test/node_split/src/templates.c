/*----------------------------- Private Includes -----------------------------*/
#include "templates.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Private Defines ------------------------------*/
/* How `_parse_name' reads a file name; `templates_parse' maps both failures
 * onto TEMPLATES_E_NAME, the loader names them apart. */
#define _NAME_OK      (0)
#define _NAME_BAD     (-1)  /* not `<lay_cnt>-<st>-<interval>.bptr' */
#define _NAME_LAY_CNT (-2)  /* the convention, but with lay_cnt 0 */
/*---------------------------- Private Defines END ----------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static int _cmp_template(const void *lhs, const void *rhs);
static int _join(char *buf, size_t size, const char *dir, const char *name);
static int _parse_name(const char *name, unsigned int *lay_cnt, int64_t *st,
                       int64_t *interval);
static void _reason(char *buf, size_t size, const char *fmt, ...);
static void _set_list(struct template *list, size_t cnt);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------- Public Variables -----------------------------*/
const struct full_fixture FULL_FIXTURES[] =
{
   { 1, 0, 0x10, 1, 512 },
   { 2, 0, 0x10, 1, 512 },
   { 3, 0, 0x10, 1, 512 },
};

const size_t FULL_FIXTURES_SZ = sizeof FULL_FIXTURES / sizeof FULL_FIXTURES[0];
/*---------------------------- Public Variable END ---------------------------*/


/*---------------------------- Private Variables -----------------------------*/
/* the templates the cases walk, owned by this module */
static struct template *templates_list;
static size_t templates_cnt;
/*---------------------------- Private Variables END --------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int templates_path(char *buf, size_t size, const char *dir,
                   unsigned int lay_cnt, int64_t st, int64_t interval)
{
   size_t len = strlen(dir);
   const char *sep = (len > 0 && dir[len - 1] == '/') ? "" : "/";

   return snprintf(buf, size, "%s%s%u-%" PRIi64 "-%" PRIi64 ".bptr",
                   dir, sep, lay_cnt, st, interval);
}


int templates_parse(const char *name, struct template *out)
{
   unsigned int lay_cnt;
   int64_t st, interval;
   int status = _parse_name(name, &lay_cnt, &st, &interval);

   if (status != _NAME_OK) return TEMPLATES_E_NAME;

   if (out != NULL)
    {
      out->lay_cnt = lay_cnt;
      out->st = st;
      out->interval = interval;
      snprintf(out->path, sizeof out->path, "%s", name);
    }

   return TEMPLATES_OK;
}


int templates_load_dir(const char *dir, char *reason, size_t size)
{
   struct template *list = NULL;
   size_t cnt = 0, cap = 0;
   struct dirent *de;
   DIR *dp;

   _reason(reason, size, "%s", "");

   dp = opendir(dir);
   if (dp == NULL)
    {
      int err = errno;
      _reason(reason, size, "cannot read the directory %s: %s", dir,
              strerror(err));
      return TEMPLATES_E_DIR;
    }

   while ((de = readdir(dp)) != NULL)
    {
      char path[PATH_MAX];
      struct template tmpl;
      size_t len = strlen(de->d_name);
      int status;

      /* only a `.bptr' entry is a candidate; anything else in the directory is
       * none of this unit's business */
      if (len < 5 || strcmp(de->d_name + len - 5, ".bptr") != 0) continue;

      if (_join(path, sizeof path, dir, de->d_name) != 0)
       {
         closedir(dp);
         free(list);
         _reason(reason, size, "%s/%s: the path is longer than %d bytes", dir,
                 de->d_name, PATH_MAX);
         return TEMPLATES_E_NAME;
       }

      status = _parse_name(de->d_name, &tmpl.lay_cnt, &tmpl.st,
                           &tmpl.interval);
      if (status != _NAME_OK)
       {
         closedir(dp);
         free(list);
         if (status == _NAME_LAY_CNT)
            _reason(reason, size, "%s: lay_cnt 0 does not name a template",
                    path);
         else
            _reason(reason, size, "%s is not a <lay_cnt>-<st>-<interval>.bptr "
                    "template name", path);
         return TEMPLATES_E_NAME;
       }

      if (cnt == cap)
       {
         struct template *grown;
         size_t next = cap == 0 ? 8 : cap * 2;

         grown = realloc(list, next * sizeof *grown);
         if (grown == NULL)
          {
            closedir(dp);
            free(list);
            _reason(reason, size, "out of memory");
            return TEMPLATES_E_ALLOC;
          }

         list = grown;
         cap = next;
       }

      snprintf(tmpl.path, sizeof tmpl.path, "%s", path);
      list[cnt++] = tmpl;
    }

   if (closedir(dp) != 0)
    {
      int err = errno;
      free(list);
      _reason(reason, size, "cannot read the directory %s: %s", dir,
              strerror(err));
      return TEMPLATES_E_DIR;
    }

   if (cnt == 0)
    {
      free(list);
      _reason(reason, size,
              "no <lay_cnt>-<st>-<interval>.bptr template in %s", dir);
      return TEMPLATES_E_EMPTY;
    }

   qsort(list, cnt, sizeof *list, _cmp_template);
   _set_list(list, cnt);

   return TEMPLATES_OK;
}


int templates_load_default(char *reason, size_t size)
{
   struct template *list;
   size_t i;

   _reason(reason, size, "%s", "");

   if (FULL_FIXTURES_SZ == 0)
    {
      _reason(reason, size, "the fixture table is empty");
      return TEMPLATES_E_EMPTY;
    }

   list = calloc(FULL_FIXTURES_SZ, sizeof *list);
   if (list == NULL)
    {
      _reason(reason, size, "out of memory");
      return TEMPLATES_E_ALLOC;
    }

   for (i = 0; i < FULL_FIXTURES_SZ; i++)
    {
      const struct full_fixture *fx = &FULL_FIXTURES[i];

      list[i].lay_cnt = fx->lay_cnt;
      list[i].st = fx->st;
      list[i].interval = fx->interval;
      templates_path(list[i].path, sizeof list[i].path, TEMPLATES_DEFAULT_DIR,
                     fx->lay_cnt, fx->st, fx->interval);
    }

   _set_list(list, FULL_FIXTURES_SZ);

   return TEMPLATES_OK;
}


const struct template *templates_get(size_t *cnt)
{
   if (cnt != NULL) *cnt = templates_cnt;

   return templates_list;
}


int cmp_i64(const void *lhs, const void *rhs)
{
   int64_t diff = *(const int64_t *)lhs - *(const int64_t *)rhs;
   return diff < 0 ? -1 : diff > 0 ? 1 : 0;
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static int _cmp_template(const void *lhs, const void *rhs)
{
   const struct template *a = lhs, *b = rhs;

   if (a->lay_cnt != b->lay_cnt) return a->lay_cnt < b->lay_cnt ? -1 : 1;
   if (a->st != b->st) return a->st < b->st ? -1 : 1;
   if (a->interval != b->interval) return a->interval < b->interval ? -1 : 1;

   return 0;
}


static int _join(char *buf, size_t size, const char *dir, const char *name)
{
   size_t len = strlen(dir);
   const char *sep = (len > 0 && dir[len - 1] == '/') ? "" : "/";
   int needed = snprintf(buf, size, "%s%s%s", dir, sep, name);

   return needed < 0 || (size_t)needed >= size ? -1 : 0;
}


/**
 * @brief   Read `<lay_cnt>-<st>-<interval>.bptr' into its three numbers
 *
 * The name is accepted only in the spelling `templates_path' produces: the
 * parse is followed by a format back into the same buffer and a comparison, so
 * a name that merely parses (`01-0-16.bptr', `+1-0-16.bptr') is a stranger, not
 * a second spelling of a file the unit already knows.
 *
 * @param[in]  name      file name of a template
 * @param[out] lay_cnt   number of levels
 * @param[out] st        first key
 * @param[out] interval  distance between two successive keys
 *
 * @return  _NAME_OK, _NAME_BAD or _NAME_LAY_CNT
 */
static int _parse_name(const char *name, unsigned int *lay_cnt, int64_t *st,
                       int64_t *interval)
{
   char canon[64];
   unsigned int lay;
   long long first, step;
   int end = 0;

   if (sscanf(name, "%u-%lld-%lld.bptr%n", &lay, &first, &step, &end) != 3 ||
       name[end] != '\0')
      return _NAME_BAD;

   snprintf(canon, sizeof canon, "%u-%lld-%lld.bptr", lay, first, step);
   if (strcmp(canon, name) != 0) return _NAME_BAD;

   if (lay == 0) return _NAME_LAY_CNT;

   *lay_cnt = lay;
   *st = first;
   *interval = step;

   return _NAME_OK;
}


static void _reason(char *buf, size_t size, const char *fmt, ...)
{
   va_list args;

   if (buf == NULL || size == 0) return;

   va_start(args, fmt);
   vsnprintf(buf, size, fmt, args);
   va_end(args);
}


static void _set_list(struct template *list, size_t cnt)
{
   free(templates_list);
   templates_list = list;
   templates_cnt = cnt;
}
/*-------------------------- Private Functions END ---------------------------*/
