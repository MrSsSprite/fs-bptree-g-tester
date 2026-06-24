#ifndef TEST_BPTR_SETUP_H
#define TEST_BPTR_SETUP_H

/*----------------------------- Public Includes ------------------------------*/
#include <stddef.h>
#include "test_bptr_temp.h"
/*--------------------------- Public Includes END ----------------------------*/

/*----------------------- Public Function Declarations -----------------------*/
void _bptr_path(char *dest, size_t sz, const char *filename);
void _bptr_path_subdir
 (char *dest, size_t sz, const char *filename, const char *subdir);
struct bptr *_bptr_create(struct bptr_temp *template);
struct bptr *_bptr_create_subdir
 (struct bptr_temp *template, const char *subdir);
/*--------------------- Public Function Declarations END ---------------------*/

#endif
