/* Exercise sparse arithmetic without allocating the corresponding images. */
#include "../src/temp_full.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
   struct temp_shape shape = {0};
   struct temp_shape_counts counts;

   /* At 128 bytes lite nodes have four leaf records and six branch children.
    * A reduced first branch must not reduce its five full sibling subtrees. */
   assert(temp_shape_add(&shape, "0=2") == 0);
   assert(temp_shape_count(&shape, 3, 1, 128, &counts) == TEMP_FULL_OK);
   assert(counts.leaves == 3 + 5 * 6);
   assert(counts.records == 4 * counts.leaves);
   assert(counts.nodes == 1 + 6 + counts.leaves);
   temp_shape_free(&shape);

   /* A fully occupied 42-level ternary tree overflows uint64_t node totals.
    * Reducing all branches in its first three levels makes the requested
    * shape representable: overflowing unused full ancestors must not cause
    * rejection. The untouched full subtrees are still too large to enumerate. */
   assert(temp_shape_count(&shape, 42, 1, 92, &counts) != TEMP_FULL_OK);
   const char *overrides[] = {
      "root=1", "0=1", "1=1", "0.0=1", "0.1=1", "1.0=1", "1.1=1"
   };
   for (size_t i = 0; i < sizeof overrides / sizeof overrides[0]; i++)
      assert(temp_shape_add(&shape, overrides[i]) == 0);
   uint64_t subtree_nodes = 0, subtree_leaves = 1;
   for (unsigned int level = 0; level < 39; level++)
    {
      subtree_nodes += subtree_leaves;
      if (level != 38) subtree_leaves *= 3;
    }
   assert(temp_shape_count(&shape, 42, 1, 92, &counts) == TEMP_FULL_OK);
   assert(counts.nodes == 7 + 8 * subtree_nodes);
   assert(counts.leaves == 8 * subtree_leaves);
   assert(counts.records == counts.leaves);
   temp_shape_free(&shape);

   /* Descendants under an explicitly removed child are invalid even when
    * their requested occupancy would otherwise be legal. */
   assert(temp_shape_add(&shape, "1.2=1") == 0);
   assert(temp_shape_add(&shape, "1=1") == 0);
   assert(temp_shape_count(&shape, 3, 1, 92, &counts) == TEMP_FULL_E_SHAPE);
   temp_shape_free(&shape);
   puts("sparse shape arithmetic boundaries: ok");
   return 0;
}
