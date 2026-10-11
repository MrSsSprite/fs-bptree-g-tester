#ifndef TEMP_SPLIT_H
#define TEMP_SPLIT_H

/*----------------------------- Public Includes ------------------------------*/
#include <stddef.h>
/*--------------------------- Public Includes END ----------------------------*/


/*----------------------------- Public Functions -----------------------------*/
/**
 * @brief   Split a perfectly full template at every position it is probed at
 *
 * The case of the unit.  `bptr_node_split' is a white-box entry point -- the
 * public insert API is a stub that exits with `BPTR_E_TODO' -- so the case
 * drives the split directly: it walks the templates `templates_get' hands out
 * and, for every insertion position of one, takes a pristine copy of that
 * template, walks down to the leaf the inserted key belongs to and splits that
 * leaf.
 *
 * Every node of a perfectly full template is full, so the split of a leaf
 * cascades through every level up to the root and adds one level to the tree.
 * Each instance is flushed and closed, checked from its file by
 * `bin/temp_verify', and removed on success before the next copy is made.
 * Verification failure leaves the image for diagnosis and stops the case.
 * The flushed file checks defects in splitting, node persistence and headers.
 *
 * Heights 1--2 try every gap; height 3 keeps its original role combinations.
 * Taller trees use an independent compact projection sampler covering pairs
 * of branch roles crossed with leaf roles, plus both endpoints. It computes
 * 64-bit gaps from geometry without walking the existing leaf keys.
 *
 * An interval leaving no room between keys is skipped with a printed notice;
 * the guard case verifies every template either way. Invalid geometry or an
 * inserted key/value outside the representable range fails before copying.
 */
void test_full_split(void);
/* Free the current sample list after a case, including an assertion abort. */
void temp_split_cleanup(void);
/*--------------------------- Public Functions END ---------------------------*/

#endif
