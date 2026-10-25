#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Count real parser comparisons, with an early budget failure so a quadratic
 * regression fails quickly. There are no wall-clock or platform-speed limits. */
static size_t comparisons, budget;
static int counted_strcmp(const char *a, const char *b)
{
    assert(++comparisons <= budget);
    return strcmp(a, b);
}
#define strcmp counted_strcmp
#include "../src/temp_json.c"
#undef strcmp

static void check_object(size_t count, int duplicate)
{
    char path[] = "/tmp/bptr-json-index-XXXXXX", error[256];
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs("{", file);
    for (size_t i = 0; i < count; ++i) {
        /* 8191 is coprime with the case sizes; this visits each name once. */
        size_t name = i * 8191u % count;
        fprintf(file, "%s\"member%08zu\":%zu", i ? "," : "", name, i);
    }
    if (duplicate) fputs(",\"\\u006dember00000000\":99", file);
    fputs("}", file);
    assert(fclose(file) == 0);
    size_t levels = 0;
    for (size_t n = count; n; n /= 2) ++levels;
    comparisons = 0; budget = 4 * (count + 1) * levels;
    struct temp_json *json = temp_json_read(path, error, sizeof error);
    if (duplicate) {
        assert(!json && strstr(error, "duplicate object key"));
    } else {
        assert(json && json->type == 'o');
        const struct temp_json *child = json->child;
        for (size_t i = 0; i < count; ++i) {
            char expected[32];
            snprintf(expected, sizeof expected, "member%08zu", i * 8191u % count);
            assert(child && !strcmp(child->name, expected));
            child = child->next;
        }
        assert(!child);
    }
    printf("JSON object: %zu members, %zu comparisons%s\n", count, comparisons, duplicate ? ", duplicate rejected" : "");
    temp_json_free(json);
    assert(unlink(path) == 0);
}
int main(void)
{
    check_object(10000, 0);
    check_object(20000, 0);
    check_object(20000, 1);
    return 0;
}
