#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "unity.h"
#include "bptree.h"
#include "bptr_internal.h"
#include "test_bptr_temp.h"

void _bptr_path(char *dest, size_t sz, const char *filename)
{
   snprintf(dest, sz, "bptr_files/%s", filename);
   // Ensure bptr_files directory exists
   if (mkdir("bptr_files", 0755) != 0 && errno != EEXIST)
   {
      TEST_FAIL_MESSAGE("Failed to create bptr_files directory");
   }
}


void _bptr_path_subdir
 (char *dest, size_t sz, const char *filename, const char *subdir)
{
   char subpath[256], *it;

   if (mkdir("bptr_files", 0755) != 0 && errno != EEXIST)
      TEST_FAIL_MESSAGE("Failed to create bptr_files directory");
   it = stpcpy(subpath, "bptr_files/");
   for (; it < subpath + sizeof(subpath) - 2 && *subdir; it++, subdir++)
    {
      if (*subdir == '/' && mkdir(subpath, 0755) != 0 && errno != EEXIST)
         TEST_FAIL_MESSAGE("Failed to create sub-directory");
      *it = *subdir;
    }
   if (*subdir != '/')
    {
      if (mkdir(subpath, 0755) != 0 && errno != EEXIST)
         TEST_FAIL_MESSAGE("Failed to create sub-directory");
      *it++ = '/';
    }
   *it = '\0';
   TEST_ASSERT_LESS_THAN_size_t_MESSAGE(
      sz, strlen(subpath) + strlen(filename) + 1,
      "_bptr_path_subdir: path too long");
   strcpy(stpcpy(dest, subpath), filename);
}

struct bptr *_bptr_create(struct bptr_temp *template)
{
   char path[256];
   _bptr_path(path, sizeof(path), template->fnm);
   struct bptr *bptr = bptr_init(path, template->is_lite,
                                 template->node_sz, template->key_sz,
                                 template->val_sz, template->cache_cap,
                                 template->cmp);
   TEST_ASSERT_MESSAGE(bptr, "`_bptr_create': `bptr_init' returned NULL");
   return bptr;
}


struct bptr *_bptr_create_subdir(struct bptr_temp *template, const char *subdir)
{
   char path[256];
   _bptr_path_subdir(path, sizeof(path), template->fnm, subdir);
   struct bptr *bptr = bptr_init(path, template->is_lite,
                                 template->node_sz, template->key_sz,
                                 template->val_sz, template->cache_cap,
                                 template->cmp);
   TEST_ASSERT_MESSAGE(bptr, "`_bptr_create': `bptr_init' returned NULL");
   return bptr;
}
