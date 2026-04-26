#ifndef BPTR_STATIC_H
#define BPTR_STATIC_H


/*----------------------------- Public Includes ------------------------------*/
#include "bptr_internal.h"
#include "bptr_node.h"
/*--------------------------- Public Includes END ----------------------------*/


/*--------------------- Testable Static Function Imports ---------------------*/
extern bptr_node_t bptr_node_split(struct bptr *self, struct bptr_node *node,
                                   const void *key, const void *val);
/*------------------- Testable Static Function Imports END -------------------*/


#endif
