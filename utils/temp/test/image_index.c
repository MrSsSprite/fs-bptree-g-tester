#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Count actual path comparisons in the image API. Stop immediately if a
 * pairwise scan returns; wall-clock performance is deliberately not asserted. */
static size_t comparisons, budget;
static int counted_memcmp(const void *a, const void *b, size_t n)
{
    assert(++comparisons <= budget);
    return memcmp(a, b, n);
}
#define memcmp counted_memcmp
#include "../src/temp_image.c"
#undef memcmp

static int record(void *context, uint64_t i, void *key, void *value)
{
    (void)context;
    int64_t k = (int64_t)i, v = 17 * (int64_t)i - 31;
    memcpy(key, &k, 8); memcpy(value, &v, 8);
    return 0;
}
static int order(const void *a, const void *b, void *context)
{
    (void)context;
    int64_t left, right;
    memcpy(&left, a, 8); memcpy(&right, b, 8);
    return (left > right) - (left < right);
}
static void ok(int status, const struct temp_image_result *r)
{
    if (status) fprintf(stderr, "%s: %s\n", r->invariant, r->detail);
    assert(status == 0);
}
static void large_shape(size_t count, int generate)
{
    struct temp_occupancy *overrides = calloc(count + 1, sizeof *overrides);
    uint32_t (*paths)[6] = malloc((count + 1) * sizeof *paths);
    struct temp_occupancy *snapshot = malloc(count * sizeof *snapshot);
    assert(overrides && paths && snapshot);
    for (size_t i = 0; i < count; ++i) {
        size_t number = i * 8191u % count;
        for (size_t depth = 6; depth-- > 0;) { paths[i][depth] = number % 6; number /= 6; }
        overrides[i] = (struct temp_occupancy){paths[i], 6, 2};
    }
    memcpy(snapshot, overrides, count * sizeof *snapshot);
    struct temp_layout layout = {128, 8, 8, 1};
    struct temp_image_shape shape = {7, overrides, count};
    struct temp_image_stats stats;
    struct temp_image_result result;
    size_t levels = 0;
    for (size_t n = count; n; n /= 2) ++levels;
    comparisons = 0; budget = 4 * (count + 1) * levels;
    ok(temp_image_plan(&layout, &shape, &stats, &result), &result);
    assert(stats.nodes == 55987 && stats.leaves == 46656 && stats.records == 186624 - 2 * count);
    assert(!memcmp(snapshot, overrides, count * sizeof *snapshot));
    printf("Shape plan: %zu overrides, %zu comparisons\n", count, comparisons);
    if (generate) {
        char path[] = "/tmp/bptr-large-shape-XXXXXX";
        int fd = mkstemp(path);
        assert(fd >= 0 && close(fd) == 0 && unlink(path) == 0);
        struct temp_records records = {stats.records, NULL, record};
        struct temp_order comparator = {NULL, order};
        comparisons = 0; budget = 16 * (count + 1) * (levels + 7);
        ok(temp_image_generate(path, &layout, &shape, &records, &comparator, &result), &result);
        assert(result.actual.records == records.count && result.actual.nodes == stats.nodes);
        printf("Shape generation and exact verification: %zu overrides, %zu comparisons\n", count, comparisons);
        assert(unlink(path) == 0);
    }
    memcpy(paths[count], paths[0], sizeof paths[0]);
    overrides[count] = (struct temp_occupancy){paths[count], 6, 3};
    shape.override_count = count + 1;
    comparisons = 0; budget = 4 * (count + 1) * levels;
    assert(temp_image_plan(&layout, &shape, &stats, &result) == TEMP_IMAGE_SPEC);
    assert(strstr(result.detail, "duplicate override path"));
    shape.override_count = count;
    paths[0][5] = 6;
    comparisons = 0;
    assert(temp_image_plan(&layout, &shape, &stats, &result) == TEMP_IMAGE_SPEC);
    assert(strstr(result.detail, "nonexistent path"));
    free(snapshot); free(paths); free(overrides);
}
static void numeric_paths(void)
{
    uint32_t a[] = {256}, b[] = {255};
    struct temp_occupancy overrides[] = {{a, 1, 127}, {NULL, 0, 256}, {b, 1, 126}};
    struct temp_layout layout = {4096, 8, 8, 1};
    struct temp_image_shape shape = {2, overrides, 3};
    struct temp_image_stats stats;
    struct temp_image_result result;
    comparisons = 0; budget = 100000;
    ok(temp_image_plan(&layout, &shape, &stats, &result), &result);
    assert(stats.nodes == 258 && stats.leaves == 257 && stats.records == 64513);
    char path[] = "/tmp/bptr-numeric-path-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && close(fd) == 0 && unlink(path) == 0);
    struct temp_records records = {stats.records, NULL, record};
    struct temp_order comparator = {NULL, order};
    ok(temp_image_generate(path, &layout, &shape, &records, &comparator, &result), &result);
    assert(result.actual.records == records.count && unlink(path) == 0);
    struct temp_occupancy duplicate[] = {{NULL, 0, 2}, {NULL, 0, 3}};
    shape.overrides = duplicate; shape.override_count = 2;
    assert(temp_image_plan(&layout, &shape, &stats, &result) == TEMP_IMAGE_SPEC);
    assert(strstr(result.detail, "duplicate override path"));
}
int main(void)
{
    large_shape(10000, 0);
    large_shape(20000, 1);
    numeric_paths();
    return 0;
}
