#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "temp_image.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define META 64u
#define MAX_HEIGHT 254u

static int fail(struct temp_image_result *r, enum temp_image_status status,
                const char *invariant, const char *fmt, ...)
{
    va_list ap;
    r->status = status;
    snprintf(r->invariant, sizeof r->invariant, "%s", invariant);
    va_start(ap, fmt);
    vsnprintf(r->detail, sizeof r->detail, fmt, ap);
    va_end(ap);
    return status;
}
static uint64_t number(const unsigned char *p, unsigned width)
{
    uint64_t v = 0;
    if (width == 2) { uint16_t x; memcpy(&x, p, 2); v = x; }
    else if (width == 4) { uint32_t x; memcpy(&x, p, 4); v = x; }
    else memcpy(&v, p, 8);
    return v;
}
static void put(unsigned char *p, uint64_t v, unsigned width)
{
    if (width == 2) { uint16_t x = (uint16_t)v; memcpy(p, &x, 2); }
    else if (width == 4) { uint32_t x = (uint32_t)v; memcpy(p, &x, 4); }
    else memcpy(p, &v, 8);
}
static uint64_t offset_max(void)
{
    unsigned bits = sizeof(off_t) * CHAR_BIT;
    return bits >= 64 ? INT64_MAX : (UINT64_C(1) << (bits - 1)) - 1;
}
static int bounds(const struct temp_layout *l, uint32_t *leaf, uint32_t *branch,
                  struct temp_image_result *r)
{
    uint64_t ptr = l->is_lite ? 4u : 8u;
    if (l->key_size == 0 || l->value_size == 0 ||
        l->node_size < META + ptr + 2u * ((uint64_t)l->key_size + ptr))
        return fail(r, TEMP_IMAGE_SPEC, "layout", "invalid fixed-width node layout");
    *leaf = (l->node_size - META) / ((uint32_t)l->key_size + l->value_size);
    *branch = (l->node_size - META - ptr) / ((uint64_t)l->key_size + ptr);
    if (*leaf == 0)
        return fail(r, TEMP_IMAGE_SPEC, "layout", "node cannot hold a leaf record");
    return 0;
}
static int prefix(const struct temp_occupancy *o, const uint32_t *path, size_t n)
{
    return o->depth >= n && (n == 0 || memcmp(o->path, path, n * sizeof *path) == 0);
}
static uint32_t shape_keys(const struct temp_image_shape *s, const uint32_t *path,
                           size_t depth, uint32_t full)
{
    for (size_t i = 0; i < s->override_count; ++i)
        if (s->overrides[i].depth == depth && prefix(&s->overrides[i], path, depth))
            return s->overrides[i].keys;
    return full;
}
static int accumulate(uint64_t *n, uint64_t value, uint64_t times)
{
    if (times && value > (UINT64_MAX - *n) / times) return -1;
    *n += value * times;
    return 0;
}
static int full_counts(uint32_t height, uint32_t leaf, uint32_t branch,
                       struct temp_image_stats *out)
{
    uint64_t fanout = (uint64_t)branch + 1;
    out->nodes = out->leaves = 1; out->records = leaf;
    for (uint32_t h = 1; h < height; ++h) {
        if (out->nodes > (UINT64_MAX - 1) / fanout ||
            out->leaves > UINT64_MAX / fanout || out->records > UINT64_MAX / fanout)
            return -1;
        out->nodes = out->nodes * fanout + 1;
        out->leaves *= fanout; out->records *= fanout;
    }
    return 0;
}
static int count_shape(const struct temp_image_shape *s, uint32_t *path,
                        uint32_t depth, uint32_t leaf, uint32_t branch,
                        struct temp_image_stats *out)
{
    uint32_t height = s->height - depth, full = height == 1 ? leaf : branch;
    uint32_t keys = shape_keys(s, path, depth, full);
    uint32_t min = depth == 0 ? 1u : height == 1 ? (leaf + 1u) / 2u : branch / 2u;
    if (keys < min || keys > full) return -1;
    memset(out, 0, sizeof *out);
    out->nodes = 1;
    if (height == 1) { out->leaves = 1; out->records = keys; return 0; }
    struct temp_image_stats sub = {0};
    uint64_t special = 0;
    for (size_t i = 0; i < s->override_count; ++i) {
        const struct temp_occupancy *o = &s->overrides[i];
        if (o->depth <= depth || !prefix(o, path, depth)) continue;
        uint32_t child = o->path[depth];
        int seen = 0;
        for (size_t j = 0; j < i; ++j)
            if (s->overrides[j].depth > depth && prefix(&s->overrides[j], path, depth) &&
                s->overrides[j].path[depth] == child) seen = 1;
        if (seen) continue;
        if (child > keys) return -1;
        path[depth] = child;
        if (count_shape(s, path, depth + 1, leaf, branch, &sub) ||
            accumulate(&out->nodes, sub.nodes, 1) ||
            accumulate(&out->leaves, sub.leaves, 1) ||
            accumulate(&out->records, sub.records, 1)) return -1;
        ++special;
    }
    uint64_t remaining = (uint64_t)keys + 1 - special;
    if (remaining && (full_counts(height - 1, leaf, branch, &sub) ||
        accumulate(&out->nodes, sub.nodes, remaining) ||
        accumulate(&out->leaves, sub.leaves, remaining) ||
        accumulate(&out->records, sub.records, remaining))) return -1;
    return 0;
}
int temp_image_plan(const struct temp_layout *l, const struct temp_image_shape *s,
                    struct temp_image_stats *out, struct temp_image_result *r)
{
    uint32_t leaf, branch, path[MAX_HEIGHT];
    memset(r, 0, sizeof *r);
    memset(out, 0, sizeof *out);
    if (bounds(l, &leaf, &branch, r)) return r->status;
    if (s->height > MAX_HEIGHT || (s->override_count && s->overrides == NULL))
        return fail(r, TEMP_IMAGE_SPEC, "shape", "unsupported height or missing overrides");
    for (size_t i = 0; i < s->override_count; ++i) {
        const struct temp_occupancy *o = &s->overrides[i];
        if (o->depth >= s->height || (o->depth && o->path == NULL))
            return fail(r, TEMP_IMAGE_SPEC, "shape", "override path exceeds height");
        for (size_t j = 0; j < i; ++j)
            if (o->depth == s->overrides[j].depth && prefix(o, s->overrides[j].path, o->depth))
                return fail(r, TEMP_IMAGE_SPEC, "shape", "duplicate override path");
    }
    if (s->height && count_shape(s, path, 0, leaf, branch, out))
        return fail(r, TEMP_IMAGE_SPEC, "shape", "illegal occupancy, nonexistent path or count overflow");
    out->height = s->height;
    uint64_t limit = l->is_lite ? UINT32_MAX : UINT64_MAX;
    if (out->nodes > limit || out->nodes >= offset_max() / l->node_size)
        return fail(r, TEMP_IMAGE_SPEC, "extent", "output exceeds node-index or file-offset range");
    return 0;
}

struct reader {
    int fd;
    const struct temp_layout *layout;
    const struct temp_records *records;
    const struct temp_order *order;
    const struct temp_image_shape *shape;
    struct temp_image_result *result;
    uint32_t ptr, leaf, branch;
    uint64_t blocks, last[MAX_HEIGHT], next[MAX_HEIGHT];
    unsigned char *visited, *previous, *expected_key, *expected_value;
    uint32_t path[MAX_HEIGHT];
};
static int read_at(struct reader *v, uint64_t index, uint64_t within,
                    void *buffer, size_t bytes)
{
    const struct temp_layout *l = v->layout;
    if (index > v->blocks || within > l->node_size || bytes > l->node_size - within)
        return fail(v->result, TEMP_IMAGE_INVALID, "bounds", "node/payload outside file extent");
    uint64_t position = index * l->node_size + within;
    unsigned char *dst = buffer;
    while (bytes) {
        size_t chunk = bytes > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes;
        ssize_t n = pread(v->fd, dst, chunk, (off_t)position);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return fail(v->result, n < 0 ? TEMP_IMAGE_IO : TEMP_IMAGE_INVALID,
                                "read", "%s", n < 0 ? strerror(errno) : "truncated image");
        position += (uint64_t)n; dst += n; bytes -= (size_t)n;
    }
    return 0;
}
static int byte_mismatch(struct reader *v, const char *field,
                          const unsigned char *a, const unsigned char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return fail(v->result, TEMP_IMAGE_INVALID, field,
            "byte %zu: expected %02x, got %02x", i, b[i], a[i]);
    return 0;
}
static int visit(struct reader *v, uint64_t index, uint64_t parent,
                  uint32_t level, unsigned char *minimum)
{
    struct temp_image_result *r = v->result;
    const struct temp_layout *l = v->layout;
    unsigned char metadata[META], *data = NULL, *child_min = NULL;
    uint32_t depth = r->actual.height - 1u - level;
    r->node = index; r->slot = 0;
    if (index == 0 || index > v->blocks)
        return fail(r, TEMP_IMAGE_INVALID, "child-index", "node index outside image");
    if (v->visited[index]) return fail(r, TEMP_IMAGE_INVALID, "ownership", "cycle or repeated child");
    v->visited[index] = 1;
    if (read_at(v, index, 0, metadata, sizeof metadata)) return r->status;
    uint32_t keys = (uint32_t)number(metadata + 4, 4);
    uint32_t maximum = level == 0 ? v->leaf : v->branch;
    uint32_t min = parent == 0 ? 1 : level == 0 ? (v->leaf + 1u) / 2u : v->branch / 2u;
    uint16_t flags = (uint16_t)number(metadata, 2);
    if (!(flags & 1) || !!(flags & 2) != (level == 0) || number(metadata + 2, 2) != level)
        return fail(r, TEMP_IMAGE_INVALID, "flags-level", "invalid flags or node level");
    if (keys < min || keys > maximum)
        return fail(r, TEMP_IMAGE_INVALID, "occupancy", "keys %u outside [%u,%u]", keys, min, maximum);
    if (number(metadata + 12, v->ptr) != parent)
        return fail(r, TEMP_IMAGE_INVALID, "parent", "incorrect parent index");
    if (number(metadata + 12 + 2u * v->ptr, v->ptr) != v->last[level] ||
        (v->last[level] && v->next[level] != index))
        return fail(r, TEMP_IMAGE_INVALID, "siblings", "sibling chain disagrees with child traversal");
    v->last[level] = index;
    v->next[level] = number(metadata + 12 + v->ptr, v->ptr);
    if (v->shape && keys != shape_keys(v->shape, v->path, depth, maximum))
        return fail(r, TEMP_IMAGE_INVALID, "shape", "node occupancy differs from exact fixture");
    size_t key_bytes = (size_t)keys * l->key_size;
    size_t data_bytes = key_bytes + (level ? ((size_t)keys + 1) * v->ptr : (size_t)keys * l->value_size);
    data = malloc(data_bytes);
    child_min = malloc(l->key_size);
    if (data == NULL || child_min == NULL) {
        fail(r, TEMP_IMAGE_RESOURCE, "allocation", "cannot allocate node payload"); goto done;
    }
    if (read_at(v, index, META, data, data_bytes)) goto done;
    ++r->actual.nodes;
    if (level == 0) {
        ++r->actual.leaves;
        memcpy(minimum, data, l->key_size);
        for (uint32_t i = 0; i < keys; ++i) {
            unsigned char *key = data + (size_t)i * l->key_size;
            unsigned char *value = data + key_bytes + (size_t)i * l->value_size;
            r->node = index; r->slot = i; r->record = r->actual.records;
            if (r->actual.records && v->order->compare(v->previous, key, v->order->context) >= 0) {
                fail(r, TEMP_IMAGE_INVALID, "record-order", "unordered or comparator-equivalent records"); goto done;
            }
            if (v->records) {
                if (r->actual.records >= v->records->count) {
                    fail(r, TEMP_IMAGE_INVALID, "record-count", "extra leaf record"); goto done;
                }
                if (v->records->get(v->records->context, r->actual.records, v->expected_key, v->expected_value)) {
                    fail(r, TEMP_IMAGE_SPEC, "record-source", "cannot obtain expected record"); goto done;
                }
                if (byte_mismatch(v, "record-key", key, v->expected_key, l->key_size) ||
                    byte_mismatch(v, "record-value", value, v->expected_value, l->value_size)) goto done;
            }
            memcpy(v->previous, key, l->key_size);
            ++r->actual.records;
        }
    } else {
        for (uint32_t i = 0; i <= keys; ++i) {
            uint64_t child = number(data + key_bytes + (size_t)i * v->ptr, v->ptr);
            v->path[depth] = i;
            if (visit(v, child, index, level - 1, child_min)) goto done;
            r->node = index; r->slot = i;
            if (i == 0) memcpy(minimum, child_min, l->key_size);
            else if (v->order->compare(data + (size_t)(i - 1) * l->key_size,
                                       child_min, v->order->context) != 0) {
                fail(r, TEMP_IMAGE_INVALID, "separator", "separator differs from right-subtree minimum"); goto done;
            }
        }
    }
done:
    free(data); free(child_min);
    return r->status;
}

int temp_image_verify(const char *path, const struct temp_layout *l,
                      const struct temp_records *records, const struct temp_order *order,
                      const struct temp_image_shape *shape,
                      const struct temp_image_constraints *constraints,
                      struct temp_image_result *r)
{
    struct reader v = {0};
    struct stat st;
    unsigned char header[META], *minimum = NULL;
    uint64_t expected_records, expected_nodes, free_count, free_head, root;
    memset(r, 0, sizeof *r);
    v.fd = -1; v.layout = l; v.records = records; v.order = order; v.shape = shape; v.result = r;
    v.ptr = l->is_lite ? 4 : 8;
    if (bounds(l, &v.leaf, &v.branch, r)) return r->status;
    if (order == NULL || order->compare == NULL || (records && records->get == NULL))
        return fail(r, TEMP_IMAGE_SPEC, "callbacks", "missing comparator or record source");
    if (shape) {
        struct temp_image_stats planned;
        if (temp_image_plan(l, shape, &planned, r)) return r->status;
    }
    v.fd = open(path, O_RDONLY);
    if (v.fd < 0) return fail(r, TEMP_IMAGE_IO, "open", "%s", strerror(errno));
    if (fstat(v.fd, &st) != 0) { fail(r, TEMP_IMAGE_IO, "stat", "%s", strerror(errno)); goto done; }
    if (!S_ISREG(st.st_mode) || st.st_size < (off_t)l->node_size || st.st_size % l->node_size) {
        fail(r, TEMP_IMAGE_INVALID, "extent", "image must contain complete header/node blocks"); goto done;
    }
    v.blocks = (uint64_t)st.st_size / l->node_size - 1;
    if (v.blocks > (l->is_lite ? UINT32_MAX : UINT64_MAX) || v.blocks >= SIZE_MAX) {
        fail(r, TEMP_IMAGE_RESOURCE, "extent", "image exceeds addressable node indices"); goto done;
    }
    if (read_at(&v, 0, 0, header, sizeof header)) goto done;
    if (memcmp(header, "BPTR", 4) || number(header + 4, 4) != (l->is_lite ? 0x80u : 0u) ||
        number(header + 8, 4) != l->node_size || number(header + 12, 2) != l->key_size ||
        number(header + 14, 2) != l->value_size) {
        fail(r, TEMP_IMAGE_INVALID, "header-layout", "magic, version or layout mismatch"); goto done;
    }
    expected_records = number(header + 16, 8);
    r->actual.height = (uint32_t)number(header + 24, 4);
    root = number(header + 28, v.ptr);
    free_head = number(header + 28 + v.ptr, v.ptr);
    free_count = number(header + 28 + 2u * v.ptr, v.ptr);
    expected_nodes = number(header + 28 + 3u * v.ptr, v.ptr);
    if (r->actual.height > MAX_HEIGHT || expected_nodes > v.blocks || free_count > v.blocks) {
        fail(r, TEMP_IMAGE_INVALID, "header-count", "height or node count exceeds limits"); goto done;
    }
    if ((r->actual.height == 0) != (root == 0) ||
        (!root && (expected_records != 0 || expected_nodes != 0))) {
        fail(r, TEMP_IMAGE_INVALID, "empty-state", "inconsistent rootless empty image"); goto done;
    }
    if (shape && r->actual.height != shape->height) {
        fail(r, TEMP_IMAGE_INVALID, "shape-height", "exact fixture height mismatch"); goto done;
    }
    v.visited = calloc((size_t)v.blocks + 1, 1);
    v.previous = malloc(l->key_size); minimum = malloc(l->key_size);
    v.expected_key = malloc(l->key_size); v.expected_value = malloc(l->value_size);
    if (!v.visited || !v.previous || !minimum || !v.expected_key || !v.expected_value) {
        fail(r, TEMP_IMAGE_RESOURCE, "allocation", "cannot allocate traversal state"); goto done;
    }
    if (root && visit(&v, root, 0, r->actual.height - 1, minimum)) goto done;
    for (uint32_t level = 0; level < r->actual.height; ++level)
        if (v.next[level]) { fail(r, TEMP_IMAGE_INVALID, "siblings", "nonzero final sibling link"); goto done; }
    while (free_head) {
        r->node = free_head;
        if (free_head > v.blocks || v.visited[free_head]) {
            fail(r, TEMP_IMAGE_INVALID, "free-list", "cycle, repeated or live free node"); goto done;
        }
        v.visited[free_head] = 2;
        if (read_at(&v, free_head, 0, header, 2u + v.ptr)) goto done;
        if (number(header, 2) & 1) { fail(r, TEMP_IMAGE_INVALID, "free-list", "valid node on free list"); goto done; }
        free_head = number(header + 2, v.ptr);
        ++r->actual.free_nodes;
    }
    for (uint64_t i = 1; i <= v.blocks; ++i)
        if (!v.visited[i]) { r->node = i; fail(r, TEMP_IMAGE_INVALID, "reachability", "orphan block outside tree and free list"); goto done; }
    if (r->actual.nodes != expected_nodes || r->actual.free_nodes != free_count ||
        r->actual.records != expected_records || (records && r->actual.records != records->count)) {
        fail(r, TEMP_IMAGE_INVALID, "totals", "observed records=%" PRIu64 " nodes=%" PRIu64 " free=%" PRIu64
             "; header records=%" PRIu64 " nodes=%" PRIu64 " free=%" PRIu64,
             r->actual.records, r->actual.nodes, r->actual.free_nodes,
             expected_records, expected_nodes, free_count); goto done;
    }
    if (constraints && (((constraints->mask & TEMP_EXPECT_HEIGHT) && constraints->height != r->actual.height) ||
        ((constraints->mask & TEMP_EXPECT_NODES) && constraints->nodes != r->actual.nodes) ||
        ((constraints->mask & TEMP_EXPECT_LEAVES) && constraints->leaves != r->actual.leaves)))
        fail(r, TEMP_IMAGE_INVALID, "expected-totals", "optional topology expectation mismatch");
done:
    free(v.visited); free(v.previous); free(v.expected_key); free(v.expected_value); free(minimum);
    if (v.fd >= 0 && close(v.fd) && r->status == 0) fail(r, TEMP_IMAGE_IO, "close", "%s", strerror(errno));
    return r->status;
}

struct writer {
    int fd;
    const struct temp_layout *layout;
    const struct temp_image_shape *shape;
    const struct temp_records *records;
    struct temp_image_result *result;
    uint32_t leaf, branch, ptr, path[MAX_HEIGHT];
    uint64_t next_index, cursor, last[MAX_HEIGHT];
};
static int write_at(struct writer *w, uint64_t index, uint64_t within,
                     const void *buffer, size_t bytes)
{
    uint64_t position = index * w->layout->node_size + within;
    const unsigned char *src = buffer;
    while (bytes) {
        size_t chunk = bytes > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : bytes;
        ssize_t n = pwrite(w->fd, src, chunk, (off_t)position);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return fail(w->result, TEMP_IMAGE_IO, "write", "%s", n < 0 ? strerror(errno) : "zero-length write");
        src += n; bytes -= (size_t)n; position += (uint64_t)n;
    }
    return 0;
}
static int build(struct writer *w, uint64_t parent, uint32_t depth,
                  uint64_t *index, unsigned char *minimum)
{
    const struct temp_layout *l = w->layout;
    struct temp_image_result *r = w->result;
    uint32_t level = w->shape->height - depth - 1;
    uint32_t keys = shape_keys(w->shape, w->path, depth, level ? w->branch : w->leaf);
    unsigned char *block = calloc(1, l->node_size), *child_min = malloc(l->key_size);
    unsigned char pointer[8];
    *index = w->next_index++;
    if (block == NULL || child_min == NULL) { fail(r, TEMP_IMAGE_RESOURCE, "allocation", "cannot allocate output node"); goto done; }
    put(block, level ? 1 : 3, 2); put(block + 2, level, 2); put(block + 4, keys, 4);
    put(block + 12, parent, w->ptr);
    put(block + 12 + 2u * w->ptr, w->last[level], w->ptr);
    if (w->last[level]) {
        put(pointer, *index, w->ptr);
        if (write_at(w, w->last[level], 12 + w->ptr, pointer, w->ptr)) goto done;
    }
    w->last[level] = *index;
    size_t key_bytes = (size_t)keys * l->key_size;
    if (level == 0) {
        for (uint32_t i = 0; i < keys; ++i)
            if (w->records->get(w->records->context, w->cursor++, block + META + (size_t)i * l->key_size,
                                block + META + key_bytes + (size_t)i * l->value_size)) {
                fail(r, TEMP_IMAGE_SPEC, "record-source", "cannot obtain generation record"); goto done;
            }
        memcpy(minimum, block + META, l->key_size);
    } else {
        for (uint32_t i = 0; i <= keys; ++i) {
            uint64_t child;
            w->path[depth] = i;
            if (build(w, *index, depth + 1, &child, child_min)) goto done;
            put(block + META + key_bytes + (size_t)i * w->ptr, child, w->ptr);
            if (i == 0) memcpy(minimum, child_min, l->key_size);
            else memcpy(block + META + (size_t)(i - 1) * l->key_size, child_min, l->key_size);
        }
    }
    write_at(w, *index, 0, block, l->node_size);
done:
    free(block); free(child_min);
    return r->status;
}
static int parents(const char *path, struct temp_image_result *r)
{
    char *copy = strdup(path);
    if (!copy) return fail(r, TEMP_IMAGE_RESOURCE, "allocation", "cannot allocate output path");
    for (char *p = copy + 1; *p; ++p) if (*p == '/') {
        struct stat st;
        *p = '\0';
        if ((mkdir(copy, 0755) != 0 && errno != EEXIST) || stat(copy, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fail(r, TEMP_IMAGE_IO, "directory", "cannot create parent directory %s", copy); free(copy); return r->status;
        }
        *p = '/';
    }
    free(copy); return 0;
}
int temp_image_generate(const char *path, const struct temp_layout *l,
                        const struct temp_image_shape *shape,
                        const struct temp_records *records, const struct temp_order *order,
                        struct temp_image_result *r)
{
    struct temp_image_stats planned;
    struct writer w = {0};
    unsigned char *buffer = NULL, *key, *value, *previous;
    int created = 0;
    if (temp_image_plan(l, shape, &planned, r)) return r->status;
    if (path == NULL || path[0] == '\0')
        return fail(r, TEMP_IMAGE_SPEC, "path", "missing output path");
    if (!records || !records->get || !order || !order->compare || records->count != planned.records)
        return fail(r, TEMP_IMAGE_SPEC, "records", "record source/count does not match requested shape");
    uint64_t buffer_size = (uint64_t)l->node_size + 2u * l->key_size + l->value_size;
    if (buffer_size > SIZE_MAX)
        return fail(r, TEMP_IMAGE_RESOURCE, "allocation", "generation buffers exceed address space");
    buffer = calloc(1, (size_t)buffer_size);
    if (!buffer) return fail(r, TEMP_IMAGE_RESOURCE, "allocation", "cannot allocate generation buffers");
    key = buffer + l->node_size; previous = key + l->key_size; value = previous + l->key_size;
    for (uint64_t i = 0; i < records->count; ++i) {
        r->record = i;
        if (records->get(records->context, i, key, value) ||
            (i && order->compare(previous, key, order->context) >= 0)) {
            fail(r, TEMP_IMAGE_SPEC, "record-order", "source is unreadable, unordered or comparator-equivalent"); goto done;
        }
        memcpy(previous, key, l->key_size);
    }
    if (parents(path, r)) goto done;
    w.fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (w.fd < 0) {
        if (errno == EEXIST) {
            temp_image_verify(path, l, records, order, shape, NULL, r);
            r->reused = r->status == 0;
        } else fail(r, TEMP_IMAGE_IO, "create", "%s", strerror(errno));
        goto done;
    }
    created = 1; w.layout = l; w.shape = shape; w.records = records; w.result = r;
    w.ptr = l->is_lite ? 4 : 8; w.next_index = 1;
    bounds(l, &w.leaf, &w.branch, r);
    uint64_t root = 0;
    if (shape->height && build(&w, 0, 0, &root, key)) goto close_output;
    memcpy(buffer, "BPTR", 4); put(buffer + 4, l->is_lite ? 0x80 : 0, 4);
    put(buffer + 8, l->node_size, 4); put(buffer + 12, l->key_size, 2); put(buffer + 14, l->value_size, 2);
    put(buffer + 16, planned.records, 8); put(buffer + 24, shape->height, 4);
    put(buffer + 28, root, w.ptr); put(buffer + 28 + 3u * w.ptr, planned.nodes, w.ptr);
    write_at(&w, 0, 0, buffer, l->node_size);
close_output:
    if (close(w.fd) && r->status == 0) fail(r, TEMP_IMAGE_IO, "close", "%s", strerror(errno));
    if (!r->status) temp_image_verify(path, l, records, order, shape, NULL, r);
done:
    free(buffer);
    if (created && r->status) unlink(path);
    return r->status;
}
