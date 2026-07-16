#ifndef TEST_BPTR_BRCH_SP_H
#define TEST_BPTR_BRCH_SP_H

/*----------------------------- Public Includes ------------------------------*/
#include "test_bptr_temp.h"
/*--------------------------- Public Includes END ----------------------------*/


/*----------------------- Public Function Declarations -----------------------*/
void _bptr_full_brch_create(struct bptr_temp *temp);
void _bptr_full_brch_verify(struct bptr_temp *temp);
void _bptr_full_brch_casc_create(struct bptr_temp *temp);
void _bptr_full_brch_casc_verify(struct bptr_temp *temp);
void _bptr_part_full_brch_casc_create(struct bptr_temp *temp);
void _bptr_part_full_brch_casc_verify(struct bptr_temp *temp);
int temp_instantiate
 (const struct bptr_temp *temp, const char *prefix,
  char *dst_path, size_t dst_path_sz);
/*--------------------- Public Function Declarations END ---------------------*/

#endif
