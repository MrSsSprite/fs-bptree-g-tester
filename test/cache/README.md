# Node Cache Test
This unit tests the node cache mechanism — pool allocation, hash table lookup,
reference counting, FIFO eviction, dirty-node flush, and cache-full handling.

`test_cache_hash_collisions` uses node IDs 1, 6, 9 and 17 in a four-slot
cache. With the current Fibonacci hash and eight hash buckets, IDs 1, 9 and
17 share bucket 4, while ID 6 starts in bucket 5 and is displaced twice by
Robin Hood insertion. Both lite and normal layouts check every resident's
pointer, node ID, key and value in both lookup orders. All slots stay pinned
and payload changes stay unflushed, so a failed lookup cannot pass by loading
a duplicate from disk. Update these deliberately chosen IDs if the hash or
table-sizing policy changes.

Build with `make clean && make cache`, then run `./bin/cache` from the worktree
root. The five Unity cases create and remove fixtures under `bptr_files/`;
use a fresh working directory when old fixtures may be present. Collision
fixtures are also cleaned by Unity teardown if an assertion aborts a case.
