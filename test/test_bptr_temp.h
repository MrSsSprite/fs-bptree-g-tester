#ifndef TEST_BPTR_TEMP_H
#define TEST_BPTR_TEMP_H

/*----------------------------- Public Includes ------------------------------*/
#include <stdint.h>
#include <stddef.h>
/*--------------------------- Public Includes END ----------------------------*/

/*------------------------------ Public Structs ------------------------------*/
struct bptr_temp
{
   const char *fnm;
   _Bool is_lite;
   uint32_t node_sz;
   uint16_t key_sz, val_sz;
   int (*cmp)(const void *, const void *);
};
/*---------------------------- Public Structs END ----------------------------*/

/*----------------------- Public Variable Declarations -----------------------*/
extern struct bptr_temp lite_temps[];
extern size_t lite_temps_sz;
extern struct bptr_temp norm_temps[];
extern size_t norm_temps_sz;
/*--------------------- Public Variable Declarations END ---------------------*/

#endif
