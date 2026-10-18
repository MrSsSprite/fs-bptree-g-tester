/*----------------------------- Private Includes -----------------------------*/
#include "search.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
/*--------------------------- Private Includes END ---------------------------*/

void setUp(void) { }
void tearDown(void) { search_cleanup(); }

/*----------------------------------- MAIN -----------------------------------*/
int main(int argc, char *argv[])
{
   const char *dir = argc == 2 ? argv[1] : TEMPLATES_DEFAULT_DIR;
   const struct template *templates;
   size_t count;
   char reason[PATH_MAX + 128];
   int status;

   puts("Test Unit: search");
   if (argc > 2)
    {
      fprintf(stderr, "usage: %s [TEMPLATE_DIR]\n", argv[0]);
      return EXIT_FAILURE;
    }
   if (argc == 2)
      printf("search: using the templates in %s\n", dir);
   else
      printf("search: no template directory given; using the default %s\n", dir);

   status = templates_load_dir(dir, reason, sizeof reason);
   if (status == TEMPLATES_E_DIR || status == TEMPLATES_E_EMPTY)
    {
      fprintf(stderr, "search: warning: nothing to test: %s\n", reason);
      return EXIT_SUCCESS;
    }
   if (status != TEMPLATES_OK)
    {
      fprintf(stderr, "search: %s\n", reason);
      return EXIT_FAILURE;
    }

   templates = templates_get(&count);
   UNITY_BEGIN();
   for (size_t i = 0; i < count; i++)
    {
      search_template = &templates[i];
      RUN_TEST(test_search_hits);
      RUN_TEST(test_search_misses);
    }
   status = UNITY_END();
   templates_cleanup();
   return status;
}
/*--------------------------------- MAIN END ---------------------------------*/
