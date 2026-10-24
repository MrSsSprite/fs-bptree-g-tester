#define _POSIX_C_SOURCE 200809L
#include "temp_manifest.h"
#include "temp_compare.h"
#include "temp_json.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct storage {
    struct temp_json *json;
    struct temp_comparator_module module;
    struct temp_manifest *manifest;
    const char *id;
    unsigned char *data;
    int64_t start, step, value_start, value_step;
    int profile; /* 1 bytes, 2 signed i64, 3 unsigned u64, 4 prefix, 5 equal */
    size_t prefix;
    int direction, nonunit;
    _Bool lattice;
};
static int problem(char *error, size_t size, const char *message)
{ snprintf(error, size, "%s", message); return -1; }
static int fields(const struct temp_json *j, const char *allowed, char *error, size_t size)
{
    if (!j || j->type != 'o') return problem(error, size, "expected a JSON object");
    for (const struct temp_json *c = j->child; c; c = c->next) {
        size_t n = strlen(c->name);
        const char *at = allowed;
        for (;;) {
            if (!strncmp(at, c->name, n) && (at[n] == ' ' || at[n] == 0)) break;
            at = strchr(at, ' ');
            if (!at) { snprintf(error, size, "unknown field: %s", c->name); return -1; }
            ++at;
        }
    }
    return 0;
}
static const char *text(const struct temp_json *j)
{ return j && j->type == 's' ? j->text : NULL; }
static int unsigned_value(const struct temp_json *j, uint64_t limit, uint64_t *out)
{
    if (!j || (j->type != 'n' && j->type != 's') || !j->text[0]) return -1;
    uint64_t n = 0;
    for (const char *p = j->text; *p; ++p) {
        if (*p < '0' || *p > '9' || n > limit / 10 || (n == limit / 10 && (unsigned)(*p - '0') > limit % 10)) return -1;
        n = n * 10 + (unsigned)(*p - '0');
    }
    *out = n; return 0;
}
static int signed_value(const struct temp_json *j, int64_t *out)
{
    if (!j || (j->type != 'n' && j->type != 's') || !j->text[0]) return -1;
    const char *p = j->text;
    _Bool negative = *p == '-';
    if (negative) ++p;
    if (!*p) return -1;
    uint64_t n = 0, limit = negative ? UINT64_C(1) << 63 : INT64_MAX;
    for (; *p; ++p) {
        if (*p < '0' || *p > '9' || n > limit / 10 || (n == limit / 10 && (unsigned)(*p - '0') > limit % 10)) return -1;
        n = n * 10 + (unsigned)(*p - '0');
    }
    *out = negative ? n == (UINT64_C(1) << 63) ? INT64_MIN : -(int64_t)n : (int64_t)n;
    return 0;
}
static int offset_i64(int64_t start, int64_t step, uint64_t index, int64_t *out)
{
    uint64_t magnitude = step < 0 ? (uint64_t)(-(step + 1)) + 1 : (uint64_t)step;
    if (magnitude && index > UINT64_MAX / magnitude) return -1;
    uint64_t delta = magnitude * index;
    uint64_t limit = step < 0 ? (uint64_t)start - (uint64_t)INT64_MIN : (uint64_t)INT64_MAX - (uint64_t)start;
    if (delta > limit) return -1;
    if (step >= 0) {
        if (start >= 0) *out = start + (int64_t)delta;
        else {
            uint64_t absolute = (uint64_t)(-(start + 1)) + 1;
            if (delta >= absolute) *out = (int64_t)(delta - absolute);
            else { uint64_t n = absolute - delta; *out = n == (UINT64_C(1) << 63) ? INT64_MIN : -(int64_t)n; }
        }
    } else {
        if (start > 0 && delta <= (uint64_t)start) *out = start - (int64_t)delta;
        else {
            uint64_t n = start > 0 ? delta - (uint64_t)start : (uint64_t)(-(start + 1)) + 1 + delta;
            *out = n == (UINT64_C(1) << 63) ? INT64_MIN : -(int64_t)n;
        }
    }
    return 0;
}
static int get_record(void *context, uint64_t index, void *key, void *value)
{
    struct storage *s = context;
    struct temp_manifest *m = s->manifest;
    if (index >= m->records.count) return -1;
    if (s->lattice) {
        int64_t k, v;
        if (offset_i64(s->start, s->step, index, &k) || offset_i64(s->value_start, s->value_step, index, &v)) return -1;
        memcpy(key, &k, 8); memcpy(value, &v, 8);
    } else {
        size_t stride = (size_t)m->layout.key_size + m->layout.value_size;
        memcpy(key, s->data + (size_t)index * stride, m->layout.key_size);
        memcpy(value, s->data + (size_t)index * stride + m->layout.key_size, m->layout.value_size);
    }
    return 0;
}
static int compare(const void *lhs, const void *rhs, void *context)
{
    struct storage *s = context;
    if (s->module.descriptor) return s->module.descriptor->compare(lhs, rhs);
    int c = 0;
    if (s->profile == 1 || s->profile == 4) c = memcmp(lhs, rhs, s->prefix);
    else if (s->profile == 2) {
        int64_t a, b; memcpy(&a, lhs, 8); memcpy(&b, rhs, 8); c = (a > b) - (a < b);
    } else if (s->profile == 3) {
        uint64_t a, b; memcpy(&a, lhs, 8); memcpy(&b, rhs, 8); c = (a > b) - (a < b);
    }
    c = s->direction * ((c > 0) - (c < 0));
    return s->nonunit ? c < 0 ? -17 : c > 0 ? 31 : 0 : c;
}
static int unhex(const char *p, unsigned char *out, size_t width)
{
    if (!p || strlen(p) != width * 2) return -1;
    for (size_t i = 0; i < width; ++i) {
        unsigned n = 0;
        for (unsigned j = 0; j < 2; ++j) {
            unsigned char c = (unsigned char)p[i * 2 + j];
            unsigned d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10u : c >= 'A' && c <= 'F' ? c - 'A' + 10u : 16u;
            if (d == 16) return -1;
            n = n * 16 + d;
        }
        out[i] = (unsigned char)n;
    }
    return 0;
}
static char *relative_library(const char *manifest, const char *library)
{
    if (library[0] == '/') return strdup(library);
    const char *slash = strrchr(manifest, '/');
    size_t prefix = slash ? (size_t)(slash - manifest) + 1 : 2;
    if (strlen(library) > SIZE_MAX - prefix - 1) return NULL;
    char *path = malloc(prefix + strlen(library) + 1);
    if (!path) return NULL;
    if (slash) memcpy(path, manifest, prefix); else memcpy(path, "./", 2);
    strcpy(path + prefix, library); return path;
}
static int parse_comparator(const struct temp_json *j, const char *path, const char *override,
                            struct storage *s, char *error, size_t size)
{
    if (fields(j, "profile library id prefix_bytes", error, size)) return -1;
    const char *profile = text(temp_json_get(j, "profile"));
    const char *library = text(temp_json_get(j, "library"));
    const char *id = text(temp_json_get(j, "id"));
    uint16_t width = s->manifest->layout.key_size;
    if ((temp_json_get(j, "profile") && !profile) ||
        (temp_json_get(j, "library") && (!library || !library[0])) ||
        (temp_json_get(j, "id") && !id))
        return problem(error, size, "comparator profile, library and ID must be strings");
    if ((profile && (library || id || override)) || (!profile && !library && !override))
        return problem(error, size, "choose one builtin profile or an external comparator library");
    if (!profile) {
        if (!id || !id[0] || temp_json_get(j, "prefix_bytes"))
            return problem(error, size, "external comparator requires an ID and no prefix_bytes");
        char *resolved = relative_library(override ? "./" : path, override ? override : library);
        if (!resolved) return problem(error, size, "cannot allocate comparator path");
        int result = temp_comparator_open(&s->module, resolved, width, id, error, size);
        free(resolved);
        if (result) return -1;
        s->id = s->module.descriptor->id; return 0;
    }
    s->id = profile; s->direction = strstr(profile, "-desc") ? -1 : 1;
    if (!strcmp(profile, "bytes-asc") || !strcmp(profile, "bytes-desc") || !strcmp(profile, "bytes-nonunit")) {
        s->profile = 1; s->prefix = width; s->nonunit = !strcmp(profile, "bytes-nonunit");
    } else if (!strcmp(profile, "i64-asc") || !strcmp(profile, "i64-desc")) s->profile = 2;
    else if (!strcmp(profile, "u64-asc") || !strcmp(profile, "u64-desc")) s->profile = 3;
    else if (!strcmp(profile, "prefix-asc") || !strcmp(profile, "prefix-desc")) {
        uint64_t n;
        if (unsigned_value(temp_json_get(j, "prefix_bytes"), width, &n) || n == 0)
            return problem(error, size, "prefix comparator requires prefix_bytes in [1,key_size]");
        s->profile = 4; s->prefix = (size_t)n;
    } else if (!strcmp(profile, "all-equal")) s->profile = 5;
    else return problem(error, size, "unknown comparator profile");
    if ((s->profile == 2 || s->profile == 3) && width != 8)
        return problem(error, size, "numeric comparator requires 8-byte keys");
    if (s->profile != 4 && temp_json_get(j, "prefix_bytes"))
        return problem(error, size, "prefix_bytes only applies to prefix comparators");
    return 0;
}
static int parse_shape(const struct temp_json *j, struct temp_manifest *m, char *error, size_t size)
{
    if (!j) return 0;
    if (fields(j, "height node_keys", error, size)) return -1;
    uint64_t height;
    if (unsigned_value(temp_json_get(j, "height"), 254, &height)) return problem(error, size, "invalid shape height");
    m->has_shape = 1; m->shape.height = (uint32_t)height;
    const struct temp_json *keys = temp_json_get(j, "node_keys");
    if (!keys) return 0;
    if (keys->type != 'o') return problem(error, size, "node_keys must be an object");
    for (const struct temp_json *at = keys->child; at; at = at->next) ++m->shape.override_count;
    struct temp_occupancy *overrides = calloc(m->shape.override_count ? m->shape.override_count : 1, sizeof *overrides);
    if (!overrides) return problem(error, size, "cannot allocate shape overrides");
    m->shape.overrides = overrides;
    size_t index = 0;
    for (const struct temp_json *at = keys->child; at; at = at->next, ++index) {
        uint64_t count;
        if (unsigned_value(at, UINT32_MAX, &count)) return problem(error, size, "invalid node key count");
        overrides[index].keys = (uint32_t)count;
        if (!strcmp(at->name, "root")) continue;
        uint32_t path_parts[254]; size_t depth = 0;
        const char *p = at->name;
        while (*p) {
            if (depth == 254 || *p < '0' || *p > '9' || (*p == '0' && p[1] >= '0' && p[1] <= '9'))
                return problem(error, size, "noncanonical shape path");
            uint64_t n = 0;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (unsigned)(*p++ - '0'); if (n > UINT32_MAX) return problem(error, size, "shape child index overflow"); }
            path_parts[depth++] = (uint32_t)n;
            if (!*p) break;
            if (*p++ != '.' || !*p) return problem(error, size, "invalid shape path");
        }
        if (!depth) return problem(error, size, "empty shape path");
        uint32_t *parts = malloc(depth * sizeof *parts);
        if (!parts) return problem(error, size, "cannot allocate shape path");
        memcpy(parts, path_parts, depth * sizeof *parts);
        overrides[index].path = parts; overrides[index].depth = depth;
    }
    return 0;
}
static int parse_records(const struct temp_json *j, struct storage *s, char *error, size_t size)
{
    struct temp_manifest *m = s->manifest;
    if (!j) return problem(error, size, "records is required");
    if (j->type == 'a') {
        for (const struct temp_json *at = j->child; at; at = at->next) ++m->records.count;
        size_t stride = (size_t)m->layout.key_size + m->layout.value_size;
        if (m->records.count > SIZE_MAX / stride) return problem(error, size, "record storage exceeds address space");
        s->data = malloc(m->records.count ? (size_t)m->records.count * stride : 1);
        if (!s->data) return problem(error, size, "cannot allocate record data");
        uint64_t i = 0;
        for (const struct temp_json *at = j->child; at; at = at->next, ++i) {
            if (fields(at, "key_hex value_hex", error, size)) return -1;
            unsigned char *key = s->data + (size_t)i * stride;
            if (unhex(text(temp_json_get(at, "key_hex")), key, m->layout.key_size) ||
                unhex(text(temp_json_get(at, "value_hex")), key + m->layout.key_size, m->layout.value_size))
                return problem(error, size, "record hex must match exact key/value widths");
            if (i && compare(key - stride, key, s) >= 0) return problem(error, size, "expected records must be strictly comparator ordered");
        }
    } else {
        if (fields(j, "kind count start step value_start value_step", error, size)) return -1;
        const char *kind = text(temp_json_get(j, "kind"));
        if (!kind || strcmp(kind, "i64-lattice") || m->layout.key_size != 8 || m->layout.value_size != 8)
            return problem(error, size, "i64-lattice requires 8-byte keys and values");
        if (unsigned_value(temp_json_get(j, "count"), UINT64_MAX, &m->records.count) ||
            signed_value(temp_json_get(j, "start"), &s->start) || signed_value(temp_json_get(j, "step"), &s->step) ||
            signed_value(temp_json_get(j, "value_start"), &s->value_start) || signed_value(temp_json_get(j, "value_step"), &s->value_step))
            return problem(error, size, "invalid lattice integer parameters");
        int64_t end;
        if (m->records.count && (offset_i64(s->start, s->step, m->records.count - 1, &end) ||
                                offset_i64(s->value_start, s->value_step, m->records.count - 1, &end)))
            return problem(error, size, "lattice endpoint overflow");
        s->lattice = 1;
        if (m->records.count > 1 && s->step == 0) return problem(error, size, "lattice keys must be distinct");
    }
    return 0;
}
void temp_manifest_free(struct temp_manifest *m)
{
    if (!m) return;
    for (size_t i = 0; m->shape.overrides && i < m->shape.override_count; ++i) free((void *)m->shape.overrides[i].path);
    free((void *)m->shape.overrides);
    struct storage *s = m->storage;
    if (s) { temp_comparator_close(&s->module); temp_json_free(s->json); free(s->data); free(s); }
    memset(m, 0, sizeof *m);
}
const char *temp_manifest_comparator_id(const struct temp_manifest *m)
{ return ((const struct storage *)m->storage)->id; }
int temp_manifest_load(const char *path, const char *override,
                       struct temp_manifest *m, char *error, size_t size)
{
    uint64_t n;
    if (m->storage) return problem(error, size, "manifest output must be zero initialized");
    struct storage *s = calloc(1, sizeof *s);
    if (!s) return problem(error, size, "cannot allocate manifest");
    m->storage = s; s->manifest = m;
    s->json = temp_json_read(path, error, size);
    if (!s->json) goto fail;
    if (fields(s->json, "schema byte_order layout comparator records initial_shape expect", error, size)) goto fail;
    if (unsigned_value(temp_json_get(s->json, "schema"), 1, &n) || n != 1) { problem(error, size, "unsupported manifest schema"); goto fail; }
    const struct temp_json *j = temp_json_get(s->json, "byte_order");
    if (j) {
        const char *endian = text(j);
        uint16_t one = 1;
        const char *native = *(unsigned char *)&one ? "little" : "big";
        if (!endian || (strcmp(endian, "native") && strcmp(endian, native))) { problem(error, size, "unsupported manifest byte_order on this host"); goto fail; }
    }
    j = temp_json_get(s->json, "layout");
    if (fields(j, "lite node_size key_size value_size", error, size)) goto fail;
    const struct temp_json *lite = temp_json_get(j, "lite");
    if (!lite || (lite->type != 't' && lite->type != 'f')) { problem(error, size, "layout.lite must be boolean"); goto fail; }
    m->layout.is_lite = lite->type == 't';
    if (unsigned_value(temp_json_get(j, "node_size"), UINT32_MAX, &n)) goto layout_fail;
    m->layout.node_size = (uint32_t)n;
    if (unsigned_value(temp_json_get(j, "key_size"), UINT16_MAX, &n) || n == 0) goto layout_fail;
    m->layout.key_size = (uint16_t)n;
    if (unsigned_value(temp_json_get(j, "value_size"), UINT16_MAX, &n) || n == 0) goto layout_fail;
    m->layout.value_size = (uint16_t)n;
    if (parse_comparator(temp_json_get(s->json, "comparator"), path, override, s, error, size)) goto fail;
    m->records.context = s; m->records.get = get_record;
    m->order.context = s; m->order.compare = compare;
    if (parse_shape(temp_json_get(s->json, "initial_shape"), m, error, size) ||
        parse_records(temp_json_get(s->json, "records"), s, error, size)) goto fail;
    struct temp_image_stats stats;
    struct temp_image_result result;
    struct temp_image_shape empty = {0};
    if (temp_image_plan(&m->layout, m->has_shape ? &m->shape : &empty, &stats, &result)) { problem(error, size, result.detail); goto fail; }
    j = temp_json_get(s->json, "expect");
    if (j) {
        if (fields(j, "height nodes leaves", error, size)) goto fail;
        const struct temp_json *at = temp_json_get(j, "height");
        if (at) { if (unsigned_value(at, 254, &n)) goto totals_fail; m->constraints.height = (uint32_t)n; m->constraints.mask |= TEMP_EXPECT_HEIGHT; }
        at = temp_json_get(j, "nodes");
        if (at) { if (unsigned_value(at, UINT64_MAX, &n)) goto totals_fail; m->constraints.nodes = n; m->constraints.mask |= TEMP_EXPECT_NODES; }
        at = temp_json_get(j, "leaves");
        if (at) { if (unsigned_value(at, UINT64_MAX, &n)) goto totals_fail; m->constraints.leaves = n; m->constraints.mask |= TEMP_EXPECT_LEAVES; }
    }
    return 0;
totals_fail:
    problem(error, size, "invalid optional topology expectation"); goto fail;
layout_fail:
    problem(error, size, "invalid layout widths or node size");
fail:
    temp_manifest_free(m); return -1;
}

static void json_string(const char *p)
{
    putchar('"');
    for (; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c < 32) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}
static void report(const char *tool, const char *path, const char *id,
                    const struct temp_image_result *r, _Bool json, _Bool generate)
{
    if (json) {
        printf("{\"status\":%d,\"path\":", r->status); json_string(path ? path : "");
        printf(",\"invariant\":"); json_string(r->invariant);
        printf(",\"detail\":"); json_string(r->detail);
        printf(",\"comparator\":"); json_string(id ? id : "");
        printf(",\"node\":\"%" PRIu64 "\",\"slot\":%u,\"record\":\"%" PRIu64 "\","
               "\"reused\":%s,\"height\":%u,\"records\":\"%" PRIu64 "\",\"nodes\":\"%" PRIu64
               "\",\"leaves\":\"%" PRIu64 "\",\"free_nodes\":\"%" PRIu64 "\"}\n",
               r->node, r->slot, r->record, r->reused ? "true" : "false", r->actual.height,
               r->actual.records, r->actual.nodes, r->actual.leaves, r->actual.free_nodes);
    } else if (r->status) fprintf(stderr, "%s: %s: %s node=%" PRIu64 " slot=%u record=%" PRIu64 ": %s\n",
                                 tool, path ? path : "", r->invariant, r->node, r->slot, r->record, r->detail);
    else printf("%s: %s: %s (records=%" PRIu64 ", nodes=%" PRIu64 ", leaves=%" PRIu64 ", height=%u, comparator=%s)\n",
                tool, path, generate ? r->reused ? "reused" : "written" : "ok", r->actual.records,
                r->actual.nodes, r->actual.leaves, r->actual.height, id);
}
int temp_manifest_cli(int argc, char **argv, _Bool generate)
{
    const char *spec = NULL, *path = NULL, *library = NULL, *checks = NULL;
    _Bool json = 0, contents = 1, shape = 0;
    struct temp_manifest m = {0};
    struct temp_image_result r = {0};
    const char *tool = generate ? "temp_gen" : "temp_verify";
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--json")) json = 1;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--json")) json = 1;
        else if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            printf("usage: %s %s MANIFEST %s [--comparator-library LIB] [--json]%s\n",
                   tool, generate ? "--spec" : "--expect", generate ? "--out IMAGE" : "IMAGE",
                   generate ? "" : " [--check contents,structure[,shape]]"); return 0;
        } else if (!strcmp(arg, generate ? "--spec" : "--expect")) {
            if (++i == argc || spec) goto usage;
            spec = argv[i];
        } else if (!strcmp(arg, "--comparator-library")) {
            if (++i == argc || library) goto usage;
            library = argv[i];
        } else if (generate && !strcmp(arg, "--out")) {
            if (++i == argc || path) goto usage;
            path = argv[i];
        } else if (!generate && !strcmp(arg, "--check")) {
            if (++i == argc || checks) goto usage;
            checks = argv[i];
        } else if (!generate && arg[0] != '-' && !path) path = arg;
        else goto usage;
    }
    if (!spec || !path || !path[0]) goto usage;
    if (checks) {
        contents = 0;
        const char *p = checks;
        do {
            const char *end = strchr(p, ',');
            size_t n = end ? (size_t)(end - p) : strlen(p);
            if (n == 8 && !strncmp(p, "contents", n)) contents = 1;
            else if (n == 9 && !strncmp(p, "structure", n)) { }
            else if (n == 5 && !strncmp(p, "shape", n)) shape = 1;
            else goto usage;
            if (!end) break;
            p = end + 1;
        } while (1);
    }
    if (temp_manifest_load(spec, library, &m, r.detail, sizeof r.detail)) {
        r.status = TEMP_IMAGE_SPEC; snprintf(r.invariant, sizeof r.invariant, "manifest");
    } else if ((generate || shape) && !m.has_shape) {
        r.status = TEMP_IMAGE_SPEC; snprintf(r.invariant, sizeof r.invariant, "shape");
        snprintf(r.detail, sizeof r.detail, "initial_shape is required for generation or exact shape checks");
    } else if (generate) temp_image_generate(path, &m.layout, &m.shape, &m.records, &m.order, &r);
    else temp_image_verify(path, &m.layout, contents ? &m.records : NULL, &m.order,
                            shape ? &m.shape : NULL, &m.constraints, &r);
    report(tool, path, m.storage ? temp_manifest_comparator_id(&m) : NULL, &r, json, generate);
    int status = r.status;
    temp_manifest_free(&m);
    return status;
usage:
    r.status = TEMP_IMAGE_SPEC; snprintf(r.invariant, sizeof r.invariant, "usage");
    snprintf(r.detail, sizeof r.detail, "invalid manifest command; use --%s FILE --help", generate ? "spec" : "expect");
    report(tool, path, NULL, &r, json, generate); return r.status;
}
