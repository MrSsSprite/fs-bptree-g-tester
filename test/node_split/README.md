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
it.  The unit tests the templates it is **given** and never prepares one: the
caller writes them first -- `bin/temp_gen` is the tool for that.

```sh
make clean && make                 # `make node_split' for this unit alone
find bptr_files -name '*.bptr' -delete

# prepare the templates to test, one per shape, then hand the directory over
./bin/temp_gen --dir bptr_files/temp/full 1 0 16
./bin/temp_gen --dir bptr_files/temp/full 2 0 16
./bin/temp_gen --dir bptr_files/temp/full 3 0 16
./bin/node_split                        # default directory, named below
./bin/node_split /tmp/node_split_tpls   # or any directory the caller prepared
```

With no argument the unit prints
`node_split: no template directory given; using the default bptr_files/temp/full/`;
with one argument it prints `node_split: using the templates in <DIR>`.  Either
way it scans that directory for `*.bptr` entries and runs the cases on what it
finds; it generates nothing.  More than one argument is a usage error.

A template is `<DIR>/<lay_cnt>-<st>-<interval>.bptr`, the name `bin/temp_gen`
writes.  A `.bptr` entry that does not follow that convention (or whose
`lay_cnt` is 0) stops the run with `EXIT_FAILURE` and the reason on stderr;
entries that are not `.bptr` at all are ignored.

**Nothing to test is a warning, not a failure.**  A directory that holds no
template -- because it is empty, missing or unreadable -- makes the unit print
`node_split: warning: nothing to test: <reason>` on stderr and exit
successfully without starting Unity.  The templates are the caller's input, so
no input means no case to run; prepare them with `bin/temp_gen` when the run is
expected to test something.

Exit code is the number of failed Unity assertions (`UNITY_END()`), `0` when
everything passed; a template or tool problem found before the cases is
`EXIT_FAILURE` (`1`).  A run over prepared templates reports **2 cases, 0
failures**: the template guard and the split case.  The guard is the first case,
but Unity continues after a failed case: a template the guard rejects also fails
the split case, and the run's exit code is the verdict -- a defect is never a
false pass.

The runner captures what every tool it spawns prints, on both streams, and
replays the capture on stderr **only when that tool fails**.  A passing run is
therefore the unit's own lines and nothing else -- no Unity block per verify
call, no `temp_inst: ... copied` per position -- and a tool's report of a defect
still appears above the unit's message for it.

The split case in turn draws its own progress as **one live line per phase**: on
a terminal the line is rewritten in place while the loop walks the positions, so
a 2-level template takes one line rather than the ~1065 it prints turns, and the
phase ends with its verdict:

```
    split bptr_files/temp/split/2-*.bptr: PASS
    verify bptr_files/temp/split/2-*.bptr: PASS
```

A redirected stdout is read as a log, so it never sees the live line -- it gets
the verdict lines alone (13 lines for a directory holding the level-1 template,
23 for the three of `bin/temp_gen`'s easy layouts).  A line wider than the
terminal is clipped from the left behind a `...`, which keeps the instance name,
the key and the position visible.  When a phase fails, the turn that failed is
finished off before the assertion reports it, so a log still says which position
died:

```
    verify bptr_files/temp/split/2-784.bptr: key 3123
src/temp_split.c:...:test_full_split:FAIL: failed to verify the instance ...
```

`src/progress.{h,c}` holds those three calls: `progress_update`,
`progress_done` and `progress_break`.

## The tools

The template work lives in `utils/temp` and is reached as stand-alone programs.
The unit spawns the two it needs by `posix_spawn`/`waitpid` (`src/tools.{h,c}`),
relative to the working directory -- there is no `PATH` search and no template
code in this directory any more.  `bin/temp_gen` is not among them: preparing a
template is the caller's step, so the unit holds no handle on the generator.
Neither tool writes into the unit's log: the runner gives the child a pipe in
place of its stdout and stderr, keeps the capture of a tool that exited 0 to
itself, and quotes one that did not on stderr (`src/tools.{h,c}`).

| Tool | Call | Exit status |
| --- | --- | --- |
| `bin/temp_gen` | caller's tool: `[--dir DIR] [--norm] [--node-size N] LAY_CNT ST INTERVAL` | 0 written, 1 not built (reason on stderr), 2 usage |
| `bin/temp_inst` | `SRC DST` (spawned by the unit) | 0 copied, 1 `DST` exists (untouched), 2 usage, 3 copy error |
| `bin/temp_verify` | `[--lay-cnt N] [--st S] [--interval I] [--key K --val V] TEMPLATE...` (spawned by the unit) | 0 verified, 1 check failed (the report is captured by the runner and replayed on stderr), 2 usage, 3 load failure |

`temp_inst` creates the parent directories of `DST`.  `temp_verify` reads the
layout from the template name when it is not given; the unit always passes
`--lay-cnt`/`--st`/`--interval` explicitly, because its instances are named
after the position they were split at, and passes `--key`/`--val` for an
instance that carries the inserted record.

A tool that cannot be started is reported as
`cannot run bin/temp_inst: <errno> (build the tools with 'make')`; a killed child
as `128 + signal`, and any other failure as `bin/temp_*: exited with status N`.

## Windows

| Level | Positions | Verified |
| --- | --- | --- |
| 1 | 29 (every one) | leaf split + root created |
| 2 | 1065 (every one) | leaf split + full root split, every leaf and every insertion position in the root |
| 3 | 77 (sampled) | leaf split + two cascaded branch splits, each level in each of its insertion cases |

The list the cases walk is the one `templates_load_dir()` built from the
directory the caller named (or `TEMPLATES_DEFAULT_DIR`): one entry per
`<lay_cnt>-<st>-<interval>.bptr` image found there, sorted by shape so that a
run is reproducible.  What the caller prepared is therefore what the unit
covers, and `test_temp` verifies every one of them before the split case
modifies a copy.

## Files

| File | Role |
| --- | --- |
| `src/main.c` | `main` (the directory argument, the notices and the empty-path warning) and the `test_temp` guard case |
| `src/templates.{h,c}` | `struct template`, the name convention, the directory scan and `cmp_i64` |
| `src/tools.{h,c}` | the `posix_spawn` runner (the tool's output is captured and replayed on stderr only when it fails) and the `temp_inst`/`temp_verify` wrappers |
| `src/progress.{h,c}` | the live progress line of a phase and its verdict: one line rewritten in place on a terminal, the verdicts alone in a redirected run |
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
seen on the file that was actually written.  The runner captures what the tool
printed: the `verify` line below is the whole report of a checked instance, and
the tool's own Unity block is replayed on stderr only for the call that failed.

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

The case draws one live line per phase -- `split` for the image it just wrote in
the first loop, `verify` for the check of the flushed instance in the second --
and replaces it with the verdict of that phase when the loop is through, so a
defect the tool reports can still be traced to the position under test: the
replayed report sits on stderr next to the line that named the image, and in a
redirected run that line is printed before the assertion message.  The case
writes one instance image per position (up to ~58 MB, all of them removed on the
way out); a run that a failing case aborts leaves some behind -- the next run
removes them as it goes.

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
- The unit tests only what it is handed: a shape the caller does not prepare is
  not covered by the run, and an empty directory passes with a warning.
