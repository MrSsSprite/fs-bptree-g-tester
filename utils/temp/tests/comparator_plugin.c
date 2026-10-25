#include "temp_compare.h"
#include <string.h>

static int compare(const void *lhs, const void *rhs)
{
    int64_t a, b;
    memcpy(&a, lhs, sizeof a);
    memcpy(&b, rhs, sizeof b);
    return a < b ? -17 : a > b ? 31 : 0;
}

const struct temp_comparator_descriptor_v1 *temp_comparator_v1(void)
{
    static const struct temp_comparator_descriptor_v1 descriptor = {
        TEMP_COMPARATOR_ABI_V1,
        sizeof(struct temp_comparator_descriptor_v1),
        "test/id-only/v1", 16, 0, compare
    };
    return &descriptor;
}
