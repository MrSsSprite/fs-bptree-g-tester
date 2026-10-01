/*----------------------------- Private Includes -----------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "unity.h"
#include "unity_internals.h"
#include "templates.h"
#include "tools.h"
#include "temp_split.h"
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
int main(int argc, char *argv[])
{
   char reason[PATH_MAX + 128];
   int status;

   puts("Test Unit: node_split");

   if (argc > 2)
    {
      fprintf(stderr, "usage: %s [TEMPLATE_DIR]\n", argv[0]);
      return EXIT_FAILURE;
    }

   if (argc == 2)
    {
      /* a directory of ready-made templates: the cases instantiate what they
       * find there, so nothing is generated for them */
      printf("node_split: using the templates in %s\n", argv[1]);
      status = templates_load_dir(argv[1], reason, sizeof reason);
    }
   else
    {
      /* the fixtures the cases load are an input of this unit, not a case:
       * build them here, and stop the run when they cannot be built rather
       * than let a case discover a missing image */
      printf("node_split: no template directory given; using the default %s\n",
             TEMPLATES_DEFAULT_DIR);
      status = gen_full_fixtures();
      if (status != 0)
       {
         char msg[PATH_MAX + 128];

         tools_strstatus(msg, sizeof msg, TOOLS_TEMP_GEN, status);
         fprintf(stderr, "node_split: cannot generate the fixtures: %s\n",
                 msg);
         return EXIT_FAILURE;
       }

      status = templates_load_default(reason, sizeof reason);
    }

   if (status != TEMPLATES_OK)
    {
      fprintf(stderr, "node_split: %s\n", reason);
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
 * The guard on the templates.  A split case is only meaningful once the image
 * it instantiates is known to be correct, so every template of the unit's list
 * is handed to `bin/temp_verify' here, ahead of the cases that will modify one.
 * A defect aborts the case -- and with it the run -- before anything is
 * instantiated, and the message names the tool, the path and its status.
 */
void test_temp(void)
{
   char msg[PATH_MAX + 160], err[PATH_MAX + 128];
   const struct template *tmpls;
   size_t cnt;

   tmpls = templates_get(&cnt);
   for (size_t i = 0; i < cnt; i++)
    {
      const struct template *tmpl = &tmpls[i];
      int status;

      /* name the template under check: the tool reports a defect with its own
       * Unity output, so this line is what tells which image carried it */
      printf("  template %s\n", tmpl->path);
      status = tools_verify(tmpl->lay_cnt, tmpl->st, tmpl->interval, 0, 0, 0,
                            tmpl->path);
      tools_strstatus(err, sizeof err, TOOLS_TEMP_VERIFY, status);
      snprintf(msg, sizeof msg, "failed to verify %s: %s", tmpl->path, err);
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, msg);
    }
}
/*--------------------------------- MAIN END ---------------------------------*/
