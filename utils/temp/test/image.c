#define _POSIX_C_SOURCE 200809L
#include "temp_image.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int get(void *ctx, uint64_t i, void *key, void *value)
{
    int direction = *(int *)ctx;
    int64_t k = direction * (int64_t)i;
    unsigned char v[3] = {(unsigned char)i, 0, 255};
    memcpy(key, &k, 8); memcpy(value, v, 3);
    return 0;
}
static int compare(const void *lhs, const void *rhs, void *ctx)
{
    int64_t a, b;
    memcpy(&a, lhs, 8); memcpy(&b, rhs, 8);
    return *(int *)ctx * ((a > b) - (a < b));
}
static void check(int status, struct temp_image_result *r)
{
    if (status) fprintf(stderr, "%s: %s node=%llu record=%llu\n", r->invariant,
                        r->detail, (unsigned long long)r->node, (unsigned long long)r->record);
    assert(status == 0);
}
int main(void)
{
    char path[] = "/tmp/bptr-image-XXXXXX";
    struct temp_image_result r;
    int temporary = mkstemp(path);
    assert(temporary >= 0 && close(temporary) == 0 && unlink(path) == 0);
    for (int lite = 0; lite <= 1; ++lite) for (int direction = -1; direction <= 1; direction += 2) {
        struct temp_layout l = {128, 8, 3, lite};
        uint32_t child[] = {0};
        struct temp_occupancy overrides[] = {{NULL, 0, 1}, {child, 1, lite ? 3 : 2}};
        struct temp_image_shape shape = {3, overrides, 2};
        struct temp_image_stats stats;
        struct temp_order order = {&direction, compare};
        check(temp_image_plan(&l, &shape, &stats, &r), &r);
        struct temp_records records = {stats.records, &direction, get};
        check(temp_image_generate(path, &l, &shape, &records, &order, &r), &r);
        assert(!r.reused);
        check(temp_image_generate(path, &l, &shape, &records, &order, &r), &r);
        assert(r.reused);
        check(temp_image_verify(path, &l, &records, &order, NULL, NULL, &r), &r);
        assert(r.actual.records == stats.records && r.actual.nodes == stats.nodes);
        struct temp_image_constraints wrong = {TEMP_EXPECT_HEIGHT, 4, 0, 0};
        assert(temp_image_verify(path, &l, &records, &order, NULL, &wrong, &r) == TEMP_IMAGE_INVALID);
        int fd = open(path, O_RDWR);
        assert(fd >= 0);
        uint32_t enormous = UINT32_MAX;
        assert(pwrite(fd, &enormous, 4, 128 + 4) == 4);
        assert(close(fd) == 0);
        assert(temp_image_verify(path, &l, &records, &order, NULL, NULL, &r) == TEMP_IMAGE_INVALID);
        assert(strcmp(r.invariant, "occupancy") == 0);
        assert(unlink(path) == 0);
        shape.height = 0; shape.override_count = 0; records.count = 0;
        check(temp_image_generate(path, &l, &shape, &records, &order, &r), &r);
        assert(r.actual.records == 0 && r.actual.nodes == 0 && r.actual.height == 0);
        assert(unlink(path) == 0);
    }
    puts("independent image API: ok");
    return 0;
}
