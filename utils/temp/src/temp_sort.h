#ifndef TEMP_SORT_H
#define TEMP_SORT_H

#include <stddef.h>
#include <string.h>

/* Internal stable merge sort of a borrowed-pointer index. scratch has count
 * entries, disjoint from items. Referenced objects are never modified. Unlike
 * implementation-defined qsort strategies, comparison work is O(n log n). */
static void temp_sort_index(const void **items, const void **scratch, size_t count,
                            int (*compare)(const void *, const void *))
{
    if (count < 2) return;
    size_t middle = count / 2;
    temp_sort_index(items, scratch, middle, compare);
    temp_sort_index(items + middle, scratch + middle, count - middle, compare);
    size_t left = 0, right = middle, out = 0;
    while (left < middle && right < count)
        scratch[out++] = compare(items[left], items[right]) <= 0 ? items[left++] : items[right++];
    while (left < middle) scratch[out++] = items[left++];
    while (right < count) scratch[out++] = items[right++];
    memcpy(items, scratch, count * sizeof *items);
}
#endif
