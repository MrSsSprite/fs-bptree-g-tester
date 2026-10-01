/*----------------------------- Private Includes -----------------------------*/
#include "templates.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
/*--------------------------- Private Includes END ---------------------------*/


/*----------------------------- Public Variables -----------------------------*/
const struct full_fixture FULL_FIXTURES[] =
{
   { 1, 0, 0x10, 1, 512 },
   { 2, 0, 0x10, 1, 512 },
   { 3, 0, 0x10, 1, 512 },
};

const size_t FULL_FIXTURES_SZ = sizeof FULL_FIXTURES / sizeof FULL_FIXTURES[0];
/*---------------------------- Public Variable END ---------------------------*/


/*----------------------------- Public Functions -----------------------------*/
int templates_path(char *buf, size_t size, const char *dir,
                   unsigned int lay_cnt, int64_t st, int64_t interval)
{
   size_t len = strlen(dir);
   const char *sep = (len > 0 && dir[len - 1] == '/') ? "" : "/";

   return snprintf(buf, size, "%s%s%u-%" PRIi64 "-%" PRIi64 ".bptr",
                   dir, sep, lay_cnt, st, interval);
}


int cmp_i64(const void *lhs, const void *rhs)
{
   int64_t diff = *(const int64_t *)lhs - *(const int64_t *)rhs;
   return diff < 0 ? -1 : diff > 0 ? 1 : 0;
}
/*--------------------------- Public Functions END ---------------------------*/
