#include "temp_full.h"
#include "bptr_internal.h"
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHAPE_MAX_DEPTH 253u
struct temp_shape_node {
   uint32_t child, keys;
   _Bool has_keys;
   struct temp_shape_node *children, *next;
};

static void free_node(struct temp_shape_node *node)
{
   while (node != NULL) {
      struct temp_shape_node *next = node->next;
      free_node(node->children);
      free(node);
      node = next;
   }
}
void temp_shape_free(struct temp_shape *shape)
{
   if (shape == NULL) return;
   free_node(shape->root);
   memset(shape, 0, sizeof *shape);
}

static int decimal(const char **at, uint32_t *value)
{
   const char *p = *at;
   uint32_t n = 0;
   if (*p < '0' || *p > '9') return 0;
   do {
      unsigned int digit = (unsigned int)(*p - '0');
      if (n > (UINT32_MAX - digit) / 10u) return 0;
      n = n * 10u + digit;
      p++;
   } while (*p >= '0' && *p <= '9');
   *value = n;
   *at = p;
   return 1;
}

int temp_shape_add(struct temp_shape *shape, const char *spec)
{
   uint32_t path[SHAPE_MAX_DEPTH], count;
   size_t depth = 0;
   const char *p = spec;
   struct temp_shape_node *node;
   if (shape == NULL || spec == NULL) return 1;
   if (!strncmp(p, "root=", 5)) p += 4;
   else {
      do {
         if (depth == SHAPE_MAX_DEPTH || !decimal(&p, &path[depth])) return 1;
         depth++;
         if (*p != '.') break;
         p++;
      } while (1);
   }
   if (*p++ != '=' || !decimal(&p, &count) || *p != '\0') return 1;
   if (shape->root == NULL) {
      shape->root = calloc(1, sizeof *shape->root);
      if (shape->root == NULL) return 1;
   }
   node = shape->root;
   for (size_t i = 0; i < depth; i++) {
      struct temp_shape_node **child = &node->children;
      while (*child != NULL && (*child)->child < path[i]) child = &(*child)->next;
      if (*child == NULL || (*child)->child != path[i]) {
         struct temp_shape_node *created = calloc(1, sizeof *created);
         if (created == NULL) return 1;
         created->child = path[i];
         created->next = *child;
         *child = created;
      }
      node = *child;
   }
   if (node->has_keys) return 1;
   node->keys = count;
   node->has_keys = 1;
   shape->override_count++;
   return 0;
}

const struct temp_shape_node *temp_shape_child(const struct temp_shape_node *node,
                                               uint32_t child)
{
   if (node == NULL) return NULL;
   node = node->children;
   while (node != NULL && node->child < child) node = node->next;
   return node != NULL && node->child == child ? node : NULL;
}
uint32_t temp_shape_keys(const struct temp_shape_node *node, uint32_t full_count)
{
   return node != NULL && node->has_keys ? node->keys : full_count;
}

static int add_scaled(uint64_t *dst, uint64_t value, uint64_t factor)
{
   if (factor != 0 && value > (UINT64_MAX - *dst) / factor) return 0;
   *dst += value * factor;
   return 1;
}
static int full_count(unsigned int levels, uint32_t leaf, uint32_t branch,
                       struct temp_shape_counts *out)
{
   out->records = leaf; out->leaves = 1; out->nodes = 1;
   for (unsigned int i = 1; i < levels; i++) {
      uint64_t fanout = (uint64_t)branch + 1u;
      if (out->records > UINT64_MAX / fanout ||
          out->leaves > UINT64_MAX / fanout ||
          out->nodes > (UINT64_MAX - 1u) / fanout) return TEMP_FULL_E_PTR_OVERFLOW;
      out->records *= fanout;
      out->leaves *= fanout;
      out->nodes = out->nodes * fanout + 1u;
   }
   return TEMP_FULL_OK;
}
static int count_node(const struct temp_shape_node *node, unsigned int levels,
                       _Bool root, uint32_t leaf, uint32_t branch,
                       struct temp_shape_counts *out)
{
   uint32_t full = levels == 1 ? leaf : branch;
   uint32_t keys = temp_shape_keys(node, full);
   uint32_t min = root ? 1u : (levels == 1 ? (leaf + 1u) / 2u : branch / 2u);
   const struct temp_shape_node *child = node == NULL ? NULL : node->children;
   struct temp_shape_counts sub, unchanged;
   uint64_t explicit_children = 0;
   int status;
   if (keys < min || keys > full) return TEMP_FULL_E_SHAPE;
   if (levels == 1) {
      if (child != NULL) return TEMP_FULL_E_SHAPE;
      out->records = keys; out->leaves = 1; out->nodes = 1;
      return TEMP_FULL_OK;
   }
   memset(out, 0, sizeof *out);
   out->nodes = 1;
   for (; child != NULL; child = child->next) {
      if (child->child > keys) return TEMP_FULL_E_SHAPE;
      status = count_node(child, levels - 1u, 0, leaf, branch, &sub);
      if (status != TEMP_FULL_OK) return status;
      if (!add_scaled(&out->records, sub.records, 1) ||
          !add_scaled(&out->leaves, sub.leaves, 1) ||
          !add_scaled(&out->nodes, sub.nodes, 1)) return TEMP_FULL_E_PTR_OVERFLOW;
      explicit_children++;
   }
   if ((uint64_t)keys + 1u > explicit_children) {
      status = full_count(levels - 1u, leaf, branch, &unchanged);
      if (status != TEMP_FULL_OK) return status;
      uint64_t rest = (uint64_t)keys + 1u - explicit_children;
      if (!add_scaled(&out->records, unchanged.records, rest) ||
          !add_scaled(&out->leaves, unchanged.leaves, rest) ||
          !add_scaled(&out->nodes, unchanged.nodes, rest)) return TEMP_FULL_E_PTR_OVERFLOW;
   }
   return TEMP_FULL_OK;
}
int temp_shape_count(const struct temp_shape *shape, unsigned int levels,
                     _Bool is_lite, uint32_t node_size,
                     struct temp_shape_counts *counts)
{
   uint64_t ptr = is_lite ? BPTR_LITE_PTR_BYTE : BPTR_NORM_PTR_BYTE;
   if (levels == 0 || levels > 254u) return TEMP_FULL_E_LAY_CNT;
   if (node_size < BPTR_NODE_METADATA_BYTE + ptr + 2u * (sizeof (int64_t) + ptr))
      return TEMP_FULL_E_NODE_SIZE;
   uint64_t remaining = (uint64_t)node_size - BPTR_NODE_METADATA_BYTE;
   uint32_t leaf = remaining / (2u * sizeof (int64_t));
   uint32_t branch = (remaining - ptr) / (sizeof (int64_t) + ptr);
   return count_node(shape == NULL ? NULL : shape->root, levels, 1, leaf, branch,
                     counts);
}

struct path_writer { char *buf; size_t size, used; int failed; };
static void append(struct path_writer *w, const char *fmt, ...)
{
   va_list args;
   int count;
   if (w->failed) return;
   va_start(args, fmt);
   count = vsnprintf(w->used < w->size ? w->buf + w->used : NULL,
                     w->used < w->size ? w->size - w->used : 0, fmt, args);
   va_end(args);
   if (count < 0 || w->used > (size_t)INT_MAX - (size_t)count) w->failed = 1;
   else w->used += (size_t)count;
}
static void write_overrides(struct path_writer *w, const struct temp_shape_node *n,
                             uint32_t *path, size_t depth, _Bool *first)
{
   if (n == NULL) return;
   if (n->has_keys) {
      append(w, "%s", *first ? "" : ",");
      *first = 0;
      if (depth == 0) append(w, "root");
      for (size_t i = 0; i < depth; i++) append(w, "%s%" PRIu32, i ? "." : "", path[i]);
      append(w, "=%" PRIu32, n->keys);
   }
   for (const struct temp_shape_node *c = n->children; c != NULL; c = c->next) {
      path[depth] = c->child;
      write_overrides(w, c, path, depth + 1u, first);
   }
}
int temp_path(char *buf, size_t size, const char *dir, unsigned int levels,
               int64_t st, int64_t interval, const struct temp_shape *shape)
{
   struct path_writer w = {buf, size, 0, 0};
   uint32_t path[SHAPE_MAX_DEPTH];
   size_t len = strlen(dir);
   _Bool first = 1;
   append(&w, "%s%s%u-%" PRIi64 "-%" PRIi64, dir,
           len && dir[len - 1] == '/' ? "" : "/", levels, st, interval);
   if (shape != NULL && shape->override_count != 0) {
      append(&w, "--keys-");
      write_overrides(&w, shape->root, path, 0, &first);
   }
   append(&w, ".bptr");
   return w.failed ? -1 : (int)w.used;
}

static int signed_number(const char **at, int64_t *out)
{
   const char *p = *at;
   _Bool negative = *p == '-';
   uint64_t value = 0, limit = negative ? UINT64_C(1) << 63 : INT64_MAX;
   if (negative) p++;
   if (*p < '0' || *p > '9') return 0;
   do {
      unsigned digit = (unsigned)(*p - '0');
      if (value > (limit - digit) / 10u) return 0;
      value = value * 10u + digit;
      p++;
   } while (*p >= '0' && *p <= '9');
   *out = negative ? (int64_t)(~value + 1u) : (int64_t)value;
   *at = p;
   return 1;
}
int temp_parse_name(const char *path, unsigned int *levels, int64_t *st,
                     int64_t *interval, struct temp_shape *shape)
{
   const char *p = strrchr(path, '/');
   uint32_t height;
   struct temp_shape parsed = {0};
   p = p == NULL ? path : p + 1;
   if (!decimal(&p, &height) || height == 0 || *p++ != '-' || !signed_number(&p, st) ||
       *p++ != '-' || !signed_number(&p, interval)) return 0;
   if (!strncmp(p, "--keys-", 7)) {
      p += 7;
      do {
         const char *start = p, *end = strchr(p, '=');
         char *spec;
         if (end == NULL) goto invalid;
         end++;
         while (*end >= '0' && *end <= '9') end++;
         size_t len = (size_t)(end - start);
         spec = malloc(len + 1u);
         if (spec == NULL) goto invalid;
         memcpy(spec, start, len); spec[len] = '\0';
         int bad = temp_shape_add(&parsed, spec);
         free(spec);
         if (bad) goto invalid;
         p = end;
         if (*p != ',') break;
         p++;
      } while (1);
   }
   if (strcmp(p, ".bptr")) goto invalid;
   *levels = height;
   *shape = parsed;
   return 1;
invalid:
   temp_shape_free(&parsed);
   return 0;
}
