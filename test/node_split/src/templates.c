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
/* How `_parse_name' reads a file name; `templates_load_dir' names the two
 * failures apart in the reason it hands back. */
#define _NAME_OK      (0)
#define _NAME_BAD     (-1)  /* not a canonical full/custom fixture name */
#define _NAME_LAY_CNT (-2)  /* the convention, but with lay_cnt 0 */
/*---------------------------- Private Defines END ----------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static int _cmp_template(const void *lhs, const void *rhs);
static int _join(char *buf, size_t size, const char *dir, const char *name);
static int _parse_name(const char *name, struct template *tmpl);
static void _free_list(struct template *list, size_t cnt);
static void _reason(char *buf, size_t size, const char *fmt, ...);
static void _set_list(struct template *list, size_t cnt);
/*------------------------- Forward Declarations END -------------------------*/


/*---------------------------- Private Variables -----------------------------*/
/* the templates the cases walk, owned by this module */
static struct template *templates_list;
static size_t templates_cnt;
/*---------------------------- Private Variables END --------------------------*/


/*----------------------------- Public Functions -----------------------------*/
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
      struct template tmpl = {0};
      size_t len = strlen(de->d_name);
      int status;

      /* only a `.bptr' entry is a candidate; anything else in the directory is
       * none of this unit's business */
      if (len < 5 || strcmp(de->d_name + len - 5, ".bptr") != 0) continue;

      if (_join(path, sizeof path, dir, de->d_name) != 0)
       {
         _reason(reason, size, "%s/%s: the path is longer than %d bytes", dir,
                 de->d_name, PATH_MAX);
         closedir(dp);
         _free_list(list, cnt);
         return TEMPLATES_E_NAME;
       }

      status = _parse_name(de->d_name, &tmpl);
      if (status != _NAME_OK)
       {
         closedir(dp);
         _free_list(list, cnt);
         if (status == _NAME_LAY_CNT)
            _reason(reason, size, "%s: lay_cnt 0 does not name a template",
                    path);
         else
            _reason(reason, size, "%s is not a canonical full or custom "
                    "template name", path);
         return TEMPLATES_E_NAME;
       }

      if (cnt == cap)
       {
         struct template *grown;
         size_t next = cap == 0 ? 8 : cap * 2;

         grown = cap > SIZE_MAX / 2 || next > SIZE_MAX / sizeof *grown
                   ? NULL : realloc(list, next * sizeof *grown);
         if (grown == NULL)
          {
            closedir(dp);
            temp_shape_free(&tmpl.shape);
            _free_list(list, cnt);
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
      _free_list(list, cnt);
      _reason(reason, size, "cannot read the directory %s: %s", dir,
              strerror(err));
      return TEMPLATES_E_DIR;
    }

   if (cnt == 0)
    {
      _free_list(list, cnt);
      _reason(reason, size,
              "no full or custom .bptr template in %s", dir);
      return TEMPLATES_E_EMPTY;
    }

   qsort(list, cnt, sizeof *list, _cmp_template);
   _set_list(list, cnt);

   return TEMPLATES_OK;
}


const struct template *templates_get(size_t *cnt)
{
   if (cnt != NULL) *cnt = templates_cnt;

   return templates_list;
}


void templates_cleanup(void)
{
   _set_list(NULL, 0);
}


int cmp_i64(const void *lhs, const void *rhs)
{
   int64_t a = *(const int64_t *)lhs, b = *(const int64_t *)rhs;
   return (a > b) - (a < b);
}
/*--------------------------- Public Functions END ---------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static int _cmp_template(const void *lhs, const void *rhs)
{
   const struct template *a = lhs, *b = rhs;

   if (a->lay_cnt != b->lay_cnt) return a->lay_cnt < b->lay_cnt ? -1 : 1;
   if (a->st != b->st) return a->st < b->st ? -1 : 1;
   if (a->interval != b->interval) return a->interval < b->interval ? -1 : 1;

   return strcmp(a->path, b->path);
}


static int _join(char *buf, size_t size, const char *dir, const char *name)
{
   size_t len = strlen(dir);
   const char *sep = (len > 0 && dir[len - 1] == '/') ? "" : "/";
   int needed = snprintf(buf, size, "%s%s%s", dir, sep, name);

   return needed < 0 || (size_t)needed >= size ? -1 : 0;
}


/* Parse with the shared fixture grammar, then round-trip through its canonical
 * formatter. This rejects decimal aliases, reordered overrides and alternate
 * spellings while preserving the exact full-fixture naming convention. */
static int _parse_name(const char *name, struct template *tmpl)
{
   char canon[PATH_MAX + 2];
   const char *base;
   int needed;

   if (!temp_parse_name(name, &tmpl->lay_cnt, &tmpl->st, &tmpl->interval,
                        &tmpl->shape))
      return strncmp(name, "0-", 2) == 0 ? _NAME_LAY_CNT : _NAME_BAD;

   needed = temp_path(canon, sizeof canon, "", tmpl->lay_cnt, tmpl->st,
                       tmpl->interval, &tmpl->shape);
   if (needed < 0 || (size_t)needed >= sizeof canon)
    {
      temp_shape_free(&tmpl->shape);
      return _NAME_BAD;
    }
   base = strrchr(canon, '/');
   base = base == NULL ? canon : base + 1;
   if (strcmp(base, name))
    {
      temp_shape_free(&tmpl->shape);
      return _NAME_BAD;
    }

   return _NAME_OK;
}


static void _free_list(struct template *list, size_t cnt)
{
   for (size_t i = 0; i < cnt; i++) temp_shape_free(&list[i].shape);
   free(list);
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
   _free_list(templates_list, templates_cnt);
   templates_list = list;
   templates_cnt = cnt;
}
/*-------------------------- Private Functions END ---------------------------*/
