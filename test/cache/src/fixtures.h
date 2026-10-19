#ifndef CACHE_FIXTURES_H
#define CACHE_FIXTURES_H

#include "test_bptr_temp.h"

int cache_fixtures_begin(void);
int cache_fixtures_end(void);
struct bptr *cache_fixture_try_create(const struct bptr_temp *template);
struct bptr *cache_fixture_create(const struct bptr_temp *template);
int cache_fixture_unload(void);
int cache_fixture_discard(void);

#endif
