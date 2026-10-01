/*----------------------------- Private Includes -----------------------------*/
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "temp_full.h"
/*--------------------------- Private Includes END ---------------------------*/


/*------------------------------ Private Macros ------------------------------*/
/* Process status of the tool, part of its interface: 0 every template was
 * verified, 1 at least one check failed, 2 the command line cannot be served or
 * a name does not conform, 3 a template cannot be loaded. */
#define VERIFY_EXIT_OK    (0)
#define VERIFY_EXIT_FAIL  (1)
#define VERIFY_EXIT_USAGE (2)
#define VERIFY_EXIT_LOAD  (3)

/* suffix `temp_full_path' appends to a template name */
#define TEMPLATE_SUFFIX     ".bptr"
#define TEMPLATE_SUFFIX_LEN (sizeof TEMPLATE_SUFFIX - 1)

/* node cache capacity a template is loaded with: one node stays pinned per
 * level of the walk, and the generator serves at most 254 levels */
#define TEMPLATE_CACHE_CAP 256u
/*------------------------------ Private Macros END --------------------------*/


/*------------------------------ Private Types -------------------------------*/
/* What the case below checks: `main' fills it in one template at a time. */
struct verify_req
{
   struct bptr *bptr;        /* image loaded by `main', unloaded by the case */
   unsigned int lay_cnt;     /* number of levels it is expected to have */
   int64_t      st;          /* first key of its lattice */
   int64_t      interval;    /* distance between two successive keys */
   _Bool        has_new_kv;  /* an inserted record is expected as well */
   int64_t      key;         /* the inserted key, when there is one */
   int64_t      val;         /* the value of the inserted key */
};
/*------------------------------ Private Types END ---------------------------*/


/*----------------------- Private Variable Declarations ----------------------*/
static struct verify_req g_req;
static _Bool g_verified;   /* the case reached its end: no assertion failed */
/*--------------------- Private Variable Declarations END --------------------*/


/*------------------------------- Unity Setup --------------------------------*/
void setUp(void) { }

void tearDown(void) { }
/*----------------------------- Unity Setup END ------------------------------*/


/*--------------------------- Forward Declarations ---------------------------*/
static void test_template(void);
static void usage(FILE *stream, const char *prog);
static _Bool _parse_u32(const char *str, unsigned int *out);
static _Bool _parse_i64(const char *str, int64_t *out);
static _Bool _parse_template_name(const char *path, unsigned int *lay_cnt,
                                  int64_t *st, int64_t *interval);
/*------------------------- Forward Declarations END -------------------------*/


/*----------------------------------- MAIN -----------------------------------*/
int main(int argc, char **argv)
{
   unsigned int lay_cnt = 0;
   int64_t st = 0, interval = 0, key = 0, val = 0;
   _Bool has_lay_cnt = 0, has_st = 0, has_interval = 0;
   _Bool has_key = 0, has_val = 0, began = 0;
   int at, status = VERIFY_EXIT_OK, failures;

   /* The options come first and the templates after them: option scanning
    * stops at the first argument that is not one of the known options, so a
    * path is never read as one. */
   for (at = 1; at < argc; at++)
    {
      const char *arg = argv[at];

      if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
       { usage(stdout, argv[0]); return VERIFY_EXIT_OK; }
      else if (!strcmp(arg, "--lay-cnt"))
       {
         if (++at == argc || !_parse_u32(argv[at], &lay_cnt))
          { usage(stderr, argv[0]); return VERIFY_EXIT_USAGE; }
         has_lay_cnt = 1;
       }
      else if (!strcmp(arg, "--st"))
       {
         if (++at == argc || !_parse_i64(argv[at], &st))
          { usage(stderr, argv[0]); return VERIFY_EXIT_USAGE; }
         has_st = 1;
       }
      else if (!strcmp(arg, "--interval"))
       {
         /* a zero step is not a lattice: refuse it here, before a file is
          * loaded, rather than let the count checks divide by it */
         if (++at == argc || !_parse_i64(argv[at], &interval) || interval == 0)
          { usage(stderr, argv[0]); return VERIFY_EXIT_USAGE; }
         has_interval = 1;
       }
      else if (!strcmp(arg, "--key"))
       {
         if (++at == argc || !_parse_i64(argv[at], &key))
          { usage(stderr, argv[0]); return VERIFY_EXIT_USAGE; }
         has_key = 1;
       }
      else if (!strcmp(arg, "--val"))
       {
         if (++at == argc || !_parse_i64(argv[at], &val))
          { usage(stderr, argv[0]); return VERIFY_EXIT_USAGE; }
         has_val = 1;
       }
      else
         break;
    }
   /* at least one template, and an inserted record needs both of its numbers */
   if (at == argc || has_key != has_val)
    { usage(stderr, argv[0]); return VERIFY_EXIT_USAGE; }

   for (; at < argc; at++)
    {
      const char *path = argv[at];
      unsigned int req_lay_cnt = lay_cnt;
      int64_t req_st = st, req_interval = interval;

      /* A layout parameter the caller did not give is read off the file name:
       * a template under the name convention is complete without any flag. */
      if (!has_lay_cnt || !has_st || !has_interval)
       {
         unsigned int name_lay_cnt;
         int64_t name_st, name_interval;

         if (!_parse_template_name(path, &name_lay_cnt, &name_st,
                                   &name_interval))
          {
            fprintf(stderr, "temp_verify: %s: not a "
                            "<lay_cnt>-<st>-<interval>.bptr name; pass "
                            "--lay-cnt, --st and --interval\n", path);
            status = VERIFY_EXIT_USAGE;
            break;
          }
         if (!has_lay_cnt) req_lay_cnt = name_lay_cnt;
         if (!has_st) req_st = name_st;
         if (!has_interval) req_interval = name_interval;
       }
      if (req_interval == 0)
       {
         fprintf(stderr, "temp_verify: %s: interval 0 is not a key lattice\n",
                 path);
         status = VERIFY_EXIT_USAGE;
         break;
       }

      g_req.bptr = bptr_load(path, TEMPLATE_CACHE_CAP, &cmp_i64);
      if (g_req.bptr == NULL)
       {
         fprintf(stderr, "temp_verify: %s: cannot be loaded (bptr_errno %d)\n",
                 path, bptr_errno);
         status = VERIFY_EXIT_LOAD;
         continue;
       }
      g_req.lay_cnt = req_lay_cnt;
      g_req.st = req_st;
      g_req.interval = req_interval;
      g_req.has_new_kv = has_key;
      g_req.key = key;
      g_req.val = val;
      g_verified = 0;
      /* a run in which nothing could be loaded produces no Unity output at
       * all: there is no verdict to print, only the reason on stderr */
      if (!began)
       { UNITY_BEGIN(); began = 1; }
      /* `temp_full_verify' asserts, so it needs the abort frame a case
       * installs: a defect fails the case and leaves `g_verified' clear */
      RUN_TEST(test_template);
      if (g_verified) printf("temp_verify: %s: ok\n", path);
    }
   failures = began ? UNITY_END() : 0;

   if (status != VERIFY_EXIT_OK) return status;
   return failures != 0 ? VERIFY_EXIT_FAIL : VERIFY_EXIT_OK;
}
/*--------------------------------- MAIN END ---------------------------------*/


/*---------------------------- Private Functions -----------------------------*/
/**
 * @brief   Verify the template `main' loaded into `g_req.bptr'
 *
 * The case has to be the frame: `temp_full_verify' asserts with fixed messages,
 * and an assertion longjmps back here, leaving `g_verified' clear so that
 * `main' does not report a template it could not check as ok.  The loaded image
 * then leaks with the process, which is about to exit.
 */
static void test_template(void)
{
   temp_full_verify(g_req.bptr, g_req.lay_cnt, g_req.st, g_req.interval,
                    g_req.has_new_kv, g_req.key, g_req.val);
   TEST_ASSERT_EQUAL_MESSAGE(BPTR_E_SUCCESS, bptr_unload(g_req.bptr),
                             "cannot unload the image");
   g_verified = 1;
}


static void usage(FILE *stream, const char *prog)
{
   fprintf(stream,
           "usage: %s [--lay-cnt N] [--st S] [--interval I]\n"
           "       [--key K --val V] TEMPLATE...\n"
           "\n"
           "Check every TEMPLATE against the perfectly full tree of its\n"
           "layout.  A layout parameter that is not given is read off the\n"
           "template name <lay_cnt>-<st>-<interval>.bptr; --key/--val tell\n"
           "the checker that one record was inserted (both or neither).\n"
           "\n"
           "  --lay-cnt N     number of levels\n"
           "  --st S          first key of the lattice\n"
           "  --interval I    distance between two successive keys, not 0\n"
           "  --key K         the inserted key\n"
           "  --val V         the value of the inserted key\n"
           "  -h, --help      print this help and exit\n"
           "\n"
           "Exit status: 0 every template verified, 1 at least one check\n"
           "failed, 2 the command line cannot be served, 3 a template\n"
           "cannot be loaded.\n",
           prog);
}


/**
 * @brief   Parse a whole unsigned decimal argument
 *
 * @param[in]  str  argument to parse
 * @param[out] out  parsed value
 *
 * @return  1 when @p str is a whole number that fits, 0 otherwise
 */
static _Bool _parse_u32(const char *str, unsigned int *out)
{
   unsigned long long val;
   char *end;

   errno = 0;
   val = strtoull(str, &end, 10);
   if (end == str || *end != '\0' || errno == ERANGE || val > UINT_MAX)
      return 0;

   *out = (unsigned int)val;
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


/**
 * @brief   Read `<lay_cnt>-<st>-<interval>' off a template path
 *
 * The name convention of `temp_full_path': the last path component without its
 * `.bptr' suffix holds the three decimal numbers, separated by '-'.  A name
 * that does not conform is an error, not a default -- the layout of an image is
 * not recoverable from the image itself.
 *
 * @param[in]  path       template path to read
 * @param[out] lay_cnt    number of levels
 * @param[out] st         first key
 * @param[out] interval   distance between two successive keys
 *
 * @return  1 when @p path conforms, 0 otherwise
 */
static _Bool _parse_template_name(const char *path, unsigned int *lay_cnt,
                                  int64_t *st, int64_t *interval)
{
   char stem[64];
   const char *base, *p;
   char *end;
   size_t len, stem_len;
   unsigned long long lc;
   long long st_v, interval_v;

   base = strrchr(path, '/');
   base = base == NULL ? path : base + 1;
   len = strlen(base);
   if (len <= TEMPLATE_SUFFIX_LEN) return 0;
   stem_len = len - TEMPLATE_SUFFIX_LEN;
   if (stem_len >= sizeof stem ||
       memcmp(base + stem_len, TEMPLATE_SUFFIX, TEMPLATE_SUFFIX_LEN))
      return 0;
   memcpy(stem, base, stem_len);
   stem[stem_len] = '\0';

   p = stem;
   if (*p < '0' || *p > '9') return 0;
   errno = 0;
   lc = strtoull(p, &end, 10);
   if (end == p || errno == ERANGE || lc == 0 || lc > UINT_MAX) return 0;
   p = end;
   if (*p++ != '-') return 0;

   errno = 0;
   st_v = strtoll(p, &end, 10);
   if (end == p || errno == ERANGE) return 0;
   p = end;
   if (*p++ != '-') return 0;

   errno = 0;
   interval_v = strtoll(p, &end, 10);
   if (end == p || errno == ERANGE || *end != '\0') return 0;

   *lay_cnt = (unsigned int)lc;
   *st = st_v;
   *interval = interval_v;
   return 1;
}
/*-------------------------- Private Functions END ---------------------------*/
