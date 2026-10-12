/* Include the implementation so arithmetic boundaries can be tested without
 * exposing a private helper in the public API or writing multi-terabyte files. */
#include "../src/temp_full.c"
#include <assert.h>

int main(void)
{
   /* Inclusive pointer maxima: a binary tree of N levels has 2^N - 1 nodes.
    * The helper must accept the maximum without ever evaluating maximum + 1. */
   assert(_gen_node_count_fits(32, 2, UINT32_MAX));
   assert(!_gen_node_count_fits(32, 2, (uint64_t)UINT32_MAX - 1u));
   assert(!_gen_node_count_fits(33, 2, UINT32_MAX));
   assert(_gen_node_count_fits(64, 2, UINT64_MAX));
   assert(!_gen_node_count_fits(64, 2, UINT64_MAX - 1u));
   assert(!_gen_node_count_fits(65, 2, UINT64_MAX));
   assert(!_gen_node_count_fits(2, UINT64_MAX, UINT64_MAX));
   assert(!_gen_node_count_fits(254, UINT64_C(357913936), UINT64_MAX));
   assert(_gen_node_count_fits(1, UINT64_MAX, 1));
   assert(!_gen_node_count_fits(1, 3, 0));

   /* Model a smaller file-offset cap so equality is reachable without writing
    * an enormous image: 39 tree blocks plus one header, each 512 bytes. The
    * required final position exactly at 20480 is valid, one byte less is not.
    * A later node would exceed this cap; it must not affect acceptance now. */
   assert(_gen_node_count_fits(2, 38, UINT64_C(20480) / 512u - 1u));
   assert(!_gen_node_count_fits(2, 38, UINT64_C(20479) / 512u - 1u));

   /* Check the recurrence against an independent small geometric sum, on both
    * sides of the exact boundary for varied shapes and actual fanouts. */
   for (uint64_t fanout = 3; fanout <= 40; fanout++)
    {
      uint64_t total = 0, width = 1;
      for (unsigned int levels = 1; levels <= 8; levels++)
       {
         total += width;
         assert(_gen_node_count_fits(levels, fanout, total));
         assert(!_gen_node_count_fits(levels, fanout, total - 1u));
         width *= fanout;
       }
    }

   /* Largest safe default-layout height for each active limit. No files are
    * created by these checks; interval 0 in the CLI tests isolates pointers. */
   assert(_gen_check_pointers(7, 1, 512) == TEMP_FULL_OK);
   assert(_gen_check_pointers(8, 1, 512) == TEMP_FULL_E_PTR_OVERFLOW);
   if (sizeof (bptr_off_t) * CHAR_BIT == 64u)
    {
      assert(_gen_check_pointers(12, 0, 512) == TEMP_FULL_OK);
      assert(_gen_check_pointers(13, 0, 512) == TEMP_FULL_E_PTR_OVERFLOW);
    }
   puts("pointer arithmetic boundaries: ok");
   return 0;
}
