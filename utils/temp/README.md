# Template utilities

## External comparator ABI

`src/temp_compare.h` defines a standalone POSIX comparator loader. A test-owned
shared library exports `temp_comparator_v1()` (with C linkage for C++), returning
a static `struct temp_comparator_descriptor_v1`. Set `abi_version` to
`TEMP_COMPARATOR_ABI_V1`, `struct_size` to `sizeof` the descriptor, `reserved` to
zero, and provide a nonempty versioned ID, exact key width, and callback:

```c
int compare(const void *lhs, const void *rhs);
```

Negative, zero, and positive results mean before, equivalent, and after.
Callbacks must define a consistent order and read only the declared key bytes.
They need not return unit magnitudes. See `tests/comparator_plugin.c` for an
ID-only comparator that ignores a key's tag. The callback has no context
parameter; use case-specific immutable configuration, not a mutable global
configuration shared by trees with different orders.

Call `temp_comparator_open` with a zero-initialized module, library path, key
width, and optional expected ID. Missing symbols, incompatible ABI/width, and
wrong IDs fail explicitly. Keep the module open until all trees and checks
using its callback are finished; `temp_comparator_close` clears its borrowed
pointers. The loader does not call or depend on the core library.

`make -C utils/temp check-comparator` builds and exercises an external module.
The build probes whether `dlopen` needs `-ldl`. Override `DL_LIBS`,
`PLUGIN_CFLAGS`, and `PLUGIN_LDFLAGS` for other POSIX toolchains or cross builds.
The default module flags are `-fPIC -dynamiclib` on Darwin and `-fPIC -shared`
elsewhere. Module filenames need not have a particular suffix.

The manifest CLI will expose this loader; the legacy lattice commands continue
to use their existing integer comparator.

## Independent image API

`src/temp_image.h` exposes normal result-returning planning, generation, and
verification functions, independent of both core and Unity. Layout, sparse
occupancy, expected record stream, comparison callback/context, and optional
topology constraints are separate borrowed descriptors. See `FORMAT.md` for
the precise native-byte-order image contract and supported geometry.

`temp_image_verify` always checks structural validity. Pass a record source to
compare every key/value byte, or NULL for a structural inspection. Pass an
exact shape only for pristine-fixture checks; otherwise any legal final shape
is accepted. Optional height/node/leaf constraints can be asserted individually.
Records are globally strict in comparator order, so equivalent duplicates are
rejected even across leaf boundaries. Child traversal independently determines
sibling order and detects repeated ownership, cycles, orphan blocks, bad free
lists, and inconsistent header totals.

`temp_image_generate` validates geometry and record ordering before creating
directories or a file, builds the image without calling core, and reuses an
existing image only after exact read-only verification. Failed owned outputs
are removed. An empty shape is height 0 with zero records and no overrides;
it is verified explicitly, never skipped.

Results distinguish invalid images (1), invalid specifications (2), I/O errors
(3), and resource exhaustion (4), with invariant, node, slot, record, detail,
and observed totals. Callers may translate these results into Unity assertions.
`make -C utils/temp check-image` compiles and runs the API without core objects.

Three stand-alone programs that build, copy and check deterministic tree
images for tests. Nodes are full by default; individual nodes can have explicit
key counts:

| Tool | Job |
| --- | --- |
| `bin/temp_gen` | build, or reuse, a template of a requested layout |
| `bin/temp_inst` | copy a template to an instance name, creating its directories |
| `bin/temp_verify` | walk a template (or a split instance) and check its shape |

They sit on top of the library like a unit does, but they are not a unit: they
are built into `bin/` by the root `make` and a test spawns them instead of
linking their sources, so one generator/verifier serves every test and can be
used from a shell. Split integration tests can use these tools to build full
descendants beneath an existing ancestor with space; this checkout currently
contains the cache and fflush test units.

## Building

From the repository root, as everything in this repo is run:

```sh
make            # every directory under test/ and utils/
make temp       # this utility alone
make -C utils/temp check  # overflow boundaries and generator/verifier regressions
```

`utils/temp/Makefile` includes `config.mk`, so the tools are compiled with the
same flags as a unit and link the same `obj/core/*.o` objects.  Only
`temp_verify` links Unity: its check is `temp_verify`, which asserts. The
`temp_full_*` C entry points remain available as full-tree compatibility wrappers.

`check` requires Python 3 and runs the C pointer/file-offset boundary tests and
the executable regression suites.  They cover generated record bytes at the
signed value boundaries, pointer and file-offset limits, rejection before
directory creation or reuse, large layout arithmetic, the unused step after
the final key, and small template generation, verification and reuse.  Set
`TEMP_GEN` to test another generator binary in the key/value suite, such as one
built with integer overflow sanitizers. The verifier suite covers missing and
duplicate inserted records, including duplicates across leaf boundaries, and
incorrect physical/header record totals in both lite and normal layouts. Set
`TEMP_VERIFY` to run the verifier suites against another verifier binary. Parent
link regressions cover every branch of a four-level tree, stale/zero/unreadable
parents, missing or duplicate child references, and a nonzero root parent in
both layouts. Independently encoded split images exercise the same checks when
an inserted record is expected.

Verifier overflow regressions cover inclusive key/value boundaries, unused
final steps, large lattice spans, positive and negative step overflow, and
wrapped values in the first and later leaves. They include split images with
boundary keys and arbitrary inserted values. Run the same suite with
`TEMP_VERIFY` pointing at a verifier built with undefined-behavior sanitizers
to check that valid boundary fixtures perform no overflowing intermediate
arithmetic. The core objects can remain uninstrumented.

The occupancy suite builds independent split images with correct record totals,
links and separators. It checks leaf and branch minima/maxima, odd and even
capacities, one-key leaves, and the root exception in lite and normal layouts.
It also rejects empty non-root nodes, extreme stored counts, forged parentless
nodes trying to claim the root exception, and inconsistent leaf/branch flags.

The shape suites cover asymmetric occupancy, canonical naming, reuse, invalid
selectors, sparse count arithmetic, and independently encoded inserted images
whose splits stop at an existing parent or below the root. `TEMP_GEN` and
`TEMP_VERIFY` select alternate binaries for these executable regressions too.

## Name convention

A template is

```
<DIR>/<lay_cnt>-<st>-<interval>.bptr
```

Custom occupancy adds a suffix before `.bptr`:

```
<DIR>/<lay_cnt>-<st>-<interval>--keys-root=4,0.1=3.bptr
```

`temp_path()` is the shared naming authority; `temp_full_path()` keeps the
original full-tree form. The verifier recovers the lattice and original shape
from either name. Override paths are sorted numerically, with ancestors before
descendants, and decimal numbers have no leading zeroes. Explicit full counts
remain in the suffix. Both filename component and full path limits are checked;
names are never silently truncated.

A trailing `/` of `<DIR>` is collapsed, so `bptr_files/temp/full/` and
`bptr_files/temp/full` name the same file.  The layout (`is_lite`, `node_size`)
is **not** part of the name, so two requests that differ only there share one
name; the generator refuses an existing file that does not match the request
instead of serving it silently.

## `temp_gen`

```
temp_gen [--dir DIR] [--norm] [--node-size N]
         [--node-keys PATH=COUNT]... LAY_CNT ST INTERVAL
```

Builds, or reuses, a tree of `LAY_CNT` levels:
`LAY_CNT` 1 is a single leaf, keys are `ST, ST + INTERVAL, ...` laid down left to
right, every value is the key times two, unspecified nodes are full, and every
level forms one global doubly linked list.

| Option | Meaning |
| --- | --- |
| `--dir DIR` | directory of the template (default `bptr_files/temp/full/`) |
| `--norm` | 8-byte child pointer layout (default lite, 4-byte) |
| `--node-size N` | size of a node in bytes (default 512) |
| `--node-keys PATH=COUNT` | key count of one node; repeat for multiple nodes |
| `-h`, `--help` | print the usage on stdout and exit 0 |

Options come before the three numbers; option scanning stops at the first
argument that is not one, so a negative `ST` is still read as the number it is.
An empty `--dir` is a usage error.  The parent directories of the template are
created as needed, also when `DIR` is absolute.

### Using `--node-keys PATH=COUNT`

`--node-keys PATH=COUNT` sets the **number of keys in one node**. Nodes you do
not specify remain full. The path starts at the root and follows zero-based
child positions:

| Override | Meaning |
| --- | --- |
| `root=4` | The root holds 4 keys. |
| `0=2` | The root's first child holds 2 keys. |
| `0.1=2` | The second child of the root's first child holds 2 keys. |

For a leaf, `COUNT` is its number of records. For an internal node, `COUNT`
keys means `COUNT + 1` children. The node's position in the requested tree
determines whether it is a leaf or an internal node.

Run these commands from the repository root. This two-level tree has a root
with room for one more key and full leaves:

```sh
./bin/temp_gen --node-size 128 --node-keys root=4 2 0 16
```

The final three arguments, `2 0 16`, mean two levels, starting key `0`, and
key interval `16`. With this lite 128-byte layout, internal nodes hold at most
5 keys and leaves hold at most 4 records. `root=4` therefore gives the root
5 children, all full leaves.

Repeat the option to control several nodes:

```sh
./bin/temp_gen --node-size 128 \
  --node-keys root=1 \
  --node-keys 0=2 \
  --node-keys 0.1=2 \
  3 0 16
```

This creates a three-level tree with two root children, three children under
internal node `0`, and two records in leaf `0.1`. All other nodes remain full.
Overrides are resolved together, so their option order has no effect. Paths
must exist after all overrides have been applied: for example, `root=1`
creates only children `0` and `1`, so an override for child `2` would be invalid.

Duplicate canonical paths, malformed numbers, paths beyond the tree height,
children removed by another override, and illegal occupancy are refused.
The root requires at least one key; other nodes use the same occupancy bounds
as the verifier. Syntax and duplicate errors exit 2; invalid shape requests
exit 1 before directories or files are created or reused.

The generated filename includes the overrides, so verification normally needs
no extra flags. For the first example above:

```sh
./bin/temp_verify bptr_files/temp/full/2-0-16--keys-root=4.bptr
```

For `temp_verify`, explicit `--node-keys` options replace the complete
filename-derived override set. These paths always describe the **original
fixture before insertion**. Custom inserted images also require the expected
final totals described in the [`temp_verify` section](#temp_verify).

### Generation and reuse

When `--dir` is omitted the tool says so before it does anything else, so a
caller that meant to pass a directory can see that it did not:

```
temp_gen: no --dir given; using the default bptr_files/temp/full/
```

It then prints one line per template:

```
temp_gen: <path>: written
temp_gen: <path>: reused
```

`: reused` means a complete template of that layout was already there and was
left untouched. For custom shapes, read-only reuse checks include original
record/node totals and every requested or default node count. A previously
modified instance cannot pass reuse merely by retaining the original file size.

Before creating directories or creating/reusing an image, the generator counts
the requested shape. A full tree has
`1 + brch.up + ... + brch.up^(LAY_CNT - 1)` nodes. Custom shapes are counted
through a sparse tree of overrides, aggregating untouched full subtrees
arithmetically rather than enumerating them.
It refuses a count above `UINT32_MAX` in lite mode or `UINT64_MAX` in normal
mode. Child pointers (the values of internal nodes), parent and sibling pointers
store **node indices**, not byte offsets; block 0 is the header, so the largest
node index is the total node count. It also refuses a full file size
`(node_count + 1) * node_size` above the library's signed file-offset maximum
(normally `INT64_MAX`), including the header block and final file position.
The preflight arithmetic itself uses division guards to avoid overflow and
reports whether the node-index range or file-offset range would be exceeded.

These checks guarantee representability only for generating this template.
Reaching a maximum exactly is valid: no spare nodes or space are required for
later insertions or splits. Users are responsible for ensuring that their
subsequent operations on a template do not overflow.

A negative `INTERVAL` is refused: the keys of a template have to ascend, because
the split of a full node and the verifier both search a node in the comparator's
order, so a descending lattice is not a tree they can serve.  The refusal happens
before a directory or a file is touched -- an image already sitting under that
name is left alone, not even reported as `reused` -- and it names the ascending
request that builds the same keys: `INTERVAL * -1`, starting at the last key of
the descending lattice, `ST + INTERVAL * (record_cnt - 1)`:

```
temp_full_generate: interval is negative: a template's keys must ascend
temp_full_generate: interval -2 descends; retry with interval 2 and st -79858 (interval * -1, from the last key of that lattice)
```

`record_cnt` is the number of records the requested shape holds (for full trees,
`leaf.up - 1` keys per leaf over `brch.up^(lay_cnt - 1)` leaves), so the suggested start is the last
key of the refused lattice and the two requests hold the same keys.  The numbers
are printed only when they can be computed: a layout that holds no tree, or an
`int64_t` overflow, falls back to the symbolic recipe.

Before creating directories or a template file, or reusing an existing image,
the generator checks the first key, the largest generated key
`ST + INTERVAL * (record_cnt - 1)`, and the range of their doubled values.
The count and endpoint calculations are bounded before each multiplication,
so the checks themselves cannot overflow.  An overflowing request exits 1
without creating a template and reports:

```
temp_full_generate: key/value overflow: generated keys or doubled values exceed int64_t
```

The limits are inclusive: a generated value may equal `INT64_MIN`, and the
largest possible doubled value is `INT64_MAX - 1`.  The unused interval step
after the final key need not fit.  These checks guarantee only that key/value
arithmetic during template generation does not overflow; they reserve no
capacity for later insertions or splits.  Users are responsible for ensuring
that their subsequent use of the template does not overflow.

| Exit | Meaning |
| --- | --- |
| 0 | the template exists |
| 1 | it could not be built; the reason is named on stderr as `temp_full_generate: <reason>`, with the path it was working on prefixed once there is one |
| 2 | the command line cannot be served |

## `temp_inst`

```
temp_inst SRC DST
```

Copies the template `SRC` to `DST`, creating every `/` separated parent
directory of `DST` (`EEXIST` tolerated).  `SRC` and `DST` are used as given,
relative to the working directory when they are relative -- there is no implicit
`bptr_files/` prefix.  `DST` is created `O_EXCL`: an existing destination is
never written to, and a copy that fails after the destination was created is
removed again, so a retry cannot mistake a truncated image for a complete one.

```
temp_inst: <DST>: copied
```

| Exit | Meaning |
| --- | --- |
| 0 | copied |
| 1 | `DST` already exists and was left untouched |
| 2 | usage |
| 3 | copy error; `temp_inst: <SRC> -> <DST>: <reason>` on stderr, the reason from `errno` |

## `temp_verify`

```
temp_verify [--lay-cnt N] [--st S] [--interval I]
            [--node-keys PATH=COUNT]... [--key K --val V]
            [--expect-height H --expect-nodes N --expect-leaves L] TEMPLATE...
```

Loads every `TEMPLATE` with `bptr_load(path, 256, &cmp_i64)` and walks it with
`temp_verify` inside the `RUN_TEST` frame its assertions need.  The check
covers the height, the root, the record and node counts, the leaf layer (one
global chain, `prev`/`next`/`parent`, occupancy, every key against the
`ST`/`INTERVAL` lattice, every value against `key * 2`) and every internal layer
(chain from both ends, `flags`, `level`, parent/child ownership, occupancy,
separator keys). The root's parent must be zero, including a single-leaf root.
Every other internal node must have an internal parent one level above it,
whose child array references that node exactly once. Every child reference of
an internal node, including child 0, must point to a node whose parent points
back to that owner. These checks apply to both full templates and split images.

A layout parameter that is not given is read off the file name.  A name that
does not conform is an error, not a default: the layout cannot be recovered
from the image itself.  The lattice may start at any key `ST`, not only at 0;
an `INTERVAL` of 0 is a parameter error (exit 2) because a lattice cannot step
by nothing. `--key`/`--val` (both or neither) tell the checker that one record
was inserted and require that record exactly once. For full-tree requests,
the original expectation of `lay_cnt + 1` levels and `lay_cnt + 1` added nodes
remains the default.

Occupancy is recovered from the filename even when all three lattice parameters
are supplied explicitly. If any `--node-keys` options are given, their complete
set replaces the filename-derived shape; they are never merged. These paths
always describe the original fixture, because child positions can change after
a split. An arbitrary instance filename can be checked by passing all lattice
parameters and the original occupancy overrides explicitly.

For a custom shape with an inserted record, all three `--expect-height`,
`--expect-nodes` and `--expect-leaves` options are required. They specify final
totals, not increments. The options must be given together and are only valid
with `--key`/`--val`. They are also available to override full-tree inserted
expectations. Without an inserted record, the checker requires exact requested
occupancy at every original path.

Independently, the verifier sums every leaf's record count and checks the
physical total and header record count against the original shape's calculated
record total, plus one for an inserted record. It compares the expected node
total with both the header and the actual traversal. These calculations use
checked arithmetic and do not depend on the key lattice walk or header counts.

Lattice-key addition and doubling an original key are checked before they are
evaluated; an unrepresentable key or doubled value fails verification (exit 1).
The expected key advances only when another original record is visited, so an
unused step after the final key need not fit `int64_t`. Inserted records do not
advance the original lattice, and their values are checked against `--val`.
Leaf and node totals are checked against the requested geometry using checked
unsigned arithmetic, without multiplying counts by `INTERVAL` or subtracting
signed key endpoints. A valid template whose intermediate count-times-interval
product would exceed `int64_t` is therefore still accepted.

Every visited node is checked for legal occupancy, including split instances.
If a leaf's maximum is `L = leaf.up - 1`, a non-root leaf must hold between
`ceil(L / 2)` and `L` records. A branch's maximum is `B = brch.up` children;
a non-root branch must hold between `ceil(B / 2) - 1` and `B - 1` keys.
The root of these nonempty images may hold as few as one key. These are legal
bounds rather than a requirement for one exact split distribution. Pristine
templates also undergo exact requested occupancy and record-total checks.
The verifier reads each node's persisted count before fetching it through the
library, because decoding an overfull node could otherwise overrun its arrays.
It also requires every visited node's `VALID` flag and checks that its `LEAF`
flag agrees with its level, including both leaves produced by a split.

The name reader accepts noncanonical decimal spellings such as
`01-0-16.bptr` and normalizes shape selectors. When the name cannot supply the
request, pass `--lay-cnt`/`--st`/`--interval` and any occupancy overrides
explicitly. A required name that does not conform ends the run there, so the
templates listed after it are not checked at all.

```
temp_verify: <path>: ok
```

one line per template, printed only after that template's Unity case passed.

| Exit | Meaning |
| --- | --- |
| 0 | every template verified |
| 1 | at least one check failed (Unity output on stdout) |
| 2 | the command line cannot be served, or a name does not conform -- the rest of the list is not checked |
| 3 | a template cannot be loaded -- it was not checked, and `bptr_errno` is printed; this outranks a failed check (1) in the same run |

A run in which nothing could be loaded prints no Unity output at all; a run in
which at least one template was checked ends with the usual Unity summary.  A
load failure and a check failure in one run therefore exit 3, with the failed
check still reported in the Unity output on stdout.

## Example

```sh
rm -rf /tmp/tpl
./bin/temp_gen --dir /tmp/tpl 1 0 16                    # written,  exit 0
./bin/temp_gen --dir /tmp/tpl 1 0 16                    # reused,   exit 0
./bin/temp_gen --dir /tmp/tpl 3 1004 -2                 # refused,  exit 1 (negative interval)
./bin/temp_gen --dir /tmp/tpl 3 -79858 2                # the ascending request it suggests
./bin/temp_verify /tmp/tpl/1-0-16.bptr                  # ok,       exit 0
./bin/temp_verify --lay-cnt 1 --st 0 --interval 16 /tmp/tpl/1-0-16.bptr   # ok
./bin/temp_inst /tmp/tpl/1-0-16.bptr /tmp/tpl/inst/a.bptr   # copied, exit 0
./bin/temp_inst /tmp/tpl/1-0-16.bptr /tmp/tpl/inst/a.bptr   # exit 1
```

For a leaf split absorbed by an existing parent, leave the root one key short
of full and keep its leaves full. With 128-byte nodes, a lite branch holds at
most five keys and a normal branch holds at most three:

```sh
./bin/temp_gen --dir /tmp/occupancy/lite --node-size 128 --node-keys root=4 2 0 16
./bin/temp_gen --dir /tmp/occupancy/normal --norm --node-size 128 --node-keys root=2 2 0 16
./bin/temp_verify /tmp/occupancy/lite/2-0-16--keys-root=4.bptr
./bin/temp_verify /tmp/occupancy/normal/2-0-16--keys-root=2.bptr
```

For a partial cascade absorbed below the root, use four levels and reduce only
child 0 of the root. Its full leaf and full leaf-parent can split into that
existing grandparent without changing the root:

```sh
./bin/temp_gen --dir /tmp/occupancy/lite --node-size 128 --node-keys 0=4 4 0 16
./bin/temp_gen --dir /tmp/occupancy/normal --norm --node-size 128 --node-keys 0=2 4 0 16
./bin/temp_verify /tmp/occupancy/lite/4-0-16--keys-0=4.bptr
./bin/temp_verify /tmp/occupancy/normal/4-0-16--keys-0=2.bptr
```

After a separate test inserts key `1` with value `2` into a copy of the
two-level lite fixture, the final height is 2, node count 7, and leaf count 6:

```sh
./bin/temp_verify --lay-cnt 2 --st 0 --interval 16 --node-keys root=4 \
  --key 1 --val 2 --expect-height 2 --expect-nodes 7 --expect-leaves 6 \
  /tmp/occupancy/instance.bptr
```

The utilities generate and check these shapes; they do not perform the split.

## C entry points

`temp_shape.h` defines `struct temp_shape`, `struct temp_shape_counts`, and
`struct temp_verify_expect`. Zero-initialize a shape, add overrides with
`temp_shape_add`, then use `temp_shape_count`, `temp_path`, `temp_generate`, or
`temp_verify`. `temp_parse_name` recovers the original request from a name.
Release shape storage with `temp_shape_free`. Shape pointers passed to the
generator and verifier are borrowed; their callers retain ownership. The
verifier expects a Unity assertion frame. Existing `temp_full_path`,
`temp_full_generate`, and `temp_full_verify` retain their signatures and
full-tree behavior.

## Limits

- The generator serves `lay_cnt` 1..254: one node stays pinned per level while
  its subtree is filled and the cache holds 256 slots. Full-tree image size grows as
  `brch.up^(lay_cnt - 1)`, so realistic heights are 1..4 -- for the default
  lite 512-byte layout (`leaf.up` 29, `brch.up` 38) 1/2/3 levels produce
  1/39/1483 nodes and 1 024/20 480/759 808 bytes.
- `node_size` must be at least `64 + PTR + 2 * (8 + PTR)` (92 lite, 104 normal),
  the smallest node `bptr_init` accepts.  A request that cannot be served is
  refused before a file is created.
- The verifier's walk uses the 256-slot cache the tools load with; a template
  taller than the generator serves cannot be walked with it.
- Loading an image **mutates** it: `bptr_unload` rewrites block 0 from the
  library's shared scratch buffer, so the bytes past the header end up holding
  the last node image that went through it.  The header and every node block are
  stable; do not pin a template by the hash of the whole file.
- The verifier's check is a Unity assertion, so a defect longjmps out of the
  case and leaves the loaded image to the process exit.
