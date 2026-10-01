# Node Split test

This unit tests whether `bptr_node_split` works correctly: it takes a perfectly
full tree template, copies it, inserts a single record into the copy, splits the
leaf the record belongs to, and checks the tree the split leaves behind -- from
the file it was written to.

Everything here drives the library through its **internal** node API: the
public insert API is a stub that exits with `BPTR_E_TODO`, and the split is
`BPTR_STATIC` unless the library is compiled with `BPTR_TESTING`, which
`config.mk` passes for every unit (see `src/bptr_static.h`, the test-side
declaration of the function).

## Running it

From the repository root, because the tests and the tools use paths relative to
it:

```sh
make clean && make      # `make node_split' for this unit alone
find bptr_files -name '*.bptr' -delete
./bin/node_split                        # templates generated in the default dir
./bin/node_split /tmp/node_split_tpls   # templates taken from a directory
```

With no argument the unit prints
`node_split: no template directory given; using the default bptr_files/temp/full/`,
generates one template per `FULL_FIXTURES` entry there with `bin/temp_gen`, and
only then starts Unity.  With one argument it prints
`node_split: using the templates in <DIR>` and scans `<DIR>/*.bptr` instead; it
generates nothing.  More than one argument is a usage error.

A template is `<DIR>/<lay_cnt>-<st>-<interval>.bptr`, exactly as
`templates_path()` writes it.  In directory mode a missing, unreadable or empty
directory, or a `.bptr` name that does not follow the convention (or whose
`lay_cnt` is 0), stops the run before Unity with `EXIT_FAILURE` and the reason
on stderr; entries that are not `.bptr` at all are ignored.

Exit code is the number of failed Unity assertions (`UNITY_END()`), `0` when
everything passed; a template or tool problem found before the cases is
`EXIT_FAILURE` (`1`).  The unit currently reports **2 cases, 0 failures**: the
template guard and the split case.

The tools print their own progress on stdout, so a run interleaves their lines
with the unit's; the unit's summary is the last `N Tests ...` block.

## The tools

The template work lives in `utils/temp` and is reached as three stand-alone
programs.  The unit spawns them by `posix_spawn`/`waitpid` (`src/tools.{h,c}`),
relative to the working directory -- there is no `PATH` search and no template
code in this directory any more.

| Tool | Call | Exit status |
| --- | --- | --- |
| `bin/temp_gen` | `[--dir DIR] [--norm] [--node-size N] LAY_CNT ST INTERVAL` | 0 written, 1 not built (reason on stderr), 2 usage |
| `bin/temp_inst` | `SRC DST` | 0 copied, 1 `DST` exists (untouched), 2 usage, 3 copy error |
| `bin/temp_verify` | `[--lay-cnt N] [--st S] [--interval I] [--key K --val V] TEMPLATE...` | 0 verified, 1 check failed (Unity output on stdout), 2 usage, 3 load failure |

`temp_inst` creates the parent directories of `DST`.  `temp_verify` reads the
layout from the template name when it is not given; the unit always passes
`--lay-cnt`/`--st`/`--interval` explicitly, because its instances are named
after the position they were split at, and passes `--key`/`--val` for an
instance that carries the inserted record.

A tool that cannot be started is reported as
`cannot run bin/temp_gen: <errno> (build the tools with 'make')`; a killed child
as `128 + signal`, and any other failure as `bin/temp_*: exited with status N`.

The tools are a separate change (`utils/temp`) and `bin/` is gitignored build
output, so before the two are merged, borrow the built tools:

```sh
cp ../vibe/bin/temp_gen ../vibe/bin/temp_inst ../vibe/bin/temp_verify bin/
```

## Windows

| Level | Positions | Verified |
| --- | --- | --- |
| 1 | 29 (every one) | leaf split + root created |
| 2 | 1065 (every one) | leaf split + full root split, every leaf and every insertion position in the root |
| 3 | 77 (sampled) | leaf split + two cascaded branch splits, each level in each of its insertion cases |

`FULL_FIXTURES` (`src/templates.h`) is the one table the whole unit walks in
default mode: `gen_full_fixtures()` (in `main`) generates one template per entry
and `test_temp` verifies one per entry before anything is modified; the
directory mode replaces that table with the scanned list (`templates_get()`),
for the cases only.  Generating from `main` keeps a template failure out of the
Unity cases: it stops the run before `UNITY_BEGIN()`.

## Files

| File | Role |
| --- | --- |
| `src/main.c` | `main`, the default-directory generation and the `test_temp` guard case |
| `src/templates.{h,c}` | the fixture table, `struct template`, the name convention, the directory scan and `cmp_i64` |
| `src/tools.{h,c}` | the `posix_spawn` runner and the three tool wrappers |
| `src/temp_split.{h,c}` | the `test_full_split` case: where to insert, how to get there |
| `src/bptr_static.h` | declaration of the `BPTR_STATIC` internals the unit calls |

## The split case

For every template, the case reads the key lattice (`st`, `interval`) off the
template's leftmost leaf and then, for every insertion position:

1. copies the pristine template to `bptr_files/temp/split/<lay>-<pos>.bptr`
   with `bin/temp_inst`;
2. descends to the leaf the inserted key belongs to (`st - interval / 2 + gap *
   interval`, a key that sorts between two keys of the image);
3. calls `bptr_node_split` on that leaf and checks that it reports the node it
   linked the leaf to;
4. unloads the instance: `bptr_unload` flushes and closes it, and an instance is
   never verified while the cache still holds it.

Only after every position of a template has been split and flushed does a second
loop hand each instance file to `bin/temp_verify` (`--lay-cnt`, `--st`,
`--interval`, `--key`, `--val`), assert exit 0 and remove it.  There is no
in-memory verification pass any more: the tool is the verifier, so a defect in
the split, in the flush of the nodes it touched, or in the header it rewrote is
seen on the file that was actually written.

Every node of a perfectly full image is full, so the split cascades through
every level of the template and adds one level: the tool is asked for
`lay_cnt + 1` levels and tolerates exactly the one inserted record.

A **position** is the number of existing keys the inserted key sorts after:
`0` inserts before every key and `record_cnt` behind the last one.  All
positions are tried while they are affordable.  The 3-level template has ~40 000
of them and pays a full tree walk each, so it is sampled instead, at the
positions that put the cascade into each of its cases: a split of a full node
distributes its keys around the promoted one, and where the new key sorts
relative to that point (`<`, `==`, `>`) selects a different branch of
`bptr_node_split` -- at the leaf as well as at every branch level above it.

A template the sampler cannot serve is **skipped with a printed notice**: more
than 3 levels tall, or an `interval` with `interval / 2 == 0` (no key fits
between two keys of the image).  The skip is never silent, and `test_temp`
verifies a skipped template like any other, so a directory of tall or dense
templates still fails loudly when one of them is broken.

The case prints one line per insertion, so a defect the tool reports can still
be traced to the position under test.  It writes one instance image per
position (up to ~58 MB, all of them removed on the way out); a run that a
failing case aborts leaves some behind -- the next run removes them as it goes.

## What `temp_verify` checks

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
- A template taller than 3 levels or with `interval / 2 == 0` is only guarded,
  not split; the notice names it, so the gap is visible in the run.
