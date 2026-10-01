/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "unity.h"
#include "unity_internals.h"
#include "templates.h"
#include "tools.h"
#include "temp_full.h"
#include "temp_split.h"
#include "bptree.h"
/*--------------------------- Private Includes END ---------------------------*/

/*------------------------------- Unity Setup --------------------------------*/
void setUp(void) { }

void tearDown(void) { }
/*----------------------------- Unity Setup END ------------------------------*/

void test_temp(void);

/*----------------------------- Fixture Utility ------------------------------*/
/**
 * @brief   Generate every template the cases below load
 *
 * A tester utility rather than a case of its own: `main' runs it directly,
 * before `UNITY_BEGIN', and stops the run when it fails.  It reports through
 * its return value instead of through a Unity assertion, which would need an
 * abort frame that does not exist outside a case; the work itself is delegated
 * to `bin/temp_gen', one run per entry of `FULL_FIXTURES'.
 *
 * @return  0 when every template exists; the status of the first `temp_gen' that
 *          failed otherwise
 */
static int gen_full_fixtures(void)
{
   for (size_t i = 0; i < FULL_FIXTURES_SZ; i++)
    {
      const struct full_fixture *fx = &FULL_FIXTURES[i];
      int status = tools_generate(TEMPLATES_DEFAULT_DIR, fx->lay_cnt, fx->st,
                                  fx->interval, fx->is_lite, fx->node_size);

      if (status != 0) return status;
    }

   return 0;
}
/*--------------------------- Fixture Utility END ----------------------------*/

/*----------------------------------- MAIN -----------------------------------*/
int main(void)
{
   int status;

   puts("Test Unit: node_split");

   /* the fixtures the cases load are an input of this unit, not a case: build
    * them here, and stop the run when they cannot be built rather than let a
    * case discover a missing image */
   status = gen_full_fixtures();
   if (status != 0)
    {
      char msg[PATH_MAX + 128];

      tools_strstatus(msg, sizeof msg, TOOLS_TEMP_GEN, status);
      fprintf(stderr, "node_split: cannot generate the fixtures: %s\n", msg);
      return EXIT_FAILURE;
    }

   UNITY_BEGIN();
   /* the template guard: it proves the images the cases below instantiate, so
    * it has to stay the first case of the unit */
   RUN_TEST(test_temp);
   /* every split case starts from a pristine copy of a template, so the guard
    * above has to have verified them first */
   RUN_TEST(test_full_split);

   return UNITY_END();
}


/**
 * @brief   Check every generated template before a modification is attempted
 *
 * The guard on the generator.  A split case is only meaningful once the image
 * it instantiates is known to be correct, so each template is loaded and walked
 * by `temp_full_verify' here, ahead of the cases that will modify one.  A
 * defect aborts the case -- and with it the run -- before anything is
 * instantiated.
 *
 * @note  Loading and unloading an image rewrites its header block: the library
 *        flushes that whole block from the shared scratch buffer it marshals
 *        nodes through, so the bytes past the header end up holding whatever
 *        node image was left there -- they change as soon as that leftover
 *        differs, though a load that fetches no node leaves the file alone.
 *        The header itself and every node block are stable; a fixture must not
 *        be pinned by the hash of the whole file.
 */
void test_temp(void)
{
   char path[PATH_MAX], msg[PATH_MAX + 32];

   for (size_t i = 0; i < FULL_FIXTURES_SZ; i++)
    {
      const struct full_fixture *fx = &FULL_FIXTURES[i];
      struct bptr *bptr;

      templates_path(path, sizeof path, TEMPLATES_DEFAULT_DIR, fx->lay_cnt,
                     fx->st, fx->interval);
      snprintf(msg, sizeof msg, "failed to load %s", path);
      /* loader cache only: the verifier reloads nodes as it walks the image */
      bptr = bptr_load(path, 256, &cmp_i64);
      TEST_ASSERT_NOT_NULL_MESSAGE(bptr, msg);

      /* name the template under check: `temp_full_verify' asserts with fixed
       * messages, so this line is what tells which image carried a defect */
      printf("  template %s\n", path);
      temp_full_verify(bptr, fx->lay_cnt, fx->st, fx->interval, 0, 0, 0);
      TEST_ASSERT_EQUAL(BPTR_E_SUCCESS, bptr_unload(bptr));
    }
}
/*--------------------------------- MAIN END ---------------------------------*/
