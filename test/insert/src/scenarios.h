#ifndef INSERT_SCENARIOS_H
#define INSERT_SCENARIOS_H

/* -1 runs both, 0 normal pointers, 1 lite pointers. */
extern int insert_scenario_layout;

void test_insert_empty(void);
void test_insert_permutations(void);
void test_insert_growth(void);
void test_insert_comparators(void);
void test_insert_binary(void);
void test_insert_equivalence(void);
void test_insert_ownership(void);
void test_insert_fixtures(void);
void test_insert_cache(void);
void insert_scenarios_cleanup(void);

#endif
