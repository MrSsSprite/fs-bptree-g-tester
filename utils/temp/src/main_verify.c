/*----------------------------- Private Includes -----------------------------*/
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "temp_full.h"
#include "temp_manifest.h"
/*--------------------------- Private Includes END ---------------------------*/

#define VERIFY_EXIT_OK    (0)
#define VERIFY_EXIT_FAIL  (1)
#define VERIFY_EXIT_USAGE (2)
#define VERIFY_EXIT_LOAD  (3)
#define TEMPLATE_CACHE_CAP 256u

/* One Unity case owns each loaded image and its assertion abort frame. */
struct verify_req
{
   struct bptr *bptr;
   unsigned int lay_cnt;
   int64_t st, interval, key, val;
   _Bool has_new_kv;
   const struct temp_shape *shape;
   const struct temp_verify_expect *expected;
};

static struct verify_req g_req;
static _Bool g_verified;

void setUp(void) { }
void tearDown(void) { }

static void test_template(void);
static void usage(FILE *stream, const char *prog);
static _Bool _parse_u32(const char *str, unsigned int *out);
static _Bool _parse_u64(const char *str, uint64_t *out);
static _Bool _parse_i64(const char *str, int64_t *out);
static int _preflight_shape(const char *path, unsigned int levels,
                            const struct temp_shape *shape);

int main(int argc, char **argv)
{
   for (int i = 1; i < argc; ++i)
      if (!strcmp(argv[i], "--expect")) return temp_manifest_cli(argc, argv, 0);
   unsigned int lay_cnt = 0;
   int64_t st = 0, interval = 0, key = 0, val = 0;
   _Bool has_lay_cnt = 0, has_st = 0, has_interval = 0;
   _Bool has_key = 0, has_val = 0, began = 0;
   _Bool has_height = 0, has_nodes = 0, has_leaves = 0;
   struct temp_shape explicit_shape = {0};
   struct temp_verify_expect expected = {0};
   int at, status = VERIFY_EXIT_OK, failures;

   /* Options precede paths, as in the original utility interface. */
   for (at = 1; at < argc; at++)
    {
      const char *arg = argv[at];
      if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
       {
         usage(stdout, argv[0]);
         temp_shape_free(&explicit_shape);
         return VERIFY_EXIT_OK;
       }
      else if (!strcmp(arg, "--lay-cnt"))
       {
         if (++at == argc || !_parse_u32(argv[at], &lay_cnt) || lay_cnt == 0)
            goto BAD_USAGE;
         has_lay_cnt = 1;
       }
      else if (!strcmp(arg, "--st"))
       {
         if (++at == argc || !_parse_i64(argv[at], &st)) goto BAD_USAGE;
         has_st = 1;
       }
      else if (!strcmp(arg, "--interval"))
       {
         if (++at == argc || !_parse_i64(argv[at], &interval) || interval == 0)
            goto BAD_USAGE;
         has_interval = 1;
       }
      else if (!strcmp(arg, "--key"))
       {
         if (++at == argc || !_parse_i64(argv[at], &key)) goto BAD_USAGE;
         has_key = 1;
       }
      else if (!strcmp(arg, "--val"))
       {
         if (++at == argc || !_parse_i64(argv[at], &val)) goto BAD_USAGE;
         has_val = 1;
       }
      else if (!strcmp(arg, "--node-keys"))
       {
         if (++at == argc || temp_shape_add(&explicit_shape, argv[at]) != 0)
            goto BAD_USAGE;
       }
      else if (!strcmp(arg, "--expect-height"))
       {
         unsigned int height;
         if (++at == argc || !_parse_u32(argv[at], &height) || height == 0)
            goto BAD_USAGE;
         expected.height = height;
         has_height = 1;
       }
      else if (!strcmp(arg, "--expect-nodes"))
       {
         if (++at == argc || !_parse_u64(argv[at], &expected.nodes) ||
             expected.nodes == 0) goto BAD_USAGE;
         has_nodes = 1;
       }
      else if (!strcmp(arg, "--expect-leaves"))
       {
         if (++at == argc || !_parse_u64(argv[at], &expected.leaves) ||
             expected.leaves == 0) goto BAD_USAGE;
         has_leaves = 1;
       }
      else
         break;
    }
   if (at == argc || has_key != has_val ||
       has_height != has_nodes || has_height != has_leaves ||
       (has_height && !has_key)) goto BAD_USAGE;

   for (; at < argc; at++)
    {
      const char *path = argv[at];
      const char *base = strrchr(path, '/');
      unsigned int req_lay_cnt = lay_cnt, name_lay_cnt = 0;
      int64_t req_st = st, req_interval = interval;
      int64_t name_st = 0, name_interval = 0;
      struct temp_shape name_shape = {0};
      const struct temp_shape *shape;
      int shape_status;
      _Bool named = temp_parse_name(path, &name_lay_cnt, &name_st,
                                    &name_interval, &name_shape);

      base = base == NULL ? path : base + 1;
      if ((!has_lay_cnt || !has_st || !has_interval) && !named)
       {
         fprintf(stderr, "temp_verify: %s: not a fixture name; pass "
                         "--lay-cnt, --st and --interval\n", path);
         temp_shape_free(&name_shape);
         status = VERIFY_EXIT_USAGE;
         break;
       }
      if (!has_lay_cnt) req_lay_cnt = name_lay_cnt;
      if (!has_st) req_st = name_st;
      if (!has_interval) req_interval = name_interval;
      /* Parsing the suffix is independent of scalar overrides. Explicit
       * node options replace the whole suffix, including any full counts. */
      if (explicit_shape.override_count == 0 && !named &&
          strstr(base, "--keys-") != NULL)
       {
         fprintf(stderr, "temp_verify: %s: malformed node occupancy suffix\n",
                 path);
         temp_shape_free(&name_shape);
         status = VERIFY_EXIT_USAGE;
         break;
       }
      shape = explicit_shape.override_count != 0 ? &explicit_shape : &name_shape;
      if (req_lay_cnt == 0 || req_interval == 0 ||
          (shape->override_count != 0 && has_key && !has_height))
       {
         fprintf(stderr, "temp_verify: %s: %s\n", path,
            req_lay_cnt == 0 ? "original level count must be positive" :
            req_interval == 0 ? "interval 0 is not a key lattice" :
            "custom inserted images require --expect-height, --expect-nodes "
            "and --expect-leaves");
         temp_shape_free(&name_shape);
         status = VERIFY_EXIT_USAGE;
         break;
       }
      shape_status = _preflight_shape(path, req_lay_cnt, shape);
      if (shape_status != TEMP_FULL_OK)
       {
         fprintf(stderr, "temp_verify: %s: %s\n", path,
                 temp_full_strerror(shape_status));
         temp_shape_free(&name_shape);
         status = VERIFY_EXIT_USAGE;
         break;
       }

      g_req.bptr = bptr_load(path, TEMPLATE_CACHE_CAP, &cmp_i64);
      if (g_req.bptr == NULL)
       {
         fprintf(stderr, "temp_verify: %s: cannot be loaded (bptr_errno %d)\n",
                 path, bptr_errno);
         temp_shape_free(&name_shape);
         status = VERIFY_EXIT_LOAD;
         continue;
       }
      g_req.lay_cnt = req_lay_cnt;
      g_req.st = req_st;
      g_req.interval = req_interval;
      g_req.has_new_kv = has_key;
      g_req.key = key;
      g_req.val = val;
      g_req.shape = shape;
      g_req.expected = has_height ? &expected : NULL;
      g_verified = 0;
      if (!began)
       { UNITY_BEGIN(); began = 1; }
      RUN_TEST(test_template);
      if (g_verified) printf("temp_verify: %s: ok\n", path);
      temp_shape_free(&name_shape);
    }
   failures = began ? UNITY_END() : 0;
   temp_shape_free(&explicit_shape);
   if (status != VERIFY_EXIT_OK) return status;
   return failures != 0 ? VERIFY_EXIT_FAIL : VERIFY_EXIT_OK;

BAD_USAGE:
   usage(stderr, argv[0]);
   temp_shape_free(&explicit_shape);
   return VERIFY_EXIT_USAGE;
}

static void test_template(void)
{
   temp_verify(g_req.bptr, g_req.lay_cnt, g_req.st, g_req.interval,
                 g_req.has_new_kv, g_req.key, g_req.val,
                 g_req.shape, g_req.expected);
   TEST_ASSERT_EQUAL_INT_MESSAGE(BPTR_E_SUCCESS, bptr_unload(g_req.bptr),
                                 "cannot unload the image");
   g_verified = 1;
}

static void usage(FILE *stream, const char *prog)
{
   fprintf(stream,
      "usage: %s [--lay-cnt N] [--st S] [--interval I]\n"
      "       [--node-keys PATH=COUNT]... [--key K --val V]\n"
      "       [--expect-height H --expect-nodes N --expect-leaves L] TEMPLATE...\n"
      "       Manifest mode: --expect FILE IMAGE [--check contents,structure[,shape]]\n"
      "                     [--comparator-library LIB] [--json]\n"
      "\n"
      "Check pristine fixtures or an image with one inserted record. Missing\n"
      "layout parameters and node occupancies come from the fixture filename.\n"
      "Explicit --node-keys options replace all filename occupancies.\n"
      "\n"
      "  --lay-cnt N       original number of levels\n"
      "  --st S            first original lattice key\n"
      "  --interval I      lattice step, not 0\n"
      "  --node-keys P=C   original node keys; P is root or a path such as 0.1\n"
      "  --key K --val V   exactly one inserted record (both or neither)\n"
      "  --expect-height H --expect-nodes N --expect-leaves L\n"
      "                    final totals; all three together, only with an\n"
      "                    inserted record, required for custom occupancies\n"
      "  -h, --help        print this help and exit\n"
      "\n"
      "Exit status: 0 every template verified, 1 at least one check failed,\n"
      "2 invalid parameters, 3 a template cannot be loaded.\n", prog);
}

static _Bool _parse_u64(const char *str, uint64_t *out)
{
   unsigned long long value;
   char *end;
   if (*str < '0' || *str > '9') return 0;
   for (const char *p = str; *p; p++)
      if (*p < '0' || *p > '9') return 0;
   errno = 0;
   value = strtoull(str, &end, 10);
   if (end == str || *end != '\0' || errno == ERANGE || value > UINT64_MAX)
      return 0;
   *out = (uint64_t)value;
   return 1;
}

static _Bool _parse_u32(const char *str, unsigned int *out)
{
   uint64_t value;
   if (!_parse_u64(str, &value) || value > UINT_MAX) return 0;
   *out = (unsigned int)value;
   return 1;
}

static _Bool _parse_i64(const char *str, int64_t *out)
{
   long long value;
   char *end;
   const char *digits = (*str == '-' || *str == '+') ? str + 1 : str;
   if (*digits < '0' || *digits > '9') return 0;
   for (const char *p = digits; *p; p++)
      if (*p < '0' || *p > '9') return 0;
   errno = 0;
   value = strtoll(str, &end, 10);
   if (end == str || *end != '\0' || errno == ERANGE) return 0;
   *out = value;
   return 1;
}

/* Semantic selector errors must be parameter errors before bptr_load, whose
 * current implementation opens the image for update. A short/invalid header
 * is left to the normal loader and its established load-error convention. */
static int _preflight_shape(const char *path, unsigned int levels,
                            const struct temp_shape *shape)
{
   uint32_t header[3];
   struct temp_shape_counts counts;
   FILE *file;
   size_t read_count;
   if (shape->override_count == 0) return TEMP_FULL_OK;
   file = fopen(path, "rb");
   if (file == NULL) return TEMP_FULL_OK;
   read_count = fread(header, sizeof header, 1, file);
   fclose(file);
   if (read_count != 1 || memcmp(header, "BPTR", 4) != 0 ||
       (header[1] & 0x7f) != BPTR_CURRENT_VERSION) return TEMP_FULL_OK;
   return temp_shape_count(shape, levels, (header[1] & 0x80) != 0,
                            header[2], &counts);
}
