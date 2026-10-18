#ifndef SEARCH_H
#define SEARCH_H

/*----------------------------- Public Includes ------------------------------*/
#include "templates.h"
#include "bptr_internal.h"
#include "bptr_node.h"
/*--------------------------- Public Includes END ----------------------------*/

/*------------------------------ Public Structs ------------------------------*/
/* Ordered by the independent leaf chain, never by the function under test. */
struct search_leaf
{
   bptr_node_t node_idx;
   uint32_t count;
   uint64_t offset;
   int64_t first;
};

/* All resources outlive Unity's assertion longjmp and are owned by teardown.
 * Each non-NULL refs slot owns one reference, including pointer aliases. */
struct search_state
{
   const struct template *tmpl;
   struct bptr *tree;
   struct bptr_node *refs[4];
   struct search_leaf *leaves;
   size_t leaf_count;
   uint64_t record_count;
   char dir[PATH_MAX], path[PATH_MAX];
   uint64_t hits, misses;
};
/*---------------------------- Public Structs END ----------------------------*/

extern const struct template *search_template;
extern struct search_state search_state;

/*----------------------------- Public Functions -----------------------------*/
void search_require(_Bool ok, const char *fmt, ...);
void search_prepare(void);
void search_empty_prepare(_Bool lite);
void search_close(void);
void search_reopen(void);
void search_finish(void);
void search_discard(void);
void search_cleanup(void);
int64_t search_key(uint64_t offset);
void search_check(int64_t key, size_t leaf, bptr_node_ki_t idx, _Bool found);
void test_search_hits(void);
void test_search_misses(void);
/*--------------------------- Public Functions END ---------------------------*/

#endif
