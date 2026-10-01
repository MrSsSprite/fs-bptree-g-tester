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

/*----------------------------------- MAIN -----------------------------------*/
/**
 * @brief   Test the templates the caller prepared
 *
 * The unit owns no template: `DIR` -- the one argument, or
 * `TEMPLATES_DEFAULT_DIR` when none is given -- is scanned for
 * `<lay_cnt>-<st>-<interval>.bptr' images, and the cases walk what it holds.
 * Preparing them is the caller's job (`utils/temp`: `bin/temp_gen` writes one,
 * `bin/temp_inst` copies one); this unit only reads, split-tests and verifies
 * them, so it never generates a template of its own.
 *
 * A directory that yields no template is not a failure: the caller gave the
 * unit nothing to test, so the run warns on stderr and exits successfully
 * without starting Unity.  Only something that is offered as a template and
 * cannot be read as one -- a `.bptr' name that does not follow the convention,
 * or an image no case can understand -- fails the run.
 */
int main(int argc, char *argv[])
{
   const char *dir;
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
      /* the directory of ready-made templates the caller prepared */
      dir = argv[1];
      printf("node_split: using the templates in %s\n", dir);
    }
   else
    {
      dir = TEMPLATES_DEFAULT_DIR;
      printf("node_split: no template directory given; using the default %s\n",
             dir);
    }

   status = templates_load_dir(dir, reason, sizeof reason);
   /* Nothing to test is not a failure: a missing or empty directory is the
    * caller saying "no template this time", so the run warns and succeeds. */
   if (status == TEMPLATES_E_DIR || status == TEMPLATES_E_EMPTY)
    {
      fprintf(stderr, "node_split: warning: nothing to test: %s\n", reason);
      return EXIT_SUCCESS;
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
/*--------------------------------- MAIN END ---------------------------------*/


/**
 * @brief   Check every template before a modification is attempted
 *
 * The guard on the templates.  A split case is only meaningful once the image
 * it instantiates is known to be correct, so every template of the unit's list
 * is handed to `bin/temp_verify' here, ahead of the case that will modify one.
 * A defect fails this case first, but Unity continues with `test_full_split',
 * which then fails on copies of the same image as well; the run's exit code is
 * the verdict, so a defect is never a false pass.  The message names the tool,
 * the path and its status.
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
