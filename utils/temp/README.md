# Template utilities

Three stand-alone programs that build, copy and check the perfectly full tree
images the split tests are driven with:

| Tool | Job |
| --- | --- |
| `bin/temp_gen` | build, or reuse, a template of a requested layout |
| `bin/temp_inst` | copy a template to an instance name, creating its directories |
| `bin/temp_verify` | walk a template (or a split instance) and check its shape |

They sit on top of the library like a unit does, but they are not a unit: they
are built into `bin/` by the root `make` and a test spawns them instead of
linking their sources, so one generator/verifier serves every test and can be
used from a shell.  `test/node_split/src/temp_full.{h,c}` still holds its own
copy of the generator, copier and verifier on this branch; the unit is switched
over to these tools -- and drops that copy -- on `test/n/sp/refactor/rm_temp`,
which the merge reconciles.

## Building

From the repository root, as everything in this repo is run:

```sh
make            # every directory under test/ and utils/: three units + three tools
make temp       # this utility alone
make -C utils/temp check  # overflow boundaries and generator/verifier regressions
```

`utils/temp/Makefile` includes `config.mk`, so the tools are compiled with the
same flags as a unit and link the same `obj/core/*.o` objects.  Only
`temp_verify` links Unity: its check is `temp_full_verify`, which asserts.

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

## Name convention

A template is

```
<DIR>/<lay_cnt>-<st>-<interval>.bptr
```

`temp_full_path()` (`src/temp_full.c`) is the single authority for that name: the
generator writes it and the verifier reads `lay_cnt`/`st`/`interval` back off it.
A trailing `/` of `<DIR>` is collapsed, so `bptr_files/temp/full/` and
`bptr_files/temp/full` name the same file.  The layout (`is_lite`, `node_size`)
is **not** part of the name, so two requests that differ only there share one
name; the generator refuses an existing file that does not match the request
instead of serving it silently.

## `temp_gen`

```
temp_gen [--dir DIR] [--norm] [--node-size N] LAY_CNT ST INTERVAL
```

Builds, or reuses, the perfectly full tree of `LAY_CNT` levels:
`LAY_CNT` 1 is a single leaf, keys are `ST, ST + INTERVAL, ...` laid down left to
right, every value is the key times two, every leaf and every internal node but
the last child is full, and every level forms one global doubly linked list.

| Option | Meaning |
| --- | --- |
| `--dir DIR` | directory of the template (default `bptr_files/temp/full/`) |
| `--norm` | 8-byte child pointer layout (default lite, 4-byte) |
| `--node-size N` | size of a node in bytes (default 512) |
| `-h`, `--help` | print the usage on stdout and exit 0 |

Options come before the three numbers; option scanning stops at the first
argument that is not one, so a negative `ST` is still read as the number it is.
An empty `--dir` is a usage error.  The parent directories of the template are
created as needed, also when `DIR` is absolute.

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
left untouched.

Before creating directories or creating/reusing an image, the generator counts
the perfect tree's nodes as `1 + brch.up + ... + brch.up^(LAY_CNT - 1)`.
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

`record_cnt` is the number of records that layout holds -- `leaf.up - 1` keys per
leaf over `brch.up^(lay_cnt - 1)` leaves -- so the suggested start is the last
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
temp_verify [--lay-cnt N] [--st S] [--interval I] [--key K --val V] TEMPLATE...
```

Loads every `TEMPLATE` with `bptr_load(path, 256, &cmp_i64)` and walks it with
`temp_full_verify` inside the `RUN_TEST` frame its assertions need.  The check
covers the height, the root, the record and node counts, the leaf layer (one
global chain, `prev`/`next`/`parent`, fullness, every key against the
`ST`/`INTERVAL` lattice, every value against `key * 2`) and every internal layer
(chain from both ends, `flags`, `level`, parent/child ownership, fullness,
separator keys). The root's parent must be zero, including a single-leaf root.
Every other internal node must have an internal parent one level above it,
whose child array references that node exactly once. Every child reference of
an internal node, including child 0, must point to a node whose parent points
back to that owner. These checks apply to both full templates and split images.

A layout parameter that is not given is read off the file name.  A name that
does not conform is an error, not a default: the layout cannot be recovered
from the image itself.  The lattice may start at any key `ST`, not only at 0;
an `INTERVAL` of 0 is a parameter error (exit 2) because a lattice cannot step
by nothing.  `--key`/`--val` (both or neither) tell the checker that one record
was inserted -- how a split instance is checked: it then expects `lay_cnt + 1`
levels and requires that record exactly once. Independently, it sums every
leaf's record count and checks the physical total and the header record count
against `(leaf.up - 1) * brch.up^(LAY_CNT - 1)`, plus one when an inserted record
is expected. Computing that expected total uses checked arithmetic and does
not depend on the number of leaves visited, the key lattice walk or the header
record count.

Lattice-key addition and doubling an original key are checked before they are
evaluated; an unrepresentable key or doubled value fails verification (exit 1).
The expected key advances only when another original record is visited, so an
unused step after the final key need not fit `int64_t`. Inserted records do not
advance the original lattice, and their values are checked against `--val`.
Leaf and node totals are checked against the full-tree geometry using checked
unsigned arithmetic, without multiplying counts by `INTERVAL` or subtracting
signed key endpoints. A valid template whose intermediate count-times-interval
product would exceed `int64_t` is therefore still accepted.

The name is read as three decimal numbers, so a spelling the generator would
never write -- `01-0-16.bptr` -- is accepted as well.  A caller that needs the
canonical `<lay_cnt>-<st>-<interval>.bptr` name (the split unit's directory
loader is one) should pass the layout explicitly with
`--lay-cnt`/`--st`/`--interval` instead.  A name that does not conform ends the
run there, so the templates listed after it are not checked at all.

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

## Limits

- The generator serves `lay_cnt` 1..254: one node stays pinned per level while
  its subtree is filled and the cache holds 256 slots.  Image size grows as
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
