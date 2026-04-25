#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
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

struct bptr *_bptr_create(struct bptr_temp *template)
{
   char path[256];
   _bptr_path(path, sizeof(path), template->fnm);
   struct bptr *bptr = bptr_init(path, template->is_lite,
                                 template->node_sz, template->key_sz,
                                 template->val_sz, template->cmp);
   TEST_ASSERT(bptr);
   return bptr;
}
