#include "temp_compare.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct temp_comparator_module module = {0};
    char error[512];
    int64_t a[2] = {42, 1}, b[2] = {42, 99}, c[2] = {43, 1};
    assert(argc == 2);
    assert(temp_comparator_open(&module, argv[1], 16, "test/id-only/v1",
                                error, sizeof error) == 0);
    assert(module.descriptor->compare(a, b) == 0);
    assert(module.descriptor->compare(a, c) == -17);
    assert(module.descriptor->compare(c, a) == 31);
    temp_comparator_close(&module);
    temp_comparator_close(&module);
    assert(temp_comparator_open(&module, argv[1], 8, NULL,
                                error, sizeof error) == -1);
    assert(strstr(error, "key size") != NULL && module.handle == NULL);
    assert(temp_comparator_open(&module, argv[1], 16, "wrong-id",
                                error, sizeof error) == -1);
    assert(strstr(error, "ID mismatch") != NULL && module.handle == NULL);
    assert(temp_comparator_open(&module, "/nonexistent/comparator", 16, NULL,
                                error, sizeof error) == -1);
    assert(module.handle == NULL);
    puts("comparator ABI and loader: ok");
    return 0;
}
