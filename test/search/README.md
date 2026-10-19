# Search unit

`bin/search [TEMPLATE_DIR]` tests `bptr_find_node` with Unity. Run it from the
repository root, where the unit can find `bin/temp_inst` and `bin/temp_verify`.
The unit generates no templates; prepare the inputs first:

```sh
make clean
make search temp
./bin/temp_gen --dir .cache/search-inputs 1 -11 16
./bin/temp_gen --dir .cache/search-inputs 2 -11 16
./bin/temp_gen --dir .cache/search-inputs 3 -11 16
./bin/temp_gen --dir .cache/search-tall --node-size 112 5 17 2
./bin/search .cache/search-inputs
./bin/search .cache/search-tall
make -C test/search check
```

Always clean before building after a branch, submodule or header change: the
shared build rules do not track header dependencies. No `BPTR_TESTING` define
or core changes are required. The unit uses the existing internal declaration
of `bptr_find_node`, whose returned leaf reference belongs to its caller.

Without an argument, the unit announces and scans `bptr_files/temp/full/`.
A missing, unreadable or empty directory warns and exits successfully without
running cases. Check the Unity summary to distinguish this from a passing run.
Every `.bptr` name must be canonical, including any `--keys-` occupancy suffix;
a malformed name fails before cases start. Other filename extensions are
ignored. See [the template tools](../../utils/temp/README.md) for input naming,
layout options and occupancy overrides.

## Checks

For each template, five Unity cases cover exhaustive hits, insertion gaps,
returned-reference ownership, cache exhaustion and read failures. One additional
case tests newly initialized empty trees in both pointer layouts. A successful
run over `N` templates reports `1 + 5*N` tests, zero failures and zero ignored.

The oracle descends through child zero to the first leaf and follows sibling
links, recording each leaf index, its actual occupancy and its cumulative record
offset. After whole-file verification, checked arithmetic derives expected keys
and values from the input lattice. It does not reproduce the search function's
branch routing. The search handle is reopened after collecting this oracle, so
the exhaustive walk begins with a cold cache and uses capacity two.

Every stored key must return the expected leaf, index, key and value. Misses
cover the two integer boundaries of each open gap, deduplicating a single
integer gap, plus the positions immediately before/after the lattice and the
`INT64_MIN`/`INT64_MAX` extremes. Consecutive keys have no internal gap, so an
interval-one input still checks all hits and the applicable exterior misses.
An exact separator must route right; the missing key immediately below the next
leaf's minimum must return the previous leaf with index equal to its key count.
This exhausts keys and insertion gaps, rather than every integer in a wide gap.

The ownership case fetches aliases and keeps one returned reference pinned
while searches churn the small cache. Cache-failure probes pin unrelated entries
to block the root or a descendant, require a NULL result and the fetch error,
then release the blockers and check recovery. Read-failure probes separately
truncate disposable copies at the root and descendant blocks. Empty-tree and
fetch-failure results inspect only the returned node and error code; other
result fields are unspecified on failure. Warm searches also start with stale
`bptr_errno` and must still return the correct referenced leaf.

## Copies, output and failures

`temp_inst` copies every caller template before the unit opens or verifies it.
All verification uses private copies and explicit lattice/occupancy arguments.
This matters because loading and unloading a tree can rewrite the unused tail
of its header block. Caller inputs remain unchanged byte for byte.

Each scenario owns `bptr_files/temp/search/run-XXXXXX/instance.bptr`. Successful
search instances are unloaded, verified and removed before the next is copied.
The intentionally truncated read-failure images are discarded after their
expected failure. Unity teardown releases tracked node references and handles
even when an assertion aborts a case. Unexpected failures retain their copy and
print `search: retained failed instance <path>` for inspection.

Redirected output includes one `hits` and one `misses` verdict per template,
with the number of queries actually executed, plus scenario totals for the
other cases. Terminal progress updates occur per leaf or phase. Successful tool
output is quiet; failed tool output is replayed. A mismatched result names the
template and query, then gives expected and actual leaf/index/found status.

The live printer is shared with other test units in `test/helper/progress.{c,h}`.
Include `progress.h` directly or through `test_util.h` to use `progress_update`,
`progress_done` and `progress_break`; the common build rules compile and link it
automatically.

## Isolated integration driver

`make -C test/search check` runs
`python3 test/search/checks/integration.py`. It uses a temporary working directory
and real tools, leaving existing fixtures untouched. The matrix includes both
pointer widths, full default-size heights 1–3, compact trees through height 8,
asymmetric occupancy, one-key roots/leaves, negative and nonzero starts,
intervals 1/2/16 and inclusive doubled-value limits.

The driver independently decodes persisted leaves to assert the hit/miss query
counts, checks Unity case totals, hashes every source image before and after,
and instruments tool calls to prove that successful instances disappear before
the next copy. It also checks default/empty/missing directories, malformed
names, absent tools, injected initial/final verifier failures and retained
failed copies. A separately compiled search-index mutant in the temporary
directory proves that wrong-result diagnostics identify the query and both
results; tracked core sources remain unchanged.

## Sanitizer finding

The new unit sources pass `-Wall -Wextra -Werror` and UBSan when linked with
ordinary core objects, including signed-value boundaries and compact height-8
trees. Instrumenting the entire core currently stops during empty-tree creation:
`core/src/bptr_io.c:291` writes normal-layout `uint64_t` header fields at an
unaligned address. This existing core issue is outside the test branch; no core
changes or sanitizer suppressions are included.
