#define _POSIX_C_SOURCE 200809L
#include "harness.h"
#include "scenarios.h"
#include "failures.h"
#include "progress.h"
#include "unity.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

struct test_group { const char *name; void (*run)(void); };
static const struct test_group groups[] = {
    { "empty", test_insert_empty },
    { "permutations", test_insert_permutations },
    { "growth", test_insert_growth },
    { "comparators", test_insert_comparators },
    { "binary", test_insert_binary },
    { "equivalence", test_insert_equivalence },
    { "ownership", test_insert_ownership },
    { "fixtures", test_insert_fixtures },
    { "cache", test_insert_cache },
    { "cache-errors", test_insert_cache_errors },
    { "io-errors", test_insert_io_errors }
};
static const struct test_group *active_group;
static char active_name[96];

static void run_group(void)
{
    active_group->run();
    progress_done("    %s: PASS", active_name);
}

void setUp(void) { }
void tearDown(void)
{
    insert_scenarios_cleanup();
    insert_cleanup();
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s [--case NAME] [--layout normal|lite|both]\n"
                    "       [--seed UINT64] [--work-dir DIR]\n"
                    "       %s --list\n", program, program);
}

int main(int argc, char **argv)
{
    const char *selected = "all";
    int layout = -1;
    _Bool seen_case = 0, seen_seed = 0, seen_dir = 0, seen_layout = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0 && argc == 2) {
            for (size_t j = 0; j < sizeof groups / sizeof *groups; j++)
                puts(groups[j].name);
            return EXIT_SUCCESS;
        }
        if (i + 1 >= argc) { usage(argv[0]); return EXIT_FAILURE; }
        if (!strcmp(argv[i], "--case") && !seen_case) {
            selected = argv[++i];
            seen_case = 1;
        } else if (!strcmp(argv[i], "--work-dir") && !seen_dir) {
            insert_work_dir = argv[++i];
            seen_dir = 1;
        } else if (!strcmp(argv[i], "--layout") && !seen_layout) {
            const char *value = argv[++i];
            if (!strcmp(value, "normal")) layout = 0;
            else if (!strcmp(value, "lite")) layout = 1;
            else if (!strcmp(value, "both")) layout = -1;
            else { usage(argv[0]); return EXIT_FAILURE; }
            seen_layout = 1;
        } else if (!strcmp(argv[i], "--seed") && !seen_seed) {
            char *end;
            const char *value = argv[++i];
            errno = 0;
            unsigned long long seed = strtoull(value, &end, 0);
            if (errno || *value < '0' || *value > '9' || *end || seed > UINT64_MAX) {
                fprintf(stderr, "insert: invalid seed: %s\n", value);
                return EXIT_FAILURE;
            }
            insert_seed = (uint64_t)seed;
            seen_seed = 1;
        } else { usage(argv[0]); return EXIT_FAILURE; }
    }
    size_t selected_count = 0;
    for (size_t i = 0; i < sizeof groups / sizeof *groups; i++)
        if (!strcmp(selected, "all") || !strcmp(selected, groups[i].name))
            selected_count++;
    if (!selected_count || !*insert_work_dir) {
        fprintf(stderr, "insert: unknown case or empty work directory: %s\n", selected);
        return EXIT_FAILURE;
    }
    printf("Test Unit: insert (seed=%" PRIu64 ")\n", insert_seed);
    UNITY_BEGIN();
    for (size_t i = 0; i < sizeof groups / sizeof *groups; i++) {
        if (strcmp(selected, "all") && strcmp(selected, groups[i].name)) continue;
        for (int lite = 0; lite < 2; lite++) {
            if (layout != -1 && layout != lite) continue;
            insert_scenario_layout = lite;
            active_group = &groups[i];
            snprintf(active_name, sizeof active_name, "%s-%s", groups[i].name,
                     lite ? "lite" : "normal");
            UnityDefaultTestRun(run_group, active_name, __LINE__);
        }
    }
    int status = UNITY_END();
    insert_summary();
    return status ? EXIT_FAILURE : EXIT_SUCCESS;
}
