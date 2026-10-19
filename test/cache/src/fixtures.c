/*----------------------------- Private Includes -----------------------------*/
#include "fixtures.h"
#include "unity.h"
#include "bptree.h"
#include "test_bptr_setup.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
/*--------------------------- Private Includes END ---------------------------*/


/*---------------------------- Private Variables -----------------------------*/
static int caller_dir = -1;
static char run_dir[] = "bptr_files/cache-XXXXXX";
static struct bptr *fixture_tree;
static char fixture_path[256];
static _Bool fixture_owned;
/*-------------------------- Private Variables END ---------------------------*/


/*----------------------------- Public Functions -----------------------------*/
/** Isolate relative fixture paths without touching caller-prepared images. */
int cache_fixtures_begin(void)
{
   if (mkdir("bptr_files", 0755) != 0 && errno != EEXIST)
    { perror("cache: create fixture parent"); return -1; }
   caller_dir = open(".", O_RDONLY);
   if (caller_dir < 0)
    { perror("cache: save working directory"); return -1; }
   if (mkdtemp(run_dir) == NULL)
    {
      perror("cache: create private fixture directory");
      close(caller_dir);
      caller_dir = -1;
      return -1;
    }
   if (chdir(run_dir) != 0)
    {
      perror("cache: enter private fixture directory");
      rmdir(run_dir);
      close(caller_dir);
      caller_dir = -1;
      return -1;
    }
   return 0;
}


int cache_fixtures_end(void)
{
   int result = 0;

   if (rmdir("bptr_files") != 0 && errno != ENOENT)
    { perror("cache: remove fixture files directory"); result = -1; }
   if (fchdir(caller_dir) != 0)
    { perror("cache: restore working directory"); result = -1; }
   else if (rmdir(run_dir) != 0)
    { perror("cache: remove private fixture directory"); result = -1; }
   if (close(caller_dir) != 0)
    { perror("cache: close working directory"); result = -1; }
   caller_dir = -1;
   return result;
}


/** Register ownership before asserting, including an unexpected init result. */
struct bptr *cache_fixture_try_create(const struct bptr_temp *template)
{
   TEST_ASSERT_FALSE_MESSAGE(fixture_owned, "previous cache fixture is still owned");
   _bptr_path(fixture_path, sizeof fixture_path, template->fnm);
   fixture_owned = 1;
   fixture_tree = bptr_init(fixture_path, template->is_lite,
                            template->node_sz, template->key_sz, template->val_sz,
                            template->cache_cap, template->cmp);
   return fixture_tree;
}


struct bptr *cache_fixture_create(const struct bptr_temp *template)
{
   struct bptr *tree = cache_fixture_try_create(template);
   TEST_ASSERT_NOT_NULL_MESSAGE(tree, "cannot create cache fixture");
   return tree;
}


int cache_fixture_unload(void)
{
   int result = bptr_unload(fixture_tree);
   /* After success the handle is freed; a failed test must not unload it twice. */
   if (result == BPTR_E_SUCCESS)
      fixture_tree = NULL;
   return result;
}


struct bptr *cache_fixture_load(uint64_t capacity, int (*compare)(const void *, const void *))
{
   TEST_ASSERT_TRUE_MESSAGE(fixture_owned, "no owned fixture to load");
   TEST_ASSERT_NULL_MESSAGE(fixture_tree, "fixture is still open");
   fixture_tree = bptr_load(fixture_path, capacity, compare);
   TEST_ASSERT_NOT_NULL_MESSAGE(fixture_tree, "cannot reload cache fixture");
   return fixture_tree;
}


int cache_fixture_discard(void)
{
   int unload_status = BPTR_E_SUCCESS, remove_status = 0;

   if (fixture_tree != NULL)
      unload_status = cache_fixture_unload();
   if (fixture_owned)
    {
      if (remove(fixture_path) != 0 && errno != ENOENT)
         remove_status = -1;
      fixture_owned = 0;
    }
   return unload_status != BPTR_E_SUCCESS || remove_status != 0;
}
/*--------------------------- Public Functions END ---------------------------*/
