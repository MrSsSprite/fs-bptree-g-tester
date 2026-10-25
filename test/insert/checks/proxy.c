/* Test-owned adversaries around public calls; no target source is inspected. */
#define _POSIX_C_SOURCE 200809L
#include "harness.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static struct bptr *pending_tree;
static uint_fast64_t saved_count;
static bptr_node_t saved_leaf;
static unsigned char saved_value;
static _Bool persisted_mutation;

static _Bool mode_is(const char *name)
{
   const char *mode = getenv("INSERT_CHECK_MODE");
   return mode != NULL && strcmp(mode, name) == 0;
}

static void require(_Bool condition, const char *detail)
{
   if (!condition)
    {
      fprintf(stderr, "insert check proxy: %s\n", detail);
      exit(97);
    }
}

static struct bptr_node *first_leaf(struct bptr *tree)
{
   bptr_node_t index = tree->root_idx;
   for (size_t depth = 0; depth < tree->height; depth++)
    {
      struct bptr_node *node = bptr_node_fetch(tree, index);
      require(node != NULL, "fetch first leaf");
      if (node->is_leaf) return node;
      if (tree->is_lite)
       {
         uint32_t child;
         memcpy(&child, node->vals, sizeof child);
         index = child;
       }
      else
       {
         uint64_t child;
         memcpy(&child, node->vals, sizeof child);
         index = child;
       }
      bptr_node_unload(tree, node);
    }
   require(0, "first leaf traversal exceeded height");
   return NULL;
}

int checked_bptr_insert(struct bptr *tree, const void *key, const void *value)
{
   if (mode_is("success-noop")) return BPTR_E_SUCCESS;
   int status = bptr_insert(tree, key, value);
   if (mode_is("errno-zero")) bptr_errno = 0;
   if (mode_is("errno-stale")) bptr_errno = 0x715;
   if (mode_is("wrong-return") && status == BPTR_E_SUCCESS) return BPTR_E_TODO;
   if (mode_is("duplicate-success") && status == BPTR_E_KEY_EXIST)
      return BPTR_E_SUCCESS;
   if (status != BPTR_E_SUCCESS) return status;
   if (mode_is("live-count"))
    {
      require(pending_tree == NULL, "unconsumed live-count adversary");
      pending_tree = tree;
      saved_count = tree->record_cnt;
      tree->record_cnt++;
    }
   if (mode_is("live-value"))
    {
      require(pending_tree == NULL, "unconsumed live-value adversary");
      struct bptr_node *leaf = first_leaf(tree);
      require(leaf->key_count != 0 && leaf->vals != NULL,
               "first leaf has no record to mutate");
      pending_tree = tree;
      saved_leaf = leaf->node_idx;
      saved_value = *(unsigned char *)leaf->vals;
      *(unsigned char *)leaf->vals ^= 0x80;
      leaf->is_dirty = 1;
      bptr_node_unload(tree, leaf);
    }
   if (mode_is("caller-write"))
      *(unsigned char *)(uintptr_t)value ^= 0x80;
   return status;
}

static void read_at(FILE *file, uint64_t offset, void *out, size_t count)
{
   off_t position = (off_t)offset;
   require(position >= 0 && (uint64_t)position == offset,
            "unrepresentable image offset");
   require(fseeko(file, position, SEEK_SET) == 0 &&
            fread(out, 1, count, file) == count, "read persisted image");
}

static uint64_t read_pointer(FILE *file, uint64_t offset, _Bool lite)
{
   if (lite)
    {
      uint32_t stored;
      read_at(file, offset, &stored, sizeof stored);
      return stored;
    }
   uint64_t stored;
   read_at(file, offset, &stored, sizeof stored);
   return stored;
}

/* Alter only persisted payload after every live assertion has already passed.
 * These offsets come from FORMAT.md, independently of core decoding. */
static void corrupt_persisted_value(const char *path)
{
   FILE *file = fopen(path, "r+b");
   uint32_t version, size, height;
   uint16_t key_size;
   require(file != NULL, "open persisted image");
   read_at(file, 4, &version, sizeof version);
   read_at(file, 8, &size, sizeof size);
   read_at(file, 12, &key_size, sizeof key_size);
   read_at(file, 24, &height, sizeof height);
   _Bool lite = !!(version & 0x80);
   uint64_t index = read_pointer(file, 28, lite);
   require(index != 0 && size != 0 && height != 0, "empty persisted image");
   for (uint32_t depth = 0; depth < height; depth++)
    {
      uint64_t base = index * size;
      uint16_t level;
      uint32_t count;
      read_at(file, base + 2, &level, sizeof level);
      read_at(file, base + 4, &count, sizeof count);
      uint64_t values = base + 64 + (uint64_t)count * key_size;
      if (level == 0)
       {
         unsigned char value;
         require(count != 0, "empty persisted leaf");
         read_at(file, values, &value, sizeof value);
         value ^= 0x80;
         require(fseeko(file, (off_t)values, SEEK_SET) == 0 &&
                  fwrite(&value, 1, 1, file) == 1 && fclose(file) == 0,
                  "write persisted adversary");
         return;
       }
      index = read_pointer(file, values, lite);
    }
   require(0, "persisted leaf traversal exceeded height");
}

int checked_bptr_unload(struct bptr *tree)
{
   if (pending_tree == tree)
    {
      if (mode_is("live-count")) tree->record_cnt = saved_count;
      if (mode_is("live-value"))
       {
         struct bptr_node *leaf = bptr_node_fetch(tree, saved_leaf);
         require(leaf != NULL, "restore hidden live value");
         *(unsigned char *)leaf->vals = saved_value;
         leaf->is_dirty = 1;
         bptr_node_unload(tree, leaf);
       }
      pending_tree = NULL;
    }
   int status = bptr_unload(tree);
   if (status == 0 && mode_is("persist-value") && !persisted_mutation)
    {
      persisted_mutation = 1;
      corrupt_persisted_value(insert_state.path);
    }
   return status;
}
