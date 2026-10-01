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
 * template, walks down to the leaf the inserted key belongs to, splits that
 * leaf and checks the whole tree with `temp_full_verify'.
 *
 * Every node of a perfectly full template is full, so the split of a leaf
 * cascades through every level up to the root and adds one level to the tree;
 * the copy is then unloaded and loaded again from the file it was written to,
 * and checked a second time, so that a defect in the split, in the flush of the
 * nodes it touched, or in the header it rewrote cannot hide behind a cache hit.
 *
 * A template the sampler cannot serve -- more than 3 levels tall, or an
 * interval that leaves no room for a key between two keys of the image -- is
 * skipped with a notice.  The skip is always printed, so a template the unit
 * cannot split is never dropped silently, and the guard case verifies every
 * template either way.
 *
 * The positions come from `split_gap_at': every one of them for the 1- and
 * 2-level template, and a sample for the 3-level one, which has too many to try
 * them all.
 */
void test_full_split(void);
/*--------------------------- Public Functions END ---------------------------*/

#endif
