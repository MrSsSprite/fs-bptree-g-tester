# Node Cache Test
This unit tests the node cache mechanism — pool allocation, hash table lookup,
reference counting, FIFO eviction, dirty-node flush, and cache-full handling.

`test_cache_fifo_eviction` releases two cached nodes in both orders, with an
optional cache hit and final release that moves the oldest entry to the tail.
Both allocation and fetch pressure must reuse the oldest released node's slot.
The survivor must retain its pointer and dirty payload, while the victim must
miss in a pinned cache and reload cleanly from disk. Both pointer layouts run
all 16 scenarios; choosing the newest node cannot satisfy these checks.

`test_cache_reference_counting` holds two or three aliases to the same node.
Every intermediate release must leave it pinned while a valid disk-only fetch
fails for lack of space; cached hits must still succeed. Churn of the other
slot preserves the retained alias and its dirty payload. Only the final release
may let that slot be reused, after which both nodes must reload correctly.
Both layouts and both pressure paths run all eight scenarios.

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
root. Each run creates a private directory under `bptr_files/` and keeps every
fixture inside it, so concurrent cache runs and caller-prepared images do not
interfere. Unity teardown unloads and removes owned fixtures even when an
assertion aborts a case. All nine cases remove their private directory on exit;
cleanup errors make the run fail.

`make -C test/cache check` checks repeat and concurrent runs with existing
caller fixtures. Disposable assertion mutants fail after acquiring each kind
of fixture and verify that teardown closes its file descriptor and removes
owned files, including an unexpected non-NULL result for an invalid capacity.
It also rejects cache mutants that evict the queue tail, break queue links or
leave a reactivated node in the eviction queue.
Alias mutants omit or double active-hit increments and either drop all aliases
on release or leak a reference; the retained-reference case must reject each.
