#define _POSIX_C_SOURCE 200809L
#include "bptree.h"
#include "temp_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int compare(const void *lhs, const void *rhs)
{
    int64_t a, b;
    memcpy(&a, lhs, 8); memcpy(&b, rhs, 8);
    return (a > b) - (a < b);
}
static int order(const void *lhs, const void *rhs, void *context)
{ (void)context; return compare(lhs, rhs); }
static int no_records(void *context, uint64_t i, void *key, void *value)
{ (void)context; (void)i; (void)key; (void)value; return -1; }
int main(void)
{
    char path[] = "/tmp/bptr-empty-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && close(fd) == 0 && unlink(path) == 0);
    for (int lite = 0; lite <= 1; ++lite) {
        struct bptr *tree = bptr_init(path, lite, 128, 8, 8, 2, compare);
        assert(tree && bptr_unload(tree) == BPTR_E_SUCCESS);
        struct temp_layout layout = {128, 8, 8, lite};
        struct temp_order comparator = {NULL, order};
        struct temp_records records = {0, NULL, no_records};
        struct temp_image_shape shape = {0};
        struct temp_image_result result;
        int status = temp_image_verify(path, &layout, &records, &comparator, &shape, NULL, &result);
        if (status) fprintf(stderr, "initialized empty image: %s: %s\n", result.invariant, result.detail);
        assert(status == 0 && unlink(path) == 0);
    }
    puts("initialized empty image interoperability: ok");
    return 0;
}
