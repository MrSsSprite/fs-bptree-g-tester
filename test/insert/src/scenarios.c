#include "scenarios.h"
#include "harness.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int insert_scenario_layout = -1;

/* These comparators only inspect their advertised key bytes.  In particular,
 * memcpy avoids giving the test's numerical comparisons alignment requirements
 * that the byte-buffer insertion interface does not have. */
static int compare_i64(const void *lhs, const void *rhs)
{
    int64_t a, b;
    memcpy(&a, lhs, sizeof a);
    memcpy(&b, rhs, sizeof b);
    return a < b ? -17 : a > b ? 31 : 0;
}

static int compare_i64_desc(const void *lhs, const void *rhs)
{
    return -compare_i64(lhs, rhs);
}

static int byte_order(const void *lhs, const void *rhs, size_t width)
{
    int result = memcmp(lhs, rhs, width);
    return result < 0 ? -17 : result > 0 ? 31 : 0;
}

#define BYTE_COMPARATOR(width) \
    static int compare_bytes_##width(const void *a, const void *b) \
    { return byte_order(a, b, width); } \
    static int compare_bytes_##width##_desc(const void *a, const void *b) \
    { return -byte_order(a, b, width); }

BYTE_COMPARATOR(1)
BYTE_COMPARATOR(3)
BYTE_COMPARATOR(7)
BYTE_COMPARATOR(9)

static int compare_prefix(const void *a, const void *b)
{
    return byte_order(a, b, 2);
}

static int compare_prefix_desc(const void *a, const void *b)
{
    return -compare_prefix(a, b);
}

static int compare_equal(const void *a, const void *b)
{
    (void)a;
    (void)b;
    return 0;
}

static struct insert_config numeric_config(_Bool lite, uint32_t node_size,
                                           uint16_t value_size, _Bool desc,
                                           uint64_t cache)
{
    struct insert_config config = {
        {node_size, 8, value_size, lite}, cache,
        desc ? compare_i64_desc : compare_i64,
        desc ? "i64-desc" : "i64-asc", 0
    };
    return config;
}

static uint64_t random_next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static void shuffled(size_t *order, size_t count, uint64_t seed)
{
    uint64_t state = seed ? seed : UINT64_C(0x97ed4931a85cb62f);
    for (size_t i = 0; i < count; i++) order[i] = i;
    for (size_t i = count; i > 1; i--) {
        size_t j = (size_t)(random_next(&state) % i);
        size_t saved = order[i - 1];
        order[i - 1] = order[j];
        order[j] = saved;
    }
}

static void value_bytes(unsigned char *value, size_t width, uint64_t id)
{
    uint64_t state = id ^ UINT64_C(0xac879d32e4b1f063);
    if (id < 2) {
        memset(value, id ? 0xff : 0, width);
        return;
    }
    for (size_t i = 0; i < width; i++)
        value[i] = i % 3 == 1 ? 0 : (unsigned char)random_next(&state);
}

static void binary_key(unsigned char *key, size_t width, size_t id)
{
    if (id < 2) {
        memset(key, id ? 0xff : 0, width);
        return;
    }
    memset(key, 0x80, width);
    key[width - 1] = (unsigned char)id;
    if (width > 1) key[width - 2] = (unsigned char)(id >> 8);
}

static _Bool next_permutation(size_t *order, size_t count)
{
    size_t suffix = count - 1, swap, lo, hi;
    while (suffix && order[suffix - 1] >= order[suffix]) suffix--;
    if (!suffix) return 0;
    swap = count - 1;
    while (order[swap] <= order[suffix - 1]) swap--;
    size_t saved = order[suffix - 1];
    order[suffix - 1] = order[swap];
    order[swap] = saved;
    lo = suffix;
    hi = count - 1;
    while (lo < hi) {
        saved = order[lo];
        order[lo++] = order[hi];
        order[hi--] = saved;
    }
    return 1;
}

void test_insert_permutations(void)
{
    static const int64_t keys[] = {-2, 0, 5, 17};
    for (unsigned lite = 0; lite < 2; lite++) {
        if (insert_scenario_layout >= 0 && lite != (unsigned)insert_scenario_layout) continue;
        size_t order[] = {0, 1, 2, 3}, trial = 0;
        do {
            char name[80];
            unsigned char value[8];
            struct insert_config config = numeric_config(lite, 128, 8, 0, 4096);
            snprintf(name, sizeof name, "permutation-%u-%zu", lite, trial++);
            insert_begin(name, &config);
            insert_create();
            for (size_t i = 0; i < 4; i++) {
                value_bytes(value, sizeof value, order[i] + 10);
                insert_apply(&keys[order[i]], value);
            }
            /* All four records fill this root leaf. Rejection must preserve
             * their values even when a fresh insertion would need growth. */
            for (size_t i = 4; i-- > 0;) {
                memset(value, 0xdd, sizeof value);
                insert_apply(&keys[i], value);
            }
            insert_checkpoint(1);
            int64_t fresh = 99;
            value_bytes(value, sizeof value, 99);
            insert_apply(&fresh, value);
            for (size_t i = 0; i < 4; i++) insert_apply(&keys[i], value);
            insert_finish();
        } while (next_permutation(order, 4));
        insert_require(trial == 24, "permutation harness executed %zu, not 24 trials", trial);
    }
}

void test_insert_growth(void)
{
    enum { COUNT = 160 };
    for (unsigned lite = 0; lite < 2; lite++) {
        if (insert_scenario_layout >= 0 && lite != (unsigned)insert_scenario_layout) continue;
        for (unsigned mode = 0; mode < 4; mode++) {
            char name[80];
            size_t order[COUNT];
            unsigned char value[8];
            struct insert_config config = numeric_config(lite, 128, 8, 0, 4096);
            snprintf(name, sizeof name, "growth-%u-%u", lite, mode);
            if (mode == 3) shuffled(order, COUNT, insert_seed ^ (lite * 37u + mode));
            else for (size_t i = 0; i < COUNT; i++)
                order[i] = mode == 0 ? i : mode == 1 ? COUNT - 1 - i :
                           (i % 2 ? COUNT - 1 - i / 2 : i / 2);
            insert_begin(name, &config);
            insert_create();
            for (size_t i = 0; i < COUNT; i++) {
                int64_t key = (int64_t)order[i] * 4 - 320;
                value_bytes(value, sizeof value, order[i] + 100);
                insert_apply(&key, value);
                if (i % 13 == 0) {
                    memset(value, 0x6d, sizeof value);
                    insert_apply(&key, value);
                }
                if (i == 47 || i == 111) insert_checkpoint(1);
            }
            /* Retry every preexisting key after multiple root-growth events. */
            for (size_t i = 0; i < COUNT; i++) {
                int64_t key = (int64_t)i * 4 - 320;
                value_bytes(value, sizeof value, i + 9000);
                insert_apply(&key, value);
            }
            insert_finish();
        }
        /* These configurations each hold exactly one leaf record, while
         * retaining two branch-key slots in both pointer layouts. */
        struct insert_config tiny = numeric_config(lite, lite ? 92 : 104,
                                                   lite ? 8 : 16, 0, 4096);
        char name[80];
        unsigned char value[16];
        snprintf(name, sizeof name, "one-record-leaves-%u", lite);
        insert_begin(name, &tiny);
        insert_create();
        for (size_t i = 0; i < 40; i++) {
            int64_t key = (int64_t)(i % 2 ? 39 - i / 2 : i / 2) * 16;
            value_bytes(value, tiny.layout.value_size, i + 40);
            insert_apply(&key, value);
        }
        insert_checkpoint(1);
        insert_finish();
    }
}

void test_insert_comparators(void)
{
    static const int64_t keys[] = {
        INT64_MIN, INT64_MAX, 0, -1, 1, INT64_MIN + 1, INT64_MAX - 1,
        -256, 256, INT32_MIN, INT32_MAX, 255, -255
    };
    for (unsigned lite = 0; lite < 2; lite++) {
        if (insert_scenario_layout >= 0 && lite != (unsigned)insert_scenario_layout) continue;
        for (unsigned desc = 0; desc < 2; desc++) {
            char name[80];
            unsigned char value[8];
            struct insert_config config = numeric_config(lite, 128, 8, desc, 4096);
            snprintf(name, sizeof name, "numeric-extremes-%u-%u", lite, desc);
            insert_begin(name, &config);
            insert_create();
            for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
                value_bytes(value, sizeof value, i + 180);
                insert_apply(&keys[i], value);
            }
            insert_checkpoint(1);
            for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
                memset(value, 0x77, sizeof value);
                insert_apply(&keys[i], value);
            }
            insert_finish();
        }
    }
}

void test_insert_binary(void)
{
    static const struct {
        uint16_t key_size, value_size;
        int (*ascending)(const void *, const void *);
        int (*descending)(const void *, const void *);
    } widths[] = {
        {1, 1, compare_bytes_1, compare_bytes_1_desc},
        {3, 7, compare_bytes_3, compare_bytes_3_desc},
        {7, 3, compare_bytes_7, compare_bytes_7_desc},
        {9, 1, compare_bytes_9, compare_bytes_9_desc}
    };
    for (unsigned lite = 0; lite < 2; lite++) {
        if (insert_scenario_layout >= 0 && lite != (unsigned)insert_scenario_layout) continue;
        for (size_t w = 0; w < sizeof widths / sizeof *widths; w++) {
            for (unsigned desc = 0; desc < 2; desc++) {
                char name[80];
                size_t order[64];
                unsigned char key[9], value[7];
                struct insert_config config = {
                    {w == 0 ? 96 : 128, widths[w].key_size,
                     widths[w].value_size, lite}, 4096,
                    desc ? widths[w].descending : widths[w].ascending,
                    desc ? "bytes-desc" : "bytes-nonunit", 0
                };
                snprintf(name, sizeof name, "binary-%u-%u-%u-%u", lite,
                         config.layout.key_size, config.layout.value_size, desc);
                shuffled(order, 64, insert_seed ^ (w * 17 + lite * 23 + desc + 1));
                insert_begin(name, &config);
                insert_create();
                for (size_t i = 0; i < 64; i++) {
                    binary_key(key, config.layout.key_size, order[i]);
                    value_bytes(value, config.layout.value_size, order[i]);
                    insert_apply(key, value);
                }
                insert_checkpoint(1);
                for (size_t i = 0; i < 64; i++) {
                    binary_key(key, config.layout.key_size, i);
                    memset(value, 0x3f, config.layout.value_size);
                    insert_apply(key, value);
                }
                insert_finish();
            }
        }
    }
}

void test_insert_equivalence(void)
{
    for (unsigned lite = 0; lite < 2; lite++) {
        if (insert_scenario_layout >= 0 && lite != (unsigned)insert_scenario_layout) continue;
        for (unsigned desc = 0; desc < 2; desc++) {
            char name[80];
            unsigned char key[3], value[7];
            struct insert_config config = {
                {128, 3, 7, lite}, 4096,
                desc ? compare_prefix_desc : compare_prefix,
                desc ? "prefix-desc" : "prefix-asc", 2
            };
            snprintf(name, sizeof name, "prefix-equivalence-%u-%u", lite, desc);
            insert_begin(name, &config);
            insert_create();
            for (size_t i = 0; i < 64; i++) {
                size_t rank = i % 2 ? 63 - i / 2 : i / 2;
                key[0] = (unsigned char)(rank >> 8);
                key[1] = (unsigned char)rank;
                key[2] = 0x12;
                value_bytes(value, sizeof value, rank + 200);
                insert_apply(key, value);
                key[2] = 0xf3;
                memset(value, 0xee, sizeof value);
                insert_apply(key, value);
                if (i == 15 || i == 47) insert_checkpoint(1);
            }
            insert_checkpoint(1);
            for (size_t i = 0; i < 72; i++) {
                key[0] = (unsigned char)(i >> 8);
                key[1] = (unsigned char)i;
                key[2] = 0x86;
                value_bytes(value, sizeof value, i + 2000);
                insert_apply(key, value);
            }
            insert_finish();
        }
        char name[80];
        unsigned char key[3], value[7];
        struct insert_config equal = {
            {128, 3, 7, lite}, 4096, compare_equal, "all-equal", 0
        };
        snprintf(name, sizeof name, "all-equivalent-%u", lite);
        insert_begin(name, &equal);
        insert_create();
        for (size_t i = 0; i < 80; i++) {
            binary_key(key, sizeof key, i);
            value_bytes(value, sizeof value, i);
            insert_apply(key, value);
            if (i == 39) insert_checkpoint(1);
        }
        insert_finish();
    }
}

static unsigned char *owned_key, *owned_value;

void insert_scenarios_cleanup(void)
{
    free(owned_key);
    free(owned_value);
    owned_key = owned_value = NULL;
}

void test_insert_ownership(void)
{
    for (unsigned lite = 0; lite < 2; lite++) {
        if (insert_scenario_layout >= 0 && lite != (unsigned)insert_scenario_layout) continue;
        char name[80];
        struct insert_config config = {
            {128, 3, 7, lite}, 4096, compare_bytes_3, "bytes-nonunit", 0
        };
        snprintf(name, sizeof name, "buffer-lifetime-%u", lite);
        insert_begin(name, &config);
        insert_create();
        for (size_t i = 0; i < 48; i++) {
            unsigned char key_guard[5], value_guard[9], original_key[5], original_value[9];
            memset(key_guard, 0xa9, sizeof key_guard);
            memset(value_guard, 0xd3, sizeof value_guard);
            binary_key(key_guard + 1, 3, i);
            value_bytes(value_guard + 1, 7, i + 430);
            memcpy(original_key, key_guard, sizeof key_guard);
            memcpy(original_value, value_guard, sizeof value_guard);
            insert_apply(key_guard + 1, value_guard + 1);
            insert_require(memcmp(key_guard, original_key, sizeof key_guard) == 0 &&
                           memcmp(value_guard, original_value, sizeof value_guard) == 0,
                           "operation %zu changed an input buffer or its canaries", i);
            memset(key_guard + 1, 0x5a, 3);
            insert_check_live();
            memset(value_guard + 1, 0xc5, 7);
            insert_check_live();
        }
        /* Exact-sized allocations expose overreads under ASan; both buffers
         * are independently overwritten and released before later reads. */
        for (size_t i = 48; i < 80; i++) {
            owned_key = malloc(3);
            owned_value = malloc(7);
            insert_require(owned_key != NULL && owned_value != NULL,
                           "ownership scenario could not allocate source buffers");
            binary_key(owned_key, 3, i);
            value_bytes(owned_value, 7, i + 430);
            insert_apply(owned_key, owned_value);
            memset(owned_key, 0x31, 3);
            free(owned_key);
            owned_key = NULL;
            insert_check_live();
            memset(owned_value, 0xec, 7);
            free(owned_value);
            owned_value = NULL;
            insert_check_live();
            owned_key = malloc(3);
            owned_value = malloc(7);
            insert_require(owned_key != NULL && owned_value != NULL,
                           "ownership scenario could not churn source allocations");
            memset(owned_key, 0x72, 3);
            memset(owned_value, 0x8d, 7);
            insert_check_live();
            insert_scenarios_cleanup();
        }
        insert_checkpoint(1);
        insert_finish();

        snprintf(name, sizeof name, "overlapping-buffers-%u", lite);
        insert_begin(name, &config);
        insert_create();
        for (unsigned variant = 0; variant < 2; variant++) {
            unsigned char shared[12], original[12];
            memset(shared, 0x4c, sizeof shared);
            shared[1] = (unsigned char)variant;
            shared[2] = 0x80;
            shared[3] = 0x34;
            memcpy(original, shared, sizeof shared);
            insert_apply(shared + 1, shared + (variant ? 3 : 1));
            insert_require(memcmp(shared, original, sizeof shared) == 0,
                           "overlapping source buffers were modified");
            memset(shared, 0xef, sizeof shared);
            insert_check_live();
        }
        insert_finish();
    }
}

static void fixture_trials(const char *tag, const struct insert_config *config,
                           const struct temp_image_shape *shape, _Bool exhaustive)
{
    struct temp_image_stats stats;
    struct temp_image_result result;
    size_t gaps[10], gap_count = 0;
    int status = temp_image_plan(&config->layout, shape, &stats, &result);
    insert_require(status == TEMP_IMAGE_OK, "%s: fixture planning failed: %s: %s",
                   tag, result.invariant, result.detail);
    insert_require(stats.records < 10000, "%s: unreasonable fixture record total", tag);
    if (!exhaustive) {
        size_t count = (size_t)stats.records;
        size_t leaf = (config->layout.node_size - 64) /
                      (config->layout.key_size + config->layout.value_size);
        size_t candidates[] = {0, 1, leaf - 1, leaf, leaf + 1,
                               count / 2, count / 2 + 1, count - 1, count};
        for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
            _Bool seen = 0;
            if (candidates[i] > count) continue;
            for (size_t j = 0; j < gap_count; j++) seen |= gaps[j] == candidates[i];
            if (!seen) gaps[gap_count++] = candidates[i];
        }
    }
    size_t trials = exhaustive ? (size_t)stats.records + 1 : gap_count;
    for (size_t trial = 0; trial < trials; trial++) {
        size_t gap = exhaustive ? trial : gaps[trial];
        char name[120];
        unsigned char value[16];
        snprintf(name, sizeof name, "fixture-%s-%u-gap-%zu", tag,
                 config->layout.is_lite, gap);
        insert_begin(name, config);
        for (size_t i = 0; i < stats.records; i++) {
            int64_t key = -512 + (int64_t)i * 16;
            value_bytes(value, config->layout.value_size, i + 700);
            insert_seed_record(&key, value);
        }
        insert_generate(shape);
        int64_t neighbor = -512 + (int64_t)(gap < stats.records ? gap : gap - 1) * 16;
        memset(value, 0x9b, config->layout.value_size);
        insert_apply(&neighbor, value);
        int64_t key = -520 + (int64_t)gap * 16;
        value_bytes(value, config->layout.value_size, gap + 5700);
        insert_apply(&key, value);
        memset(value, 0x41, config->layout.value_size);
        insert_apply(&key, value);
        insert_checkpoint(1);
        key++;
        value_bytes(value, config->layout.value_size, gap + 6700);
        insert_apply(&key, value);
        insert_finish();
    }
    insert_require(trials > 0, "%s: fixture scenario executed no trials", tag);
}

void test_insert_fixtures(void)
{
    for (unsigned lite = 0; lite < 2; lite++) {
        if (insert_scenario_layout >= 0 && lite != (unsigned)insert_scenario_layout) continue;
        struct insert_config config = numeric_config(lite, 128, 8, 0, 4096);
        uint32_t pointer_size = lite ? 4 : 8;
        uint32_t branch_capacity = (128 - 64 - pointer_size) / (8 + pointer_size);
        struct temp_image_shape shape = {1, NULL, 0};
        fixture_trials("full-h1", &config, &shape, 1);
        shape.height = 2;
        fixture_trials("full-h2", &config, &shape, 1);
        shape.height = 3;
        fixture_trials("full-h3", &config, &shape, 0);
        shape.height = 4;
        fixture_trials("full-h4", &config, &shape, 0);

        struct temp_occupancy root_room = {NULL, 0, branch_capacity - 1};
        shape = (struct temp_image_shape){2, &root_room, 1};
        fixture_trials("parent-room", &config, &shape, 1);
        shape.height = 3;
        fixture_trials("higher-ancestor-room", &config, &shape, 0);

        uint32_t first[] = {0};
        struct temp_occupancy below_room = {first, 1, branch_capacity - 1};
        shape = (struct temp_image_shape){3, &below_room, 1};
        fixture_trials("lower-ancestor-room", &config, &shape, 0);

        uint32_t second[] = {1}, leaf[] = {0, 1};
        struct temp_occupancy uneven[] = {
            {NULL, 0, 2}, {first, 1, branch_capacity / 2},
            {second, 1, branch_capacity / 2 + 1}, {leaf, 2, 2}
        };
        shape = (struct temp_image_shape){3, uneven, sizeof uneven / sizeof *uneven};
        fixture_trials("asymmetric", &config, &shape, 0);

        config = numeric_config(lite, lite ? 92 : 104, lite ? 8 : 16, 0, 4096);
        shape = (struct temp_image_shape){5, NULL, 0};
        fixture_trials("one-record-h5", &config, &shape, 0);
    }
}

void test_insert_cache(void)
{
    for (unsigned lite = 0; lite < 2; lite++) {
        if (insert_scenario_layout >= 0 && lite != (unsigned)insert_scenario_layout) continue;
        char name[80];
        unsigned char value[8];
        struct insert_config config = numeric_config(lite, 128, 8, 0,
                                                      BPTR_CACHE_CAPACITY_MIN);
        snprintf(name, sizeof name, "minimum-cache-root-%u", lite);
        insert_begin(name, &config);
        insert_create();
        for (int64_t key = 0; key < 4; key++) {
            value_bytes(value, sizeof value, (uint64_t)key + 90);
            insert_apply(&key, value);
        }
        insert_checkpoint(1);
        int64_t duplicate = 2;
        memset(value, 0x21, sizeof value);
        insert_apply(&duplicate, value);
        insert_finish();

        /* A nonfull two-leaf fixture permits a real cold search at capacity
         * two without assuming a tiny cache can support a cascading split. */
        uint32_t left[] = {0}, right[] = {1};
        struct temp_occupancy occupancy[] = {
            {NULL, 0, 1}, {left, 1, 2}, {right, 1, 2}
        };
        struct temp_image_shape shape = {2, occupancy, 3};
        snprintf(name, sizeof name, "minimum-cache-search-%u", lite);
        insert_begin(name, &config);
        for (size_t i = 0; i < 4; i++) {
            int64_t key = (int64_t)i * 16;
            value_bytes(value, sizeof value, i + 180);
            insert_seed_record(&key, value);
        }
        insert_generate(&shape);
        int64_t key = 8;
        value_bytes(value, sizeof value, 810);
        insert_apply(&key, value);
        insert_checkpoint(1);
        key = 40;
        value_bytes(value, sizeof value, 811);
        insert_apply(&key, value);
        insert_finish();

        config.cache = 16;
        snprintf(name, sizeof name, "cache-eviction-%u", lite);
        insert_begin(name, &config);
        insert_create();
        size_t order[96];
        shuffled(order, 96, insert_seed ^ (lite + 81));
        for (size_t i = 0; i < 96; i++) {
            key = (int64_t)order[i] * 4;
            value_bytes(value, sizeof value, order[i] + 810);
            insert_apply(&key, value);
            if (i == 47) insert_checkpoint(1);
        }
        insert_require(insert_state.tree->node_cnt > config.cache,
                       "eviction workload never exceeded its cache capacity");
        insert_finish();
    }
}
