#ifndef BPTR_STATIC_H
#define BPTR_STATIC_H

/*----------------------------- Public Includes ------------------------------*/
#include "bptr_internal.h"
#include "bptr_node.h"
/*--------------------------- Public Includes END ----------------------------*/


/*--------------------- Testable Static Function Imports ---------------------*/
/**
 * `core/src/bptr_node.c' declares and defines `bptr_node_split' with
 * `BPTR_STATIC', which `bptr_internal.h' expands to `static' unless the library
 * is compiled with `BPTR_TESTING'.  `config.mk' passes the macro for every unit,
 * so the function keeps external linkage and the test unit can call it through
 * this declaration -- the test-side half of that switch.
 *
 * @pre   @p node must be full, and @p key must not already exist in it;
 *        otherwise the call reports `BPTR_E_FN_INPUT' and returns 0.
 * @return  the node index of the new sibling on success, 0 on failure.
 */
extern bptr_node_t bptr_node_split(struct bptr *self, struct bptr_node *node,
                                   const void *key, const void *val);
/*------------------- Testable Static Function Imports END -------------------*/

#endif
