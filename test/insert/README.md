# Insertion unit

This unit tests `bptr_insert` against a test-owned, copied reference mapping.
It checks the return code and caller buffers, reads the live tree immediately
after each ordinary insertion or duplicate attempt, and verifies complete
persisted contents and structure after a successful unload. It never calls
`bptr_find` or `bptr_find_range`, and its expectations do not depend on the
insertion implementation or one particular split distribution.

From the checkout root:

```sh
make insert
./bin/insert
./bin/insert --list
./bin/insert --case fixtures
./bin/insert --case permutations --layout normal
./bin/insert --case growth --seed 12345 --work-dir .cache/insert-replay
make -C test/insert check-oracle
make -C test/insert check-harness
make -C test/insert check
```

No input directory or prebuilt templates are required. The default work
directory is `.cache/insert`; each scenario creates a private `run-XXXXXX`
directory beneath it. Successful scenarios remove only their own files and
directory. Existing work directories and unrelated files are left alone.
After switching branches or core/header revisions, use `make clean` before
building: the repository's shared core-object rules do not track headers or
the revision that produced an existing object.

`--case NAME` selects one group, or `all` selects every group; `all` is the
default. Each group is registered separately for normal and lite layouts, so
a failure in one layout does not skip the other. `--layout normal|lite|both`
restricts this selection. Unity stops the remaining subscenarios of a failed
group/layout; the summary reports actual execution rather than planned coverage.
`--list` prints the available names without running tests. `--seed`
accepts an unsigned 64-bit integer in the C decimal/hexadecimal/octal notation
accepted by `strtoull` with base zero. The seed printed at startup controls
the shuffled workloads. Unknown groups, invalid arguments, setup failures,
and assertion failures produce a nonzero exit status. The final summary
reports the scenarios and insertion attempts actually executed, including
successful inserts, duplicates, expected resource errors, live checks, and
completed persistence checkpoints. Live-check totals include failed
observations. There is no empty-template-directory success path.

## Two independent observations

`src/oracle.c` maintains a sorted vector of copied key/value records. Equality
is defined by the configured comparator returning zero, and the first inserted
value wins. The model is updated from the intended operations, never exported
from the observed tree. Numeric test comparators use `memcpy` and relational
comparisons, avoiding alignment requirements and subtraction overflow.

The live observer traverses root child links using `struct bptr`,
`struct bptr_node`, `bptr_node_fetch`, and `bptr_node_unload`. It does not use
the search implementation to decide where records should be. Each fetched
node is copied into test-owned storage and its reference released before the
next fetch. The observer checks complete record bytes and order against the
model, metadata totals, legal occupancy, levels, separators, reciprocal
parent/child ownership, and sibling chains. It also rejects duplicate or
cyclic ownership. It does not explicitly flush a node or unload the tree.

Most scenarios use a 4096-slot cache so their complete trees fit in memory.
Their per-call live checks can inspect dirty state before a persistence
checkpoint. A fetch in a small cache can itself evict and flush a dirty node;
the minimum-cache and eviction scenarios therefore test cache integration,
without claiming that the entire observed tree remains unflushed. A later
successful persisted check cannot override an earlier live failure.

Persistence checks directly call the core-free `temp_image_verify` C backend
from `utils/temp/src/temp_image.c`, with record and comparator callbacks backed
by the independent model. The backend opens the image read-only and checks
complete contents and structure without library loading or decoding. Final
verification accepts any topology permitted by the documented format; no
exact split midpoint, allocation order, height increment, or node-count
increment is inferred from insertion. Fixture generation uses the same
independent backend's generator and verifies the pristine requested shape
before loading it. It never builds those fixtures by calling `bptr_insert`.

The unit links this backend directly. It does not spawn `temp_gen`,
`temp_inst`, or `temp_verify`, and their executables are not runtime
prerequisites. See [the manifest API](../../utils/temp/MANIFEST.md) and
[image format](../../utils/temp/FORMAT.md).

## Coverage

Every group exercises both lite and normal layouts.

| Group | Scenarios |
| --- | --- |
| `empty` | Initialized and independently generated rootless images; empty unload/reload, first root creation, input overwrite, duplicate rejection, and continued growth. |
| `permutations` | All permutations of four keys; duplicates against the full root leaf; reopen, grow, and retry existing keys. |
| `growth` | Ascending, descending, alternating-end, and seeded shuffled insertions; repeated duplicates; multiple checkpoints; one-record leaves. |
| `comparators` | Ascending and descending signed order with non-unit comparison results; signed integer extremes and values whose numeric and byte orders differ. |
| `binary` | Key/value widths 1/1, 3/7, 7/3, and 9/1; ascending and descending lexicographic order; embedded NULs; zero and `ff` records; common-prefix keys; independent values. |
| `equivalence` | Byte-distinct keys with equal two-byte prefixes, ascending and descending prefix order, and an all-equivalent comparator; first-value preservation across growth and reopening. |
| `ownership` | Unaligned guarded buffers, input immutability, separate key/value overwrite and release, exact-sized allocations, allocation reuse, and identical or overlapping key/value storage. |
| `fixtures` | Independently generated full trees, parents or higher ancestors with space, asymmetric occupancy, and tall trees with one-record leaves. Heights 1 and 2 exhaust insertion gaps in the selected full and parent-room shapes; taller shapes use explicit boundary/interior samples. |
| `cache` | Minimum two-slot cache for root insertion and cold searches with room, plus a working set larger than a 16-slot cache. |
| `cache-errors` | Pins block root and descendant fetches, followed by cache-slot reuse checks. A separate full-leaf case permits search under the pins but blocks allocation needed by insertion. |
| `io-errors` | Cold loads of disposable images truncated before a root or target descendant, checking propagated read errors. |

The harness snapshots both caller buffers around every insertion attempt and
checks they were not modified. It deliberately seeds `bptr_errno` with stale
errors; only the returned integer determines the insertion verdict. Duplicate
attempts use replacement values different from the originals and retain the
model's original key/value bytes.

Resource-error probes check exact expected return codes after arranging the
failure. They do not impose a general rollback guarantee, compare an arbitrary
post-error mapping with the previous state, or infer a successful retry policy
from the public contract. Intentionally truncated images are discarded after
their expected failures. The minimum-cache success scenarios avoid assuming
that a cache of two can support arbitrary split cascades.
Reacquiring cache slots after a deliberately blocked search is a supplementary
reference-lifecycle check, rather than a generic post-error usability guarantee.

## Failure evidence and checker checks

An unexpected failure retains the scenario directory and reports its path.
Its files include:

- `operations.jsonl`: case, seed, cache setting, and each attempted key/value
  with expected and actual return codes;
- `expected.json`: the intended mapping from the reference model;
- `initial.json`: the original shape and records for generated fixtures;
- `failure.txt`: the failing invariant or return-code comparison, operation
  number, and seed;
- `instance.bptr`: the owned image for further inspection, when creation
  succeeded.

Files are created as their corresponding stage is reached, so a setup failure
may leave only a subset. Teardown releases tracked references and attempts to
unload the tree. That unload may flush or otherwise change the retained image;
for a live-state failure, `failure.txt` and the operation log record the primary
evidence at the time of failure. A persisted image alone cannot reconstruct
every transient cache defect.

`check-oracle` runs 78 isolated reference-model and live-observer observations,
including deliberately malformed hand-built graphs and reference balancing.
`check-harness` also checks CLI validation, successful-case cleanup, preservation
of unrelated files, seed replay, and diagnostic artifacts. It compiles separate
tester objects that redirect public calls through test-owned adversaries;
existing core objects remain untouched. The adversaries cover wrong return
codes, falsely successful duplicates, caller-buffer writes, live count/value
damage, and persisted-only payload damage. The live adversaries are repaired
during unload, proving that the earlier live check still fails when the final
image would pass. Stale-errno checks cover success, duplicates, and real
resource errors.

`check` runs those checker regressions, then the actual insertion executable.
It fails when the implementation violates the contract; a passing checker
regression does not convert a conformance failure into success. See
[the observed failure](FINDINGS.md) for a four-insertion reproducer found in
the current core.

The new unit sources and independent image backend are compiled as
C99 with the repository warning flags and `-Werror`; core objects keep the
shared build configuration. These checks validate the tester as well as the
library, and do not replace its insertion verdicts.

The conformance scenarios use initialized instances, valid fixed-width
buffers, and consistent comparators. Invalid pointers, inconsistent
comparators, concurrent mutation, general crash recovery, and undocumented
post-error atomicity are outside the assertions made here. This unit adds no
implementation changes to the core library.
