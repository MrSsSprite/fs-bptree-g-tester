/*----------------------------- Private Includes -----------------------------*/
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "temp_full.h"
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Private Macros ------------------------------*/
/* Process status of the tool, part of its interface: 0 the template exists, 1
 * it could not be built -- `temp_full_generate' names the reason on stderr --
 * and 2 the command line cannot be served. */
#define GEN_EXIT_OK    (0)
#define GEN_EXIT_FAIL  (1)
#define GEN_EXIT_USAGE (2)

/* node size and layout a request without `--node-size'/`--norm' is served with */
#define GEN_NODE_SIZE_DEFAULT 512u
/*------------------------------ Private Macros END --------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static void usage(FILE *stream, const char *prog);
static _Bool _parse_u32(const char *str, uint32_t *out);
static _Bool _parse_i64(const char *str, int64_t *out);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------------- MAIN -----------------------------------*/
int main(int argc, char **argv)
{
   const char *dir = NULL;
   _Bool is_lite = 1;
   uint32_t node_size = GEN_NODE_SIZE_DEFAULT;
   uint32_t lay_cnt;
   int64_t st, interval;
   char path[PATH_MAX];
   struct stat fst;
   _Bool was_there;
   int at, status;

   /* The options come first and the three positional numbers after them:
    * option scanning stops at the first argument that is not one of the known
    * options, so that a negative `st' (which also starts with '-') is read as
    * the number it is rather than refused as an unknown option. */
   for (at = 1; at < argc; at++)
    {
      const char *arg = argv[at];

      if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
       { usage(stdout, argv[0]); return GEN_EXIT_OK; }
      else if (!strcmp(arg, "--dir"))
       {
         if (++at == argc) { usage(stderr, argv[0]); return GEN_EXIT_USAGE; }
         dir = argv[at];
       }
      else if (!strcmp(arg, "--norm"))
         is_lite = 0;
      else if (!strcmp(arg, "--node-size"))
       {
         if (++at == argc || !_parse_u32(argv[at], &node_size))
          { usage(stderr, argv[0]); return GEN_EXIT_USAGE; }
       }
      else
         break;
    }
   if (argc - at != 3 ||
       !_parse_u32(argv[at], &lay_cnt) ||
       !_parse_i64(argv[at + 1], &st) ||
       !_parse_i64(argv[at + 2], &interval))
    { usage(stderr, argv[0]); return GEN_EXIT_USAGE; }

   if (dir != NULL && *dir == '\0')
    { usage(stderr, argv[0]); return GEN_EXIT_USAGE; }
   if (dir == NULL)
    {
      dir = TEMP_FULL_DEFAULT_DIR;
      printf("temp_gen: no --dir given; using the default %s\n", dir);
    }

   temp_full_path(path, sizeof path, dir, lay_cnt, st, interval);
   /* a template `temp_full_generate' can serve is never rewritten: a regular
    * file at the path before the call is the file it hands back, everything
    * else makes it fail with a reason of its own */
   was_there = stat(path, &fst) == 0 && S_ISREG(fst.st_mode);

   status = temp_full_generate(dir, lay_cnt, st, interval, is_lite, node_size);
   if (status != TEMP_FULL_OK) return GEN_EXIT_FAIL;

   printf("temp_gen: %s: %s\n", path, was_there ? "reused" : "written");
   return GEN_EXIT_OK;
}
/*--------------------------------- MAIN END ---------------------------------*/


/*---------------------------- Private Functions -----------------------------*/
static void usage(FILE *stream, const char *prog)
{
   fprintf(stream,
           "usage: %s [--dir DIR] [--norm] [--node-size N] "
           "LAY_CNT ST INTERVAL\n"
           "\n"
           "Build, or reuse, the perfectly full template\n"
           "<DIR>/<lay_cnt>-<st>-<interval>.bptr.\n"
           "\n"
           "  --dir DIR       directory of the template (default %s)\n"
           "  --norm          use the 8-byte child pointer layout\n"
           "                  (default lite, 4-byte)\n"
           "  --node-size N   size of a node in bytes (default %u)\n"
           "  -h, --help      print this help and exit\n"
           "\n"
           "The template holds LAY_CNT levels of keys ST, ST + INTERVAL, ...;\n"
           "INTERVAL must be positive, so that every node of the template\n"
           "ascends.  A negative one is refused with the request that builds\n"
           "the same keys instead: interval * -1, starting at the last key.\n"
           "\n"
           "Exit status: 0 the template exists, 1 it could not be built,\n"
           "2 the command line cannot be served.\n",
           prog, TEMP_FULL_DEFAULT_DIR, GEN_NODE_SIZE_DEFAULT);
}


/**
 * @brief   Parse a whole unsigned decimal argument
 *
 * @param[in]  str  argument to parse
 * @param[out] out  parsed value
 *
 * @return  1 when @p str is a whole number that fits, 0 otherwise
 */
static _Bool _parse_u32(const char *str, uint32_t *out)
{
   unsigned long long val;
   char *end;

   errno = 0;
   val = strtoull(str, &end, 10);
   if (end == str || *end != '\0' || errno == ERANGE || val > UINT32_MAX)
      return 0;

   *out = (uint32_t)val;
   return 1;
}


/**
 * @brief   Parse a whole signed decimal argument
 *
 * @param[in]  str  argument to parse
 * @param[out] out  parsed value
 *
 * @return  1 when @p str is a whole number that fits, 0 otherwise
 */
static _Bool _parse_i64(const char *str, int64_t *out)
{
   long long val;
   char *end;

   errno = 0;
   val = strtoll(str, &end, 10);
   if (end == str || *end != '\0' || errno == ERANGE) return 0;

   *out = val;
   return 1;
}
/*-------------------------- Private Functions END ---------------------------*/
