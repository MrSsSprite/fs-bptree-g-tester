#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#include "harness.h"
#include "progress.h"
#include "unity.h"
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct insert_state insert_state;
uint64_t insert_seed = UINT64_C(0x61c8864680b583eb);
const char *insert_work_dir = ".cache/insert";

static struct {
    size_t cases, operations, inserted, duplicates, live_checks, checkpoints;
    size_t errors;
} totals;

static void hex_write(FILE *output, const void *data, size_t count)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < count; i++) fprintf(output, "%02x", bytes[i]);
}

static void json_string(FILE *output, const char *text)
{
    fputc('"', output);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', output);
        if (*p < 32) fprintf(output, "\\u%04x", *p);
        else fputc(*p, output);
    }
    fputc('"', output);
}

/* Expected records always come from the test-owned model, never the tree. */
static int manifest_write(const char *path, const struct temp_image_shape *shape)
{
    struct insert_state *s = &insert_state;
    const struct temp_layout *l = &s->config.layout;
    FILE *out = fopen(path, "w");
    if (!out) return -1;
    fprintf(out, "{\n  \"schema\": 1, \"byte_order\": \"native\",\n"
                 "  \"layout\": {\"lite\": %s, \"node_size\": %" PRIu32
                 ", \"key_size\": %u, \"value_size\": %u},\n"
                 "  \"comparator\": {\"profile\": ",
            l->is_lite ? "true" : "false", l->node_size,
            (unsigned)l->key_size, (unsigned)l->value_size);
    json_string(out, s->config.profile);
    if (s->config.prefix_bytes)
        fprintf(out, ", \"prefix_bytes\": %zu", s->config.prefix_bytes);
    fputs("},\n", out);
    if (shape) {
        fprintf(out, "  \"initial_shape\": {\"height\": %" PRIu32
                     ", \"node_keys\": {", shape->height);
        for (size_t i = 0; i < shape->override_count; i++) {
            const struct temp_occupancy *o = &shape->overrides[i];
            if (i) fputs(", ", out);
            fputc('"', out);
            if (!o->depth) fputs("root", out);
            for (size_t j = 0; j < o->depth; j++)
                fprintf(out, "%s%" PRIu32, j ? "." : "", o->path[j]);
            fprintf(out, "\": %" PRIu32, o->keys);
        }
        fputs("}},\n", out);
    }
    fputs("  \"records\": [", out);
    for (size_t i = 0; i < s->model.count; i++) {
        fputs(i ? ",\n    {\"key_hex\": \"" : "\n    {\"key_hex\": \"", out);
        hex_write(out, insert_model_key(&s->model, i), l->key_size);
        fputs("\", \"value_hex\": \"", out);
        hex_write(out, insert_model_value(&s->model, i), l->value_size);
        fputs("\"}", out);
    }
    fputs("\n  ]\n}\n", out);
    int failed = ferror(out);
    if (fclose(out)) failed = 1;
    return failed ? -1 : 0;
}

void insert_require(_Bool ok, const char *format, ...)
{
    char detail[1024], message[1400];
    va_list args;
    if (ok) return;
    va_start(args, format);
    vsnprintf(detail, sizeof detail, format, args);
    va_end(args);
    snprintf(message, sizeof message, "%s operation %zu: %s",
             insert_state.name, insert_state.operations, detail);
    insert_state.failed = 1;
    progress_break();
    if (insert_state.dir[0]) {
        char path[PATH_MAX];
        int n = snprintf(path, sizeof path, "%s/failure.txt", insert_state.dir);
        if (n > 0 && (size_t)n < sizeof path) {
            FILE *out = fopen(path, "w");
            if (out) {
                fprintf(out, "%s\nseed=%" PRIu64 "\n", message, insert_seed);
                fclose(out);
            }
        }
        if (insert_state.manifest[0])
            (void)manifest_write(insert_state.manifest, NULL);
    }
    TEST_FAIL_MESSAGE(message);
}

static void make_directory(const char *path)
{
    char copy[PATH_MAX];
    size_t length = strlen(path);
    insert_require(length > 0 && length < sizeof copy,
                   "work directory path is empty or too long");
    memcpy(copy, path, length + 1);
    for (size_t i = 1; i <= length; i++) {
        if (copy[i] != '/' && copy[i] != '\0') continue;
        char saved = copy[i];
        copy[i] = '\0';
        int status = mkdir(copy, 0700);
        insert_require(status == 0 || errno == EEXIST,
                       "mkdir %s: %s", copy, strerror(errno));
        struct stat info;
        insert_require(stat(copy, &info) == 0 && S_ISDIR(info.st_mode),
                       "%s is not a directory", copy);
        copy[i] = saved;
    }
}

static void owned_path(char *path, size_t size, const char *name)
{
    int n = snprintf(path, size, "%s/%s", insert_state.dir, name);
    insert_require(n > 0 && (size_t)n < size, "owned path is too long");
}

void insert_begin(const char *name, const struct insert_config *config)
{
    struct insert_state *s = &insert_state;
    insert_require(!s->dir[0] && !s->tree, "previous case was not finished");
    memset(s, 0, sizeof *s);
    insert_require(strlen(name) < sizeof s->name, "case name too long");
    strcpy(s->name, name);
    s->config = *config;
    insert_model_init(&s->model, config->layout.key_size,
                      config->layout.value_size, config->compare);
    make_directory(insert_work_dir);
    int n = snprintf(s->dir, sizeof s->dir, "%s/run-XXXXXX", insert_work_dir);
    insert_require(n > 0 && (size_t)n < sizeof s->dir, "work path too long");
    if (!mkdtemp(s->dir)) {
        s->dir[0] = '\0';
        insert_require(0, "mkdtemp: %s", strerror(errno));
    }
    owned_path(s->path, sizeof s->path, "instance.bptr");
    owned_path(s->manifest, sizeof s->manifest, "expected.json");
    char path[PATH_MAX];
    owned_path(path, sizeof path, "operations.jsonl");
    s->log = fopen(path, "w");
    insert_require(s->log != NULL, "open operation log: %s", strerror(errno));
    s->input_copy = malloc(s->model.key_size + s->model.value_size);
    insert_require(s->input_copy != NULL, "allocate input snapshot");
    fputs("{\"case\":", s->log);
    json_string(s->log, name);
    fprintf(s->log, ",\"seed\":\"%" PRIu64 "\",\"cache\":\"%" PRIu64 "\"}\n",
            insert_seed, config->cache);
    insert_require(fflush(s->log) == 0, "flush operation log");
    totals.cases++;
}

void insert_create(void)
{
    struct insert_state *s = &insert_state;
    const struct temp_layout *l = &s->config.layout;
    insert_require(!s->tree && !s->model.count, "create requires an empty model");
    bptr_errno = 0;
    s->tree = bptr_init(s->path, l->is_lite, l->node_size, l->key_size,
                        l->value_size, s->config.cache, s->config.compare);
    insert_require(s->tree != NULL, "setup bptr_init returned NULL (errno=%d)",
                   bptr_errno);
    insert_check_live();
}

void insert_seed_record(const void *key, const void *value)
{
    insert_require(!insert_state.tree, "cannot seed model after opening tree");
    int status = insert_model_add(&insert_state.model, key, value);
    insert_require(status == 0, "fixture model seed returned %d", status);
}

static int model_record(void *context, uint64_t index, void *key, void *value)
{
    const struct insert_model *model = context;
    if (index >= model->count) return -1;
    memcpy(key, insert_model_key(model, (size_t)index), model->key_size);
    memcpy(value, insert_model_value(model, (size_t)index), model->value_size);
    return 0;
}

static int model_compare(const void *lhs, const void *rhs, void *context)
{
    const struct insert_model *model = context;
    return model->compare(lhs, rhs);
}

static void verify_image(const struct temp_image_shape *shape)
{
    struct insert_state *s = &insert_state;
    struct temp_records records = { s->model.count, &s->model, model_record };
    struct temp_order order = { &s->model, model_compare };
    struct temp_image_result result;
    int status = temp_image_verify(s->path, &s->config.layout, &records, &order,
                                   shape, NULL, &result);
    insert_require(status == 0,
                   "persisted %s: node %" PRIu64 " slot %" PRIu32
                   " record %" PRIu64 ": %s (status=%d)",
                   result.invariant, result.node, result.slot, result.record,
                   result.detail, status);
}

void insert_generate(const struct temp_image_shape *shape)
{
    struct insert_state *s = &insert_state;
    struct temp_records records = { s->model.count, &s->model, model_record };
    struct temp_order order = { &s->model, model_compare };
    struct temp_image_result result;
    char path[PATH_MAX];
    insert_require(!s->tree, "fixture generation requires a closed tree");
    owned_path(path, sizeof path, "initial.json");
    insert_require(manifest_write(path, shape) == 0, "write initial manifest");
    int status = temp_image_generate(s->path, &s->config.layout, shape,
                                     &records, &order, &result);
    insert_require(status == 0, "fixture %s: %s (status=%d)",
                   result.invariant, result.detail, status);
    verify_image(shape);
    s->tree = bptr_load(s->path, s->config.cache, s->config.compare);
    insert_require(s->tree != NULL, "fixture bptr_load failed (errno=%d)",
                   bptr_errno);
    /* Leave the loaded fixture cold. The first observer runs AFTER insert. */
}

static void operation_log(int expected, int actual, const char *phase)
{
    struct insert_state *s = &insert_state;
    fprintf(s->log, "{\"operation\":%zu,\"phase\":", s->operations);
    json_string(s->log, phase);
    fprintf(s->log, ",\"expected\":%d,\"actual\":%d,\"key_hex\":\"", expected, actual);
    hex_write(s->log, s->input_copy, s->model.key_size);
    fputs("\",\"value_hex\":\"", s->log);
    hex_write(s->log, s->input_copy + s->model.key_size, s->model.value_size);
    fputs("\"}\n", s->log);
    insert_require(!ferror(s->log) && fflush(s->log) == 0, "write operation log");
}

static int call_insert(const void *key, const void *value, int expected,
                       const char *phase)
{
    struct insert_state *s = &insert_state;
    insert_require(s->tree != NULL, "insertion requires an open tree");
    memcpy(s->input_copy, key, s->model.key_size);
    memcpy(s->input_copy + s->model.key_size, value, s->model.value_size);
    s->operations++;
    totals.operations++;
    progress_update("    insert %s: operation %zu", s->name, s->operations);
    /* Deliberately stale; neither its old nor new value is an oracle. */
    bptr_errno = s->operations % 2 ? BPTR_E_FSEEK : BPTR_E_KEY_EXIST;
    int status = bptr_insert(s->tree, key, value);
    operation_log(expected, status, phase);
    char key_hex[65];
    size_t shown = s->model.key_size < 32 ? s->model.key_size : 32;
    for (size_t i = 0; i < shown; i++)
        snprintf(key_hex + i * 2, sizeof key_hex - i * 2, "%02x", s->input_copy[i]);
    key_hex[shown * 2] = '\0';
    insert_require(status == expected,
                   "%s key %s: expected return %d; actual %d (bptr_errno=%d)",
                   phase, key_hex, expected, status, bptr_errno);
    insert_require(memcmp(key, s->input_copy, s->model.key_size) == 0 &&
                   memcmp(value, s->input_copy + s->model.key_size,
                          s->model.value_size) == 0,
                   "%s modified caller input bytes", phase);
    return status;
}

void insert_apply(const void *key, const void *value)
{
    struct insert_state *s = &insert_state;
    int expected = insert_model_add(&s->model, key, value);
    insert_require(expected == 0 || expected == BPTR_E_KEY_EXIST,
                   "reference model insertion failed: %d", expected);
    int status = call_insert(key, value, expected, "insert");
    if (status == 0) { s->inserted++; totals.inserted++; }
    else { s->duplicates++; totals.duplicates++; }
    insert_check_live();
}

void insert_expect_error(const void *key, const void *value, int expected,
                         const char *phase)
{
    insert_require(expected != 0 && expected != BPTR_E_KEY_EXIST,
                   "error probe needs a resource error");
    (void)call_insert(key, value, expected, phase);
    insert_state.errors++;
    totals.errors++;
}

void insert_check_live(void)
{
    char reason[768];
    int status = insert_live_verify(insert_state.tree, &insert_state.model,
                                    reason, sizeof reason);
    insert_require(status == 0, "%s", reason);
    insert_state.live_checks++;
    totals.live_checks++;
}

static void release_references(void)
{
    struct insert_state *s = &insert_state;
    if (!s->tree) return;
    for (size_t i = 0; i < sizeof s->refs / sizeof *s->refs; i++) {
        if (s->refs[i]) bptr_node_unload(s->tree, s->refs[i]);
        s->refs[i] = NULL;
    }
}

void insert_close(void)
{
    struct insert_state *s = &insert_state;
    if (!s->tree) return;
    release_references();
    int status = bptr_unload(s->tree);
    if (status == 0 || status == BPTR_E_FCLOSE) s->tree = NULL;
    insert_require(status == 0, "bptr_unload returned %d", status);
}

void insert_reopen(uint64_t cache)
{
    struct insert_state *s = &insert_state;
    insert_close();
    s->config.cache = cache;
    s->tree = bptr_load(s->path, cache, s->config.compare);
    insert_require(s->tree != NULL, "bptr_load returned NULL (errno=%d)", bptr_errno);
}

void insert_checkpoint(_Bool reopen)
{
    struct insert_state *s = &insert_state;
    insert_close();
    insert_require(manifest_write(s->manifest, NULL) == 0, "write expected manifest");
    verify_image(NULL);
    s->checkpoints++;
    totals.checkpoints++;
    if (reopen) {
        s->tree = bptr_load(s->path, s->config.cache, s->config.compare);
        insert_require(s->tree != NULL, "checkpoint bptr_load failed (errno=%d)",
                       bptr_errno);
        /* Keep it cold until the next insertion. */
    }
}

static void remove_owned(void)
{
    static const char *names[] = {
        "instance.bptr", "operations.jsonl", "expected.json", "initial.json"
    };
    struct insert_state *s = &insert_state;
    if (s->log) { int status = fclose(s->log); s->log = NULL;
        insert_require(status == 0, "close operation log"); }
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        char path[PATH_MAX];
        owned_path(path, sizeof path, names[i]);
        int status = unlink(path);
        insert_require(status == 0 || errno == ENOENT, "remove %s: %s", path,
                       strerror(errno));
    }
    insert_require(rmdir(s->dir) == 0, "remove case directory %s: %s", s->dir,
                   strerror(errno));
    insert_model_free(&s->model);
    free(s->input_copy);
    memset(s, 0, sizeof *s);
}

void insert_finish(void)
{
    insert_check_live();
    insert_checkpoint(0);
    remove_owned();
}

void insert_discard(void)
{
    insert_close();
    remove_owned();
}

void insert_cleanup(void)
{
    struct insert_state *s = &insert_state;
    if (Unity.CurrentTestFailed) s->failed = 1;
    release_references();
    if (s->tree) {
        int status = bptr_unload(s->tree);
        if (status) {
            fprintf(stderr, "insert: cleanup unload returned %d\n", status);
            if (status != BPTR_E_FCLOSE) {
                fprintf(stderr, "insert: retained failed case %s; stopping after "
                                "unrecoverable cleanup\n", s->dir);
                /* The object remains open. Stop rather than orphaning its
                 * resources and running later scenarios against unknown state. */
                exit(EXIT_FAILURE);
            }
        }
        s->tree = NULL;
    }
    if (s->log) fclose(s->log);
    if (s->dir[0])
        fprintf(stderr, "insert: retained failed case %s\n", s->dir);
    insert_model_free(&s->model);
    free(s->input_copy);
    memset(s, 0, sizeof *s);
}

void insert_summary(void)
{
    progress_break();
    printf("insert: %zu cases, %zu operations (%zu inserted, %zu duplicates, "
           "%zu expected errors), %zu live checks, %zu checkpoints\n",
           totals.cases, totals.operations, totals.inserted, totals.duplicates,
           totals.errors, totals.live_checks, totals.checkpoints);
}
