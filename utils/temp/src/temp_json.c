#define _POSIX_C_SOURCE 200809L
#include "temp_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct parser { const unsigned char *p, *end, *start; char *error; size_t size; };
static void bad(struct parser *p, const char *reason)
{
    if (!p->error[0]) snprintf(p->error, p->size, "JSON byte %zu: %s", (size_t)(p->p - p->start), reason);
}
static void space(struct parser *p)
{
    while (p->p < p->end && (*p->p == ' ' || *p->p == '\n' || *p->p == '\r' || *p->p == '\t')) ++p->p;
}
static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int codepoint(struct parser *p, unsigned *value)
{
    *value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (p->p == p->end || hex(*p->p) < 0) return -1;
        *value = *value * 16 + (unsigned)hex(*p->p++);
    }
    return 0;
}
static char *string(struct parser *p)
{
    if (p->p == p->end || *p->p++ != '"') { bad(p, "expected string"); return NULL; }
    char *out = malloc((size_t)(p->end - p->p) + 1), *at = out;
    if (!out) { bad(p, "out of memory"); return NULL; }
    while (p->p < p->end) {
        unsigned c = *p->p++;
        if (c == '"') { *at = 0; return out; }
        if (c < 32) break;
        if (c == '\\') {
            if (p->p == p->end) break;
            c = *p->p++;
            if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
            else if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
            else if (c == 'u') {
                if (codepoint(p, &c)) break;
                if (c >= 0xd800 && c <= 0xdbff) {
                    unsigned low;
                    if (p->end - p->p < 2 || p->p[0] != '\\' || p->p[1] != 'u') break;
                    p->p += 2;
                    if (codepoint(p, &low) || low < 0xdc00 || low > 0xdfff) break;
                    c = 0x10000 + (c - 0xd800) * 1024 + low - 0xdc00;
                } else if (c >= 0xdc00 && c <= 0xdfff) break;
                if (c == 0) break;
                if (c < 128) *at++ = (char)c;
                else if (c < 2048) { *at++ = (char)(0xc0 | (c >> 6)); *at++ = (char)(0x80 | (c & 63)); }
                else if (c < 65536) { *at++ = (char)(0xe0 | (c >> 12)); *at++ = (char)(0x80 | ((c >> 6) & 63)); *at++ = (char)(0x80 | (c & 63)); }
                else { *at++ = (char)(0xf0 | (c >> 18)); *at++ = (char)(0x80 | ((c >> 12) & 63)); *at++ = (char)(0x80 | ((c >> 6) & 63)); *at++ = (char)(0x80 | (c & 63)); }
                continue;
            } else if (c != '"' && c != '\\' && c != '/') break;
        } else if (c >= 128) {
            unsigned count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
            if (!count || p->end - p->p < (ptrdiff_t)count) break;
            unsigned cp = c & ((1u << (6 - count)) - 1u);
            unsigned original = count;
            for (unsigned i = 0; i < count; ++i) {
                if ((p->p[i] & 0xc0) != 0x80) { count = 0; break; }
                cp = (cp << 6) | (p->p[i] & 63);
            }
            if (!count || (original == 1 && cp < 128) || (original == 2 && cp < 2048) ||
                (original == 3 && cp < 65536) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) break;
            *at++ = (char)c;
            while (count--) *at++ = (char)*p->p++;
            continue;
        }
        *at++ = (char)c;
    }
    free(out); bad(p, "invalid or unterminated string"); return NULL;
}
void temp_json_free(struct temp_json *j)
{
    while (j) { struct temp_json *next = j->next; temp_json_free(j->child); free(j->name); free(j->text); free(j); j = next; }
}
static struct temp_json *value(struct parser *p, unsigned depth)
{
    space(p);
    if (p->p == p->end || depth > 64) { bad(p, "missing value or excessive nesting"); return NULL; }
    struct temp_json *j = calloc(1, sizeof *j);
    if (!j) { bad(p, "out of memory"); return NULL; }
    unsigned char c = *p->p;
    if (c == '{' || c == '[') {
        j->type = c == '{' ? 'o' : 'a'; ++p->p; space(p);
        unsigned char end = c == '{' ? '}' : ']';
        struct temp_json **tail = &j->child;
        if (p->p < p->end && *p->p == end) { ++p->p; return j; }
        for (;;) {
            char *name = NULL;
            if (j->type == 'o') {
                name = string(p);
                if (!name) goto fail;
                for (const struct temp_json *old = j->child; old; old = old->next)
                    if (!strcmp(old->name, name)) { free(name); bad(p, "duplicate object key"); goto fail; }
                space(p);
                if (p->p == p->end || *p->p++ != ':') { free(name); bad(p, "missing colon"); goto fail; }
            }
            struct temp_json *child = value(p, depth + 1);
            if (!child) { free(name); goto fail; }
            child->name = name; *tail = child; tail = &child->next;
            space(p);
            if (p->p < p->end && *p->p == end) { ++p->p; return j; }
            if (p->p == p->end || *p->p++ != ',') { bad(p, "missing comma or closing bracket"); goto fail; }
            space(p);
        }
    }
    if (c == '"') { j->type = 's'; j->text = string(p); if (!j->text) goto fail; return j; }
    const char *literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
    if (literal) {
        size_t length = strlen(literal);
        if ((size_t)(p->end - p->p) < length || memcmp(p->p, literal, length)) { bad(p, "invalid literal"); goto fail; }
        p->p += length; j->type = c == 'n' ? 'z' : (char)c; return j;
    }
    const unsigned char *start = p->p;
    if (*p->p == '-') ++p->p;
    if (p->p == p->end) goto number_fail;
    if (*p->p == '0') ++p->p;
    else {
        if (*p->p < '1' || *p->p > '9') goto number_fail;
        while (p->p < p->end && *p->p >= '0' && *p->p <= '9') ++p->p;
    }
    if (p->p < p->end && *p->p == '.') {
        ++p->p; const unsigned char *digits = p->p;
        while (p->p < p->end && *p->p >= '0' && *p->p <= '9') ++p->p;
        if (p->p == digits) goto number_fail;
    }
    if (p->p < p->end && (*p->p == 'e' || *p->p == 'E')) {
        ++p->p;
        if (p->p < p->end && (*p->p == '-' || *p->p == '+')) ++p->p;
        const unsigned char *digits = p->p;
        while (p->p < p->end && *p->p >= '0' && *p->p <= '9') ++p->p;
        if (p->p == digits) goto number_fail;
    }
    j->type = 'n'; j->text = malloc((size_t)(p->p - start) + 1);
    if (!j->text) { bad(p, "out of memory"); goto fail; }
    memcpy(j->text, start, (size_t)(p->p - start)); j->text[p->p - start] = 0;
    return j;
number_fail:
    bad(p, "invalid JSON value");
fail:
    temp_json_free(j); return NULL;
}
const struct temp_json *temp_json_get(const struct temp_json *j, const char *name)
{
    if (j && j->type == 'o') for (j = j->child; j; j = j->next) if (!strcmp(j->name, name)) return j;
    return NULL;
}
struct temp_json *temp_json_read(const char *path, char *error, size_t size)
{
    FILE *file = fopen(path, "rb");
    unsigned char *buffer = NULL;
    struct temp_json *out = NULL;
    error[0] = 0;
    if (!file) { snprintf(error, size, "cannot read manifest: %s", strerror(errno)); return NULL; }
    size_t capacity = 4096, length = 0;
    buffer = malloc(capacity);
    if (!buffer) goto allocation;
    for (;;) {
        size_t n = fread(buffer + length, 1, capacity - length, file);
        length += n;
        if (ferror(file)) { snprintf(error, size, "cannot read manifest: %s", strerror(errno)); goto done; }
        if (feof(file)) break;
        if (capacity == 16u * 1024u * 1024u) { snprintf(error, size, "manifest exceeds 16 MiB"); goto done; }
        capacity *= 2;
        unsigned char *grown = realloc(buffer, capacity);
        if (!grown) goto allocation;
        buffer = grown;
    }
    struct parser p = {buffer, buffer + length, buffer, error, size};
    out = value(&p, 0); space(&p);
    if (out && p.p != p.end) { bad(&p, "trailing bytes"); temp_json_free(out); out = NULL; }
    goto done;
allocation:
    snprintf(error, size, "cannot allocate manifest buffer");
done:
    free(buffer);
    if (fclose(file) && out) { temp_json_free(out); out = NULL; snprintf(error, size, "cannot close manifest"); }
    return out;
}
