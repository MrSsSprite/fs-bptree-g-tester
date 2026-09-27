# Node Split test

This unit tests whether `bptr_node_split` works correctly: it generates
perfectly full tree images, modifies a copy of one with a single insertion, and
checks the tree the split leaves behind -- in memory and again from the file it
was written to.

Everything here drives the library through its **internal** node API: the
public insert API is a stub that exits with `BPTR_E_TODO`, and the split is
`BPTR_STATIC` unless the library is compiled with `BPTR_TESTING`, which
`config.mk` passes for every unit (see `src/bptr_static.h`, the test-side
declaration of the function).

## Running it

From the repository root, because the tests use paths relative to it:

```sh
make clean && make      # `make node_split' for this unit alone
find bptr_files -name '*.bptr' -delete
./bin/node_split
```

Exit code is the number of failed Unity assertions (`UNITY_END()`), `0` when
everything passed.  The unit currently reports **2 cases, 0 failures**: the
template guard and the split case.

The case prints one line per insertion, so a defect that `temp_full_verify'
reports with a fixed message can still be traced to the position under test.
It writes one instance image per position (up to ~58 MB, all of them removed on
the way out); a run that a failing case aborts leaves some behind -- the next
run removes them as it goes.

## Windows

| Level | Positions | Verified |
| --- | --- | --- |
| 1 | 29 (every one) | leaf split + root created |
| 2 | 1065 (every one) | leaf split + full root split, every leaf and every insertion position in the root |
| 3 | 77 (sampled) | leaf split + two cascaded branch splits, each level in each of its insertion cases |

`FULL_FIXTURES` (`src/temp_full.h`) is the one table the whole unit walks:
`gen_full_fixtures()` (in `main`) writes one template per entry, `test_temp`
verifies one per entry before anything is modified, and `test_full_split`
instantiates one per entry.  Querying the templates from `main` keeps a
generation failure out of the Unity cases: it stops the run with
`temp_full_strerror()` on stderr instead.

## Files

| File | Role |
| --- | --- |
| `src/main.c` | `main`, the fixture generation and the `test_temp` guard case |
| `src/temp_full.{h,c}` | the template catalogue, generator, instance copier and `temp_full_verify` |
| `src/temp_split.{h,c}` | the `test_full_split` case: where to insert, how to get there, and the two verification passes |
| `src/bptr_static.h` | declaration of the `BPTR_STATIC` internals the unit calls |

## The split case

For every template, the case reads the key lattice (`st`, `interval`) off the
template's leftmost leaf and then, for every insertion position:

1. copies the pristine template to `bptr_files/temp/split/<lay>-<pos>.bptr`;
2. descends to the leaf the inserted key belongs to (`st - interval / 2 + gap *
   interval`, a key that sorts between two keys of the image);
3. calls `bptr_node_split` on that leaf and checks that it reports the node it
   linked the leaf to;
4. verifies the whole tree with `temp_full_verify(bptr, lay_cnt, st, interval,
   1, key, val)`;
5. unloads the image, loads it again from the file and verifies it a second
   time, so a defect in the flush of the nodes the split touched, or in the
   header it rewrote, cannot hide behind the cache;
6. removes the instance.

Every node of a perfectly full image is full, so the split cascades through
every level of the template and adds one level: the verifier is asked for
`lay_cnt + 1` levels and tolerates exactly the one inserted record.

A **position** is the number of existing keys the inserted key sorts after:
`0` inserts before every key and `record_cnt` behind the last one.  All
positions are tried while they are affordable.  The 3-level template has ~40 000
of them and pays two full tree walks each, so it is sampled instead, at the
positions that put the cascade into each of its cases: a split of a full node
distributes its keys around the promoted one, and where the new key sorts
relative to that point (`<`, `==`, `>`) selects a different branch of
`bptr_node_split` -- at the leaf as well as at every branch level above it.

## What `temp_full_verify` checks

- height (`lay_cnt` levels, plus one when a record was inserted), `root_idx`,
  the number of records and the number of nodes;
- the leaf layer: one global chain, `prev`/`next` and `parent` of every leaf,
  `is_leaf`, every key against the `st`/`interval` lattice, every value against
  `key * 2`, and the position and value of the inserted key when there is one;
- every internal layer: the chain from both ends, `flags`, `level`, the
  separator keys (`key[i]` is the leftmost key of the subtree of child `i + 1`)
  and, without an inserted record, that every node is full.

Not checked: checksums (`checksum` is not implemented by the library), the free
list, and the exact size of the two nodes a split produces (a B+tree split that
distributed the keys differently would still be a valid tree).

## Limits

- Only perfectly full images are split, so every case cascades to the root.  The
  paths that need a parent with room left -- the non-cascading insert of a
  separator into a leaf's parent, and `_node_promote`'s non-full branch -- are
  not reached from here.
- The failure paths of `bptr_node_split` (a full node cache, a failed fetch, a
  failed allocation) are not covered; with a cache too small to hold the path
  from the root to the leaf the library reports `BPTR_E_CACHE_FULL` and leaves
  the tree unchanged, which no case asserts.
- `temp_full_verify` asserts with fixed messages and needs a Unity abort frame,
  so it is only callable from inside a case; the instance of the position it was
  checking is named by the line printed just before it.
