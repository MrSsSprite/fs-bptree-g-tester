# Node Cache Test
This unit tests the node cache mechanism — pool allocation, hash table lookup,
reference counting, FIFO eviction, dirty-node flush, and cache-full handling.

`test_cache_data_persistence` changes two keys and two values after flushing
different baseline records, then leaves those changes dirty. A two-slot cache
keeps the other node pinned, making the modified leaf the only eviction
candidate when a new node is allocated. The test proves that the new node
reuses the victim's slot, then reloads the victim from disk and checks both
records and its clean flag before tree shutdown can flush cached contents.
Both lite and normal layouts run this scenario.

`test_cache_fetch_persistence` repeats the same dirty-record checks with a
third leaf already on disk. Fetching that uncached leaf forces eviction
through the fetch path, independently of the allocation path above. It also
checks both layouts, confirms slot reuse and clears the reused payload buffer
before reloading, so leftover memory cannot satisfy persistence assertions.

`test_cache_hash_collisions` uses node IDs 1, 6, 9 and 17 in a four-slot
cache. With the current Fibonacci hash and eight hash buckets, IDs 1, 9 and
17 share bucket 4, while ID 6 starts in bucket 5 and is displaced twice by
Robin Hood insertion. Both lite and normal layouts check every resident's
pointer, node ID, key and value in both lookup orders. All slots stay pinned
and payload changes stay unflushed, so a failed lookup cannot pass by loading
a duplicate from disk. Update these deliberately chosen IDs if the hash or
table-sizing policy changes.

`test_cache_hash_collision_deletion` uses IDs 3, 8 and 11 in a three-slot
cache: all hash to bucket 3 of four, so probing wraps through buckets 0 and
1. A valid disk-only collider (ID 16) must miss while every slot is pinned.
Separate scenarios evict the cluster's head, middle and tail, check that the
surviving nodes still hit, replace the reused slot again to expose incorrect
probe distances, then reload the victim to catch stale mappings. Each scenario
runs in both layouts and makes only the chosen victim evictable.

Build with `make clean && make cache`, then run `./bin/cache` from the worktree
root. The seven Unity cases create and remove fixtures under `bptr_files/`;
use a fresh working directory when old fixtures may be present. Collision and
persistence fixtures are also cleaned by Unity teardown if an assertion aborts.
