/*
 * Core-free tests of the test oracle itself. The node graph below is assembled
 * by hand, with no insertion, search, image generator, or decoder involved.
 * Linking only oracle.c also prevents accidental use of lookup/flush APIs.
 */
#include "oracle.h"
#include "bptr_internal.h"
#include "bptr_node.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bptr_errno;

static struct bptr_node *nodes[4];
static size_t fetched, released, references, peak_references, checks;
static bptr_node_t failed_fetch;

static void require(_Bool condition, const char *message)
{
   if (!condition)
    {
      fprintf(stderr, "insert oracle check: %s\n", message);
      exit(EXIT_FAILURE);
    }
}

struct bptr_node *bptr_node_fetch(struct bptr *tree, bptr_node_t index)
{
   (void)tree;
   if (index == failed_fetch || index == 0 || index >= 4 || nodes[index] == NULL)
    {
      bptr_errno = BPTR_E_FACCESS;
      return NULL;
    }
   fetched++;
   references++;
   if (references > peak_references) peak_references = references;
   return nodes[index];
}

void bptr_node_unload(struct bptr *tree, struct bptr_node *node)
{
   (void)tree;
   require(node != NULL && references != 0, "release without a fetched node");
   references--;
   released++;
}

static int compare_i64(const void *left, const void *right)
{
   int64_t a, b;
   memcpy(&a, left, sizeof a);
   memcpy(&b, right, sizeof b);
   return a < b ? -17 : a > b ? 31 : 0;
}

static int compare_prefix(const void *left, const void *right)
{
   int result = memcmp(left, right, 2);
   return result < 0 ? -17 : result > 0 ? 31 : 0;
}

static void verify(struct bptr *tree, const struct insert_model *model,
                     const char *expected_diagnostic)
{
   char reason[512];
   fetched = released = references = peak_references = 0;
   int status = insert_live_verify(tree, model, reason, sizeof reason);
   if ((expected_diagnostic == NULL && status != 0) ||
       (expected_diagnostic != NULL &&
        (status == 0 || strstr(reason, expected_diagnostic) == NULL)))
    {
      fprintf(stderr, "insert oracle check: expected %s, actual status %d: %s\n",
                expected_diagnostic == NULL ? "success" : expected_diagnostic,
                status, reason);
      exit(EXIT_FAILURE);
    }
   require(fetched == released && references == 0,
            "every successful fetch must be released on success and failure");
   require(peak_references <= 1, "observer retains more than one core node");
   checks++;
}

static void model_checks(void)
{
   struct insert_model model;
   unsigned char key[3], value[5];
   insert_model_init(&model, sizeof key, sizeof value, compare_prefix);
   /* Force several reallocations and shifts while reusing caller buffers. */
   for (size_t i = 256; i-- != 0;)
    {
      key[0] = (unsigned char)(i >> 8);
      key[1] = (unsigned char)i;
      key[2] = (unsigned char)(i ^ 0x80);
      for (size_t byte = 0; byte < sizeof value; byte++)
         value[byte] = (unsigned char)(i * 13 + byte * 7);
      require(insert_model_add(&model, key, value) == BPTR_E_SUCCESS,
               "model rejected a distinct key");
      memset(key, 0xee, sizeof key);
      memset(value, 0xdd, sizeof value);
    }
   require(model.count == 256, "model count after growth");
   for (size_t i = 0; i < model.count; i++)
    {
      const unsigned char *stored_key = insert_model_key(&model, i);
      const unsigned char *stored_value = insert_model_value(&model, i);
      require(stored_key[0] == (unsigned char)(i >> 8) &&
               stored_key[1] == (unsigned char)i &&
               stored_key[2] == (unsigned char)(i ^ 0x80),
               "model order or copied key bytes changed");
      for (size_t byte = 0; byte < sizeof value; byte++)
         require(stored_value[byte] == (unsigned char)(i * 13 + byte * 7),
                  "model copied value bytes changed");
      key[0] = stored_key[0];
      key[1] = stored_key[1];
      key[2] = (unsigned char)(stored_key[2] ^ 0xff);
      memset(value, 0xff, sizeof value);
      require(insert_model_add(&model, key, value) == BPTR_E_KEY_EXIST,
               "model did not recognize byte-distinct comparator equivalence");
      require(insert_model_key(&model, i)[2] == (unsigned char)(i ^ 0x80),
               "duplicate replaced the original key bytes");
      for (size_t byte = 0; byte < sizeof value; byte++)
         require(insert_model_value(&model, i)[byte] ==
                  (unsigned char)(i * 13 + byte * 7),
                  "duplicate replaced the first value");
    }
   require(model.count == 256, "duplicates changed model count");
   require(insert_model_key(&model, 256) == NULL &&
            insert_model_value(&model, 256) == NULL,
            "model out-of-range accessors must return NULL");
   insert_model_free(&model);
   require(model.records == NULL && model.count == 0,
            "model free did not clear ownership");
   puts("insert oracle: copied model, growth, order and equivalence: PASS");
}

static void child_set(_Bool lite, size_t slot, bptr_node_t index)
{
   if (lite)
    {
      uint32_t stored = (uint32_t)index;
      memcpy((unsigned char *)nodes[1]->vals + slot * sizeof stored,
               &stored, sizeof stored);
    }
   else
    {
      uint64_t stored = (uint64_t)index;
      memcpy((unsigned char *)nodes[1]->vals + slot * sizeof stored,
               &stored, sizeof stored);
    }
}

static void graph_checks(_Bool lite)
{
   struct bptr tree = {0};
   struct insert_model model;
   int64_t one = 1, ten = 10, eleven = 11, twenty_two = 22, zero = 0;
   tree.is_lite = lite;
   /* In either layout, capacities are two keys for branches and leaves. */
   tree.node_size = lite ? 96 : 104;
   tree.key_size = tree.value_size = 8;
   insert_model_init(&model, 8, 8, compare_i64);
   verify(&tree, &model, NULL);
   tree.root_idx = 1;
   verify(&tree, &model, "empty tree");
   tree.root_idx = 0;
   tree.record_cnt = 1;
   verify(&tree, &model, "record count");
   tree.record_cnt = 0;

   require(insert_model_add(&model, &ten, &twenty_two) == 0, "seed key ten");
   require(insert_model_add(&model, &one, &eleven) == 0, "seed key one");
   for (size_t i = 1; i < 4; i++)
    {
      nodes[i] = calloc(1, sizeof *nodes[i] + 16);
      require(nodes[i] != NULL, "allocate hand-built node");
      nodes[i]->vals = calloc(1, 32);
      require(nodes[i]->vals != NULL, "allocate hand-built values");
      nodes[i]->node_idx = i;
      nodes[i]->key_count = 1;
      nodes[i]->flags = BPTR_NODE_FLAG_VALID;
      nodes[i]->is_dirty = 1;
      if (i != 1)
       {
         nodes[i]->parent = 1;
         nodes[i]->is_leaf = 1;
         nodes[i]->flags |= BPTR_NODE_FLAG_LEAF;
       }
    }
   nodes[1]->level = 1;
   nodes[2]->next = 3;
   nodes[3]->prev = 2;
   memcpy(nodes[1]->keys, &ten, 8);
   memcpy(nodes[2]->keys, &one, 8);
   memcpy(nodes[3]->keys, &ten, 8);
   memcpy(nodes[2]->vals, &eleven, 8);
   memcpy(nodes[3]->vals, &twenty_two, 8);
   child_set(lite, 0, 2);
   child_set(lite, 1, 3);
   tree.root_idx = 1;
   tree.height = 2;
   tree.node_cnt = 3;
   tree.record_cnt = 2;
   verify(&tree, &model, NULL);

   tree.record_cnt++;
   verify(&tree, &model, "record count");
   tree.record_cnt--;
   tree.node_cnt++;
   verify(&tree, &model, "node/free counts");
   tree.node_cnt--;
   tree.free_list.cnt = UINT_FAST64_MAX;
   verify(&tree, &model, "node/free counts");
   tree.free_list.cnt = 0;
   tree.root_idx = 0;
   verify(&tree, &model, "outside");
   tree.root_idx = 1;
   tree.height = 0;
   verify(&tree, &model, "geometry or height");
   tree.height = 2;
   tree.key_size++;
   verify(&tree, &model, "configuration differs");
   tree.key_size--;

   nodes[1]->parent = 2;
   verify(&tree, &model, "identity/parent");
   nodes[1]->parent = 0;
   nodes[2]->parent = 0;
   verify(&tree, &model, "identity/parent");
   nodes[2]->parent = 1;
   nodes[2]->node_idx = 3;
   verify(&tree, &model, "identity/parent");
   nodes[2]->node_idx = 2;
   nodes[2]->flags = 0;
   verify(&tree, &model, "flags/level");
   nodes[2]->flags = BPTR_NODE_FLAG_VALID;
   verify(&tree, &model, "flags/level");
   nodes[2]->flags |= BPTR_NODE_FLAG_LEAF;
   nodes[2]->is_leaf = 0;
   verify(&tree, &model, "flags/level");
   nodes[2]->is_leaf = 1;
   nodes[2]->level = 1;
   verify(&tree, &model, "flags/level");
   nodes[2]->level = 0;
   nodes[1]->level = 0;
   verify(&tree, &model, "flags/level");
   nodes[1]->level = 1;

   nodes[2]->key_count = 0;
   verify(&tree, &model, "occupancy");
   nodes[2]->key_count = 3;
   verify(&tree, &model, "occupancy");
   nodes[2]->key_count = 1;
   nodes[1]->key_count = 0;
   verify(&tree, &model, "occupancy");
   nodes[1]->key_count = 3;
   verify(&tree, &model, "occupancy");
   nodes[1]->key_count = 1;
   nodes[1]->next = 2;
   verify(&tree, &model, "sibling links");
   nodes[1]->next = 0;
   nodes[2]->prev = 1;
   verify(&tree, &model, "sibling links");
   nodes[2]->prev = 0;
   nodes[2]->next = 0;
   verify(&tree, &model, "sibling links");
   nodes[2]->next = 3;
   nodes[3]->prev = 0;
   verify(&tree, &model, "sibling links");
   nodes[3]->prev = 2;
   nodes[3]->next = 2;
   verify(&tree, &model, "sibling links");
   nodes[3]->next = 0;

   nodes[2]->keys[0] ^= 1;
   verify(&tree, &model, "key byte");
   nodes[2]->keys[0] ^= 1;
   ((unsigned char *)nodes[3]->vals)[7] ^= 1;
   verify(&tree, &model, "value byte");
   ((unsigned char *)nodes[3]->vals)[7] ^= 1;
   nodes[1]->keys[0] ^= 1;
   verify(&tree, &model, "separator");
   nodes[1]->keys[0] ^= 1;
   nodes[2]->key_count = 2;
   memcpy(nodes[2]->keys + 8, &zero, 8);
   verify(&tree, &model, "strictly ordered");
   nodes[2]->key_count = 1;
   nodes[1]->key_count = 2;
   memcpy(nodes[1]->keys + 8, &one, 8);
   verify(&tree, &model, "strictly ordered");
   nodes[1]->key_count = 1;

   child_set(lite, 1, 2);
   verify(&tree, &model, "duplicate child ownership");
   child_set(lite, 1, 3);
   child_set(lite, 0, 1);
   verify(&tree, &model, "duplicate child ownership");
   child_set(lite, 0, 0);
   verify(&tree, &model, "outside");
   child_set(lite, 0, 4);
   verify(&tree, &model, "outside");
   child_set(lite, 0, 2);
   failed_fetch = 3;
   verify(&tree, &model, "fetch node");
   failed_fetch = 0;
   void *saved_values = nodes[2]->vals;
   nodes[2]->vals = NULL;
   verify(&tree, &model, "NULL values");
   nodes[2]->vals = saved_values;
   verify(&tree, &model, NULL);
   require(nodes[1]->is_dirty && nodes[2]->is_dirty && nodes[3]->is_dirty,
            "observer changed dirty state");

   for (size_t i = 1; i < 4; i++)
    {
      free(nodes[i]->vals);
      free(nodes[i]);
      nodes[i] = NULL;
    }
   insert_model_free(&model);
   printf("insert oracle: %s graph invariants and balanced references: PASS\n",
            lite ? "lite" : "normal");
}

int main(void)
{
   model_checks();
   graph_checks(1);
   graph_checks(0);
   printf("insert oracle: %zu synthetic observations: PASS\n", checks);
   return EXIT_SUCCESS;
}
