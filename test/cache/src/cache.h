#ifndef HEADER_H
#define HEADER_H

/*----------------------- Public Function Declarations -----------------------*/
void test_cache_init(void);
void test_cache_node_lifecycle(void);
void test_cache_data_persistence(void);
void test_cache_fetch_persistence(void);
void test_cache_capacity_boundaries(void);
void test_cache_hash_collisions(void);
void test_cache_hash_collision_deletion(void);
int cache_collisions_cleanup(void);
int cache_persistence_cleanup(void);
/*--------------------- Public Function Declarations END ---------------------*/

#endif
