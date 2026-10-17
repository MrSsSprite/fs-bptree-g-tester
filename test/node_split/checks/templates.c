#include "../src/templates.h"
#include "../src/tools.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static unsigned int directory_id;
static char reason[1024];

void setUp(void) { }
void tearDown(void) { templates_cleanup(); }

static void directory(char *path, size_t size)
{
   snprintf(path, size, "fixtures-%u", directory_id++);
   TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
}

static void entry(const char *dir, const char *name)
{
   char path[PATH_MAX];
   FILE *file;
   int needed = snprintf(path, sizeof path, "%s/%s", dir, name);
   TEST_ASSERT_TRUE(needed > 0 && (size_t)needed < sizeof path);
   file = fopen(path, "wb");
   TEST_ASSERT_NOT_NULL(file);
   TEST_ASSERT_EQUAL_INT(0, fclose(file));
}

static int load(const char *dir)
{
   return templates_load_dir(dir, reason, sizeof reason);
}

static void test_canonical_full_and_custom_coexist(void)
{
   const char *names[] = {
      "4-0-16--keys-root=5.bptr",
      "4-0-16.bptr",
      "4-0-16--keys-root=1,0=4,0.1=2.bptr",
      "2-0-16--keys-root=4.bptr",
      "4-0-16--keys-0=4.bptr",
      "3-0-16.bptr", "3--8-16.bptr", "3-0-8.bptr"
   };
   const char *ordered[] = {
      "2-0-16--keys-root=4.bptr",
      "3--8-16.bptr", "3-0-8.bptr", "3-0-16.bptr",
      "4-0-16--keys-0=4.bptr",
      "4-0-16--keys-root=1,0=4,0.1=2.bptr",
      "4-0-16--keys-root=5.bptr",
      "4-0-16.bptr"
   };
   const size_t overrides[] = {1, 0, 0, 0, 1, 3, 1, 0};
   char dir[64];
   size_t count;
   const struct template *list;

   directory(dir, sizeof dir);
   for (size_t i = 0; i < sizeof names / sizeof *names; i++) entry(dir, names[i]);
   entry(dir, "unrelated.txt");
   TEST_ASSERT_EQUAL_INT_MESSAGE(TEMPLATES_OK, load(dir), reason);
   list = templates_get(&count);
   TEST_ASSERT_EQUAL_UINT(sizeof ordered / sizeof *ordered, count);
   for (size_t i = 0; i < count; i++)
    {
      const char *base = strrchr(list[i].path, '/');
      TEST_ASSERT_NOT_NULL(base);
      TEST_ASSERT_EQUAL_STRING(ordered[i], base + 1);
      TEST_ASSERT_EQUAL_UINT(overrides[i], list[i].shape.override_count);
    }
   TEST_ASSERT_EQUAL_UINT32(5, temp_shape_keys(list[6].shape.root, 99));
}

static void test_aliases_and_malformed_names_are_rejected(void)
{
   const char *names[] = {
      "04-0-16.bptr", "+4-0-16.bptr", "4-00-16.bptr", "4-0-016.bptr",
      "0-0-16.bptr", "4294967296-0-16.bptr",
      "4-0-16--keys-root=04.bptr", "4-0-16--keys-00=4.bptr",
      "4-0-16--keys-1=3,0=4.bptr", "4-0-16--keys-0=4,0=4.bptr",
      "4-0-16--keys-root=4,.bptr", "4-0-16--keys-0..1=2.bptr"
   };
   for (size_t i = 0; i < sizeof names / sizeof *names; i++)
    {
      char dir[64];
      size_t count;
      directory(dir, sizeof dir);
      entry(dir, names[i]);
      TEST_ASSERT_EQUAL_INT_MESSAGE(TEMPLATES_E_NAME, load(dir), names[i]);
      TEST_ASSERT_TRUE(reason[0] != '\0');
      TEST_ASSERT_NULL(templates_get(&count));
      TEST_ASSERT_EQUAL_UINT(0, count);
    }
}

static void test_failed_load_preserves_list_and_cleanup_resets_it(void)
{
   char original[64], invalid[64], replacement[64], empty[64];
   size_t count;
   const struct template *before, *list;

   directory(original, sizeof original);
   entry(original, "4-0-16--keys-root=1,0=4.bptr");
   TEST_ASSERT_EQUAL_INT_MESSAGE(TEMPLATES_OK, load(original), reason);
   before = templates_get(&count);
   TEST_ASSERT_EQUAL_UINT(1, count);
   TEST_ASSERT_EQUAL_UINT(2, before[0].shape.override_count);

   directory(invalid, sizeof invalid);
   entry(invalid, "2-0-16--keys-root=4.bptr");
   entry(invalid, "bad.bptr");
   TEST_ASSERT_EQUAL_INT(TEMPLATES_E_NAME, load(invalid));
   TEST_ASSERT_EQUAL_PTR(before, templates_get(&count));
   TEST_ASSERT_EQUAL_UINT(2, before[0].shape.override_count);
   TEST_ASSERT_EQUAL_INT(TEMPLATES_E_DIR, load("missing-directory"));
   directory(empty, sizeof empty);
   TEST_ASSERT_EQUAL_INT(TEMPLATES_E_EMPTY, load(empty));
   TEST_ASSERT_EQUAL_PTR(before, templates_get(&count));

   directory(replacement, sizeof replacement);
   entry(replacement, "1--8-16.bptr");
   TEST_ASSERT_EQUAL_INT_MESSAGE(TEMPLATES_OK, load(replacement), reason);
   list = templates_get(&count);
   TEST_ASSERT_EQUAL_UINT(1, count);
   TEST_ASSERT_EQUAL_UINT(0, list[0].shape.override_count);
   TEST_ASSERT_EQUAL_INT64(-8, list[0].st);
   templates_cleanup();
   templates_cleanup();
   TEST_ASSERT_NULL(templates_get(&count));
   TEST_ASSERT_EQUAL_UINT(0, count);
}

static void test_comparator_handles_signed_extremes(void)
{
   int64_t minimum = INT64_MIN, maximum = INT64_MAX;
   TEST_ASSERT_LESS_THAN_INT(0, cmp_i64(&minimum, &maximum));
   TEST_ASSERT_GREATER_THAN_INT(0, cmp_i64(&maximum, &minimum));
   TEST_ASSERT_EQUAL_INT(0, cmp_i64(&maximum, &maximum));
}

/* The Python check runs this mode beside a fake verifier so it can assert the
 * exact argument vector without requiring a fixture or loading a tree. */
static int verify_driver(const char *mode, const char *name)
{
   struct template tmpl = {0};
   struct temp_verify_expect expected = {4, 254, 211};
   _Bool inserted = strcmp(mode, "--verify-insert") == 0;
   int status;
   if (!temp_parse_name(name, &tmpl.lay_cnt, &tmpl.st, &tmpl.interval, &tmpl.shape))
      return 2;
   snprintf(tmpl.path, sizeof tmpl.path, "%s", name);
   status = tools_verify(&tmpl, inserted, 1, 2,
                          inserted && tmpl.shape.override_count != 0
                             ? &expected : NULL,
                          "instance.bptr");
   temp_shape_free(&tmpl.shape);
   return status;
}

int main(int argc, char **argv)
{
   if (argc == 3 && (!strcmp(argv[1], "--verify-pristine") ||
                    !strcmp(argv[1], "--verify-insert")))
      return verify_driver(argv[1], argv[2]);
   UNITY_BEGIN();
   RUN_TEST(test_canonical_full_and_custom_coexist);
   RUN_TEST(test_aliases_and_malformed_names_are_rejected);
   RUN_TEST(test_failed_load_preserves_list_and_cleanup_resets_it);
   RUN_TEST(test_comparator_handles_signed_extremes);
   return UNITY_END();
}
