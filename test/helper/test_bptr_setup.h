#ifndef TEST_BPTR_SETUP_H
#define TEST_BPTR_SETUP_H

/*----------------------------- Public Includes ------------------------------*/
#include <stddef.h>
#include "test_bptr_temp.h"
/*--------------------------- Public Includes END ----------------------------*/

/*----------------------- Public Function Declarations -----------------------*/
void _bptr_path(char *dest, size_t sz, const char *filename);
struct bptr *_bptr_create(struct bptr_temp *template);
/*--------------------- Public Function Declarations END ---------------------*/

#endif
