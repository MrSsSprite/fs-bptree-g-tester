# Node Split test

This unit tests whether `bptr_node_split` works correctly: it copies a tree
template, inserts a single record into a full leaf, and checks the resulting
tree from the file it was written to. Full templates exercise propagation to a
new root. Custom occupancy templates also exercise a leaf split absorbed by an
existing parent and a cascade absorbed below the root.

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
make clean && make cache fflush node_split temp  # all units and tools
find bptr_files -name '*.bptr' -delete

# prepare the templates to test, one per shape, then hand the directory over
./bin/temp_gen --dir bptr_files/temp/full 1 0 16
./bin/temp_gen --dir bptr_files/temp/full 2 0 16
./bin/temp_gen --dir bptr_files/temp/full 3 0 16
# a cheap tall-tree smoke test: smaller nodes keep the full template small
./bin/temp_gen --dir bptr_files/temp/full --node-size 112 4 0 16
./bin/node_split                        # default directory, named below
./bin/node_split /tmp/node_split_tpls   # or any directory the caller prepared
```

With no argument the unit prints
`node_split: no template directory given; using the default bptr_files/temp/full/`;
with one argument it prints `node_split: using the templates in <DIR>`.  Either
way it scans that directory for `*.bptr` entries and runs the cases on what it
finds; it generates nothing.  More than one argument is a usage error.

A template uses the canonical name written by `bin/temp_gen`: either
`<lay_cnt>-<st>-<interval>.bptr` for a full shape or
`<lay_cnt>-<st>-<interval>--keys-<overrides>.bptr` for custom occupancy. Full and
custom fixtures can share a directory. The runner sorts them by height, start,
interval, then canonical filename. A `.bptr` entry that does not follow that
convention (including decimal aliases or reordered overrides) or whose
`lay_cnt` is 0 stops the run with `EXIT_FAILURE` and the reason on stderr;
entries that are not `.bptr` at all are ignored.

### Preparing splits that stop early

`--node-keys PATH=COUNT` sets the number of keys in one original node. `root`
selects the root; `0` selects its first child; `0.1` selects child 1 of child 0.
Child indices start at zero. A leaf with `COUNT` keys has `COUNT` records; a
branch with `COUNT` keys has `COUNT + 1` children. Unspecified nodes stay full,
and the option can be repeated. The [fixture-tool README](../../utils/temp/README.md)
explains legal occupancies, canonical names and path resolution.

With 128-byte lite nodes, branches hold at most five keys and leaves four. Use
a root with four keys to absorb a leaf split, or a root child with four keys to
absorb a cascade before it reaches the root:

```sh
./bin/temp_gen --dir bptr_files/temp/partial-lite --node-size 128 \
  --node-keys root=4 2 0 16
./bin/temp_gen --dir bptr_files/temp/partial-lite --node-size 128 \
  --node-keys 0=4 3 0 16
./bin/temp_gen --dir bptr_files/temp/partial-lite --node-size 128 \
  --node-keys 0=4 4 0 16
./bin/node_split bptr_files/temp/partial-lite
```

The height-3 fixture has a non-full leaf parent in child `0`; the height-4
fixture has a full leaf parent below a non-full grandparent. In both fixtures,
samples outside child `0` also exercise propagation through the full root.
Normal-layout branches hold three keys, so the equivalent overrides use two:

```sh
./bin/temp_gen --dir bptr_files/temp/partial-normal --norm --node-size 128 \
  --node-keys root=2 2 0 16
./bin/temp_gen --dir bptr_files/temp/partial-normal --norm --node-size 128 \
  --node-keys 0=2 3 0 16
./bin/temp_gen --dir bptr_files/temp/partial-normal --norm --node-size 128 \
  --node-keys 0=2 4 0 16
./bin/node_split bptr_files/temp/partial-normal
```

Keep the two layouts in separate directories: their filename identities encode
occupancy and lattice, while layout comes from the file header.

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
but Unity continues after a failed case, so the guard is not a gate: a rejected
template still reaches the split case. The run's exit code is the verdict.

The runner captures what every tool it spawns prints, on both streams, and
replays the capture on stderr **only when that tool fails**.  A passing run is
therefore the unit's own lines and nothing else -- no Unity block per verify
call, no `temp_inst: ... copied` per position -- and a tool's report of a defect
still appears above the unit's message for it.

The split case draws one live line per template. On a terminal it rewrites
that line as each instance moves through `split`, `verify` and `remove`, then
prints both verdicts once every instance has been checked and removed:

```
    split bptr_files/temp/split/2-*.bptr: PASS
    verify bptr_files/temp/split/2-*.bptr: PASS
```

A redirected stdout is read as a log, so it never sees the live line -- it gets
the verdict lines alone (13 lines for a directory holding the level-1 template,
23 for the three of `bin/temp_gen`'s easy layouts).  A live line wider than the
terminal is clipped from the left behind a `...`, which keeps the instance name,
the key and the position in view; the verdict lines are never clipped, so a very
narrow terminal wraps them rather than losing the phase they name.  When a phase
fails, the turn that failed is finished off before the assertion reports it, so
a log still says which position died:

```
    verify bptr_files/temp/split/2-784.bptr: key 3123 (position 785/785)
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
| `bin/temp_gen` | caller's tool: `[--dir DIR] [--norm] [--node-size N] [--node-keys PATH=COUNT]... LAY_CNT ST INTERVAL` | 0 written, 1 not built (reason on stderr), 2 usage |
| `bin/temp_inst` | `SRC DST` (spawned by the unit) | 0 copied, 1 `DST` exists (untouched), 2 usage, 3 copy error |
| `bin/temp_verify` | `[--lay-cnt N] [--st S] [--interval I] [--node-keys PATH=COUNT]... [--key K --val V] [--expect-height H --expect-nodes N --expect-leaves L] TEMPLATE...` (spawned by the unit) | 0 verified, 1 check failed (the report is captured by the runner and replayed on stderr), 2 usage, 3 load failure |

`temp_inst` creates the parent directories of `DST`.  `temp_verify` reads the
layout from the template name when it is not given; the unit always passes
`--lay-cnt`/`--st`/`--interval` explicitly, because its instances are named
after the position they were split at, and passes `--key`/`--val` for an
instance that carries the inserted record.

For custom fixtures the unit also forwards every original occupancy override.
These paths describe the pristine tree, before insertion. For inserted images
it supplies the expected final height, node count and leaf count together;
full fixtures retain their inferred full-cascade expectations. Pristine
verification checks exact occupancy at every original path.

A tool that cannot be started is reported as
`cannot run bin/temp_inst: <errno> (build the tools with 'make')`; a killed child
as `128 + signal`, and any other failure as `bin/temp_*: exited with status N`.

## Coverage

Counts for levels 1--3 below use the default 512-byte lite layout. Taller-tree
counts are candidates before duplicate gaps are removed.

| Level | Positions | Verified |
| --- | --- | --- |
| 1 | 29 (every one) | leaf split + root created |
| 2 | 1065 (every one) | leaf split + full root split, every leaf and every insertion position in the root |
| 3 | 77 (sampled, original order and duplicates retained) | leaf split + two cascaded branch splits, each level in each of its insertion cases |
| 4--7 | at most 77 | each pair of branch-level roles, crossed with the three leaf roles, plus both endpoints |
| 8--32 | at most 377 | the same pairwise coverage using three projection coordinates |
| 33 and above | `2 + 3 * 5^m`, before deduplication | the same coverage; choose the smallest `m >= 2` with `(5^m - 1) / 4 >= height - 1` |

Custom heights 1--2 exhaust every gap whose selected leaf is full. Taller custom
fixtures use the branch-role projections below with each node's actual child
count. They also anchor projections under every overridden branch, include the
first and last full leaf in those subtrees, and probe eligible neighbors of
overridden leaves. Duplicate custom gaps are removed in first-seen order.
Non-full leaves are excluded and their count is printed. A fixture with no full
leaf is verified pristine, then prints `skip ...: no full leaves to split`.

The integration checks assert these final totals for splits in the affected
subtree of the 128-byte fixtures above, with start `0` and interval `16`:

| Layout | Height / override | Final height | Nodes | Leaves | Records |
| --- | --- | --- | --- | --- | --- |
| Lite | 2 / `root=4` | 2 | 7 | 6 | 21 |
| Normal | 2 / `root=2` | 2 | 5 | 4 | 13 |
| Lite | 3 / `0=4` | 3 | 43 | 36 | 141 |
| Normal | 3 / `0=2` | 3 | 21 | 16 | 61 |
| Lite | 4 / `0=4` | 4 | 254 | 211 | 841 |
| Normal | 4 / `0=2` | 4 | 82 | 61 | 241 |

The list the cases walk is the one `templates_load_dir()` built from the
directory the caller named (or `TEMPLATES_DEFAULT_DIR`): one entry per
canonical full or custom image found there, sorted by shape so that a
run is reproducible.  What the caller prepared is therefore what the unit
covers, and `test_temp` verifies every one of them before the split case
modifies a copy.

## Files

| File | Role |
| --- | --- |
| `src/main.c` | `main` (the directory argument, the notices and the empty-path warning) and the `test_temp` guard case |
| `src/templates.{h,c}` | `struct template`, shared shape parsing, the canonical-name guard, the directory scan and `cmp_i64` |
| `src/tools.{h,c}` | the `posix_spawn` runner (the tool's output is captured and replayed on stderr only when it fails) and the `temp_inst`/`temp_verify` wrappers |
| `src/progress.{h,c}` | the live progress line of a phase and its verdict: one line rewritten in place on a terminal, the verdicts alone in a redirected run |
| `src/temp_split.{h,c}` | the `test_full_split` case: descend, split, flush/close, verify and remove one instance at a time |
| `src/split_sampler.{h,c}` | pure sampling and checked key/value arithmetic; independent small-tree and taller-tree strategies |
| `checks/` | standalone sampler tests, instrumented lifecycle checks and isolated real-tool integration runs |
| `src/bptr_static.h` | declaration of the `BPTR_STATIC` internals the unit calls |

## The split case

For every template, the case reads capacities and record count from the loaded
tree and uses the `st`/`interval` and occupancy from its name, already checked by the guard.
It does not assume that a leaf has room for two keys. Before copying any
instance, it checks requested-shape geometry, 64-bit gap arithmetic and that
inserted keys and doubled values fit `int64_t` at the lowest and highest
eligible sample.

For each insertion position, it:

1. copies the pristine template to `bptr_files/temp/split/<lay>-<pos>.bptr`
   with `bin/temp_inst`;
2. descends to the leaf for `st - interval / 2 + gap * interval`;
3. calls `bptr_node_split` and checks that its return value names the sibling
   linked to the leaf, then checks where propagation stopped;
4. unloads the instance: `bptr_unload` must successfully flush and close it;
5. hands that file to `bin/temp_verify` with the layout and inserted record,
   and requires exit 0;
6. removes the verified instance before copying the next one.

This order applies at every height. A clean successful run holds one additional
instance image at a time. Verification failure preserves that image and stops
the split case; removal failure also stops it before another copy. A retry
removes a stale instance when it reaches that same instance name.

The verifier checks the entire file, including untouched nodes. The in-memory
assertions cover the split result, totals and stopping ancestors; complete
structural verification runs after persistence. A defect normalized by the flush
can escape that persisted check. The runner captures the tool's output and replays it on stderr only when
the tool fails. Progress names the operation, instance, key and sample number;
a redirected run prints the failing turn before the assertion message.

Expected totals come from the original shape: each full node that splits adds
one sibling, and propagation stops at the first existing branch with space.
Splitting the original root adds another node for the new root and increases
height. Every sampled split adds one leaf and one record. When an existing
ancestor absorbs the promotion, the case also checks that the root identity and
height are unchanged, the absorbing ancestor gained one key, and its ancestors'
occupancies are unchanged.

### Choosing positions

A **position** is the number of existing keys the inserted key sorts after:
`0` inserts before every key and `record_cnt` behind the last one. Heights 1--2
try every position, lazily, for full fixtures. Full height-3 fixtures retain the
original 77-position sequence, including its duplicate endpoint: two endpoints followed by the Cartesian
product of five root roles, five level-1 roles and three leaf roles.

The five branch roles are child indices `0`, `half - 1`, `half`, `half + 1`
and `brch_up - 1`, where `half = brch_up / 2`. They probe the edges and the
positions below, at and above the branch promotion point. The leaf roles are
the last position below its split boundary, the first above it, and the position
behind its last key.

Full fixtures of heights >=4 use an independent projection sampler. For
`d = height - 1` branch levels, choose the smallest `m >= 2` with
`(5^m - 1) / 4 >= d`. Assign levels
lexicographically ordered base-5 coefficient vectors whose first nonzero
coordinate is 1. Enumerate all `5^m` input vectors; the dot product modulo 5
selects each level's role. For height 4, for example, the coefficient vectors
are `(0, 1)`, `(1, 0)` and `(1, 1)`, giving roles `y`, `x` and `x + y` modulo 5.

Any two chosen coefficient vectors are linearly independent modulo 5, so every
pair of branch levels receives all 25 role combinations. Cross each profile
with the three leaf positions, prepend both endpoints, and deduplicate gaps in
first-seen order. Tiny capacities can merge roles; a one-key leaf can receive a
key before its first key only at the tree's left edge, so coverage refers to
realizable roles.

For full fixtures, child indices form a number in base `brch_up`, giving the
leaf index directly; multiplying by leaf capacity and adding its insertion
offset gives the gap.
Sampling never walks existing keys or nodes, and stores only the compact gap
list. Copying and full-file verification still cost a tree-sized pass per
sample. For the default lite height-4 layout, the template is about 27.5 MiB:
keeping 77 instances together would require about 2.1 GiB, while this loop
requires one instance's space in addition to the template.

Custom offsets use the requested subtree record totals. Untouched full
subtrees are aggregated arithmetically, so sampling does not enumerate a huge
tree. Candidate storage depends on height and sparse overrides; copying and
verification still traverse each sampled image. The insertion key selects the
leaf, so the gap immediately before a leaf's first key belongs to the preceding
leaf. This matters when a neighboring leaf is non-full or has only one key.
Custom projections use each selected branch's actual child count. Variable
fanout can merge roles or change the path a profile follows, so the full-tree
pairwise guarantee does not apply across every custom path. Anchors ensure
that overridden subtrees and nearby full leaves still receive samples.

An `interval` with `interval / 2 == 0` is still **skipped with a printed notice**,
and the guard verifies it. Heights above 3 are no longer skipped. Inconsistent
geometry, overflowing counts or unrepresentable inserted records fail before
any instance is copied.

### Regression checks

After building, run from the repository root:

```sh
make -C test/node_split check
python3 test/node_split/checks/integration.py
```

`check` runs the standalone sampler binary, four fixture-loader tests,
exact verifier-argument checks and 16 instrumented lifecycle scenarios.
The driver treats injected verification, removal
and unload failures as passing checks only when call ordering, retained-image
state and stopping behavior match expectations.

The integration script prepares and removes its own fixtures and working
directories under `.cache/`. It covers heights 1--3, compact lite and normal
heights 4--5, one-key leaves, compact height 8, and default-size height 4.
It also covers the six stopped-cascade fixtures above, asymmetric and nested
overrides, mixed full/custom ordering, partial-leaf exclusion, skip-only shapes,
custom one-key leaves, and malformed or noncanonical names. Fixed expected
totals and an independent disk decoder check every custom result, including
unchanged root identity and ancestor occupancy when propagation stops. Real
tool wrappers prove that each previous instance was removed before the next
copy, and that an injected verifier failure preserves exactly the failing
image, for both full and custom fixtures. It also checks quiet logs, repeatable
custom sampling, the interval-1 skip and overflow rejection
before copying. Existing template and instance directories are left untouched.

## What `temp_verify` checks

- height (inferred for full fixtures or explicitly supplied for custom inserted images), `root_idx`,
  the number of records and the number of nodes;
- the leaf layer: one global chain, `prev`/`next` and `parent` of every leaf,
  `is_leaf`, every key against the `st`/`interval` lattice, every value against
  `key * 2`, and the position and value of the inserted key when there is one;
- every internal layer: the chain from both ends, `flags`, `level`, the
  separator keys (`key[i]` is the leftmost key of the subtree of child `i + 1`)
  and, without an inserted record, the exact requested occupancy at every path;
- independently calculated original record, node and leaf totals, and final
  node totals against both the header and the actual traversal.

Not checked: checksums (`checksum` is not implemented by the library), the free
list, and the exact size of the two nodes a split produces (a B+tree split that
distributed the keys differently would still be a valid tree).

## Limits

- Only full leaves are split. Custom fixtures cover existing parents and
  higher ancestors with space; inserting into a non-full leaf is outside the
  internal split API's preconditions.
- The failure paths of `bptr_node_split` (a full node cache, a failed fetch, a
  failed allocation) are not covered; with a cache too small to hold the path
  from the root to the leaf the library reports `BPTR_E_CACHE_FULL` and leaves
  the tree unchanged, which no case asserts.
- A template with `interval / 2 == 0` is only guarded, not split; the notice
  names it. Taller trees cover pairwise branch-role projections rather than
  every possible combination across all levels.
- Full-file verification and copying remain proportional to tree size per
  sample. Practical height is limited by file size, library/cache capacity,
  representable geometry, and the signed key/value range.
- The unit tests only what it is handed: a shape the caller does not prepare is
  not covered by the run, and an empty directory passes with a warning.
