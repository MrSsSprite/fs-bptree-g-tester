# Analysis of insertion failures

The 16 failing group/layout registrations share a leaf-capacity boundary
signature. An insertion that fills a previously nonfull leaf and shifts
existing keys changes previously stored value bytes while returning success.
The observations strongly support one shared defect class; they do not prove
that there is only one implementation defect.

Analysis used tester revision `d275504f0ce37f64634812857ab5d69716ae3ef6`
and core revision `78594b5d639cb4df9da106db00e5524471637cfa`. The insertion
implementation was neither inspected nor changed. Conclusions below come
from valid insertion sequences, internal live observations, independent image
decoding, and controlled changes to unused internal storage.

## What the current run establishes

The rerun reports 22 Unity groups, 16 failures, and no ignored groups. It
executes 38 scenarios, 868 insertion attempts, 920 live observations, and 32
successful persistence checkpoints. Empty-tree cases and all ten arranged
cache/read-error probes pass.

For every one of the 16 retained failed images:

- the last call inserts a new key and returns `BPTR_E_SUCCESS`;
- the complete expected key stream and record count are preserved;
- exactly one old value differs in these particular fixtures;
- independent structure-only verification passes;
- independent complete-content verification fails on `record-value`;
- the value mismatch is observed live and remains after unload.

The successful return therefore conceals persistent data loss. A structurally
valid tree, correct key searches, or correct record counts cannot detect it.

## Shared trigger and affected records

Let `C = floor((node_size - 64) / (key_size + value_size))`, the documented
leaf capacity. Every observed final insertion changes the selected leaf from
`C - 1` records to `C` records and inserts before at least one existing key.
These final insertions do not require a split.

Normal and lite results are combined below when their evidence is identical.
Binary keys are shown as hexadecimal bytes.

| Group / layout | Failing call | New key | Leaf occupancy | Old value damaged |
| --- | --- | --- | --- | --- |
| Permutations / both | 4 | `5` | 3 to 4 | Key `-2`: 8 bytes become zero |
| Growth / both | 5 | `304` | 3 to 4 | Key `308`: 8 bytes become zero |
| Numeric extremes / both | 4 | `-1` | 3 to 4 | Key `INT64_MIN`: 8 bytes become zero |
| Binary / normal | 30 | `2f` | 15 to 16 | Key `1d`: value `1e` becomes `00` |
| Binary / lite | 27 | `21` | 15 to 16 | Key `00`: value `00` becomes `26` |
| Prefix equivalence / both | 11 | `003d12` | 5 to 6 | Key `000012`: first 3 of 7 bytes become zero |
| Ownership / both | 6 | `800005` | 5 to 6 | Key `000000`: first 3 of 7 bytes become zero |
| Fixtures / both | 4 | `-519` | 3 to 4 | Key `-520`: 8 bytes become zero |
| Cache / normal | 4 | `0` | 3 to 4 | Key `88`: 8 bytes become zero |
| Cache / lite | 7 | `60` | 3 to 4 | Key `44`: value becomes `2c01000000000000` |

The last cache value is the native representation of an existing key, `300`.
The binary-lite replacement `26` is also an existing key. Thus this is not
simply unconditional zeroing: the replacement can contain stale key data.

The group names do not identify separate causes:

- The fixture cases successfully split and checkpoint before failing during
  the next insertion into a nonfull leaf.
- The cache cases fail with only one or three live nodes in a 16-slot cache,
  before their intended eviction-pressure workload has developed.
- Ownership fails inside the ordinary insertion check, before that call's
  subsequent source-buffer mutation checks.
- None of the failing final operations is a duplicate rejection.
- The comparator groups fail before their later descending-order cases run.

## Controlled boundary experiments

Fresh trees were populated by ascending appends, checked before the target
operation, then given a new key at each tested insertion position. These
experiments use internal leaf reads, not the public lookup APIs.

A 2,940-trial matrix covered compact-node occupancies and larger-node boundaries
using both pointer layouts, 128/512-byte nodes, key/value widths 1/1, 3/7, 8/8
and 8/16, and unit/non-unit comparator results. All keys
and return codes were correct; 508 trials had wrong values. Across this
matrix, the value failure occurred exactly when the target insertion filled
the last available slot and was not an append. A separate 256-trial native
`int64_t` control produced the same condition, including 120 value failures.

An additional 1,236-trial matrix included wider keys than values, 7/3 and 9/1,
as well as 96-byte nodes. All 122 value failures followed the same condition.
This extension shows that more than one old value can be damaged:

| Key/value width | Example effect when prepending into the last free slot |
| --- | --- |
| 3/7 | First 3 bytes of the first old value change |
| 8/16 | First 8 bytes of the first old value change; last 8 remain correct |
| 7/3 | Two complete old values and the first byte of a third change |
| 9/1 | All five old one-byte values in a six-record leaf change |

For 512-byte nodes with 8/8 records, capacity is 28: insertions leaving at
most 27 records pass, non-append insertions reaching 28 fail, and an append
reaching 28 passes. The boundary follows capacity rather than the literal
record counts in the original small-node cases.

## Source of the replacement bytes

Internal observations show `node->vals - node->keys == C * key_size` for
these leaf layouts. Immediately before the failing insertion, slot `C - 1`
is allocated key storage but does not contain a live key.

A second 2,940-trial experiment marked only the first unused key slot, at
index `old_count`, with `a5` bytes, without changing any live key or value.
The same 508 boundary cases
then placed those `a5` bytes in old values; all other cases preserved their
expected values. For the tested key widths no larger than values, the result
matches replacing the first `key_size` bytes of the old value stream with
that unused-slot data, followed by insertion of the new value at its proper
position. Prepending moves the damaged first old value to slot 1; interior
insertion leaves it at slot 0.

This is strong evidence that unused key storage crosses the key/value boundary
during key movement. A shift equivalent to moving `old_count - index + 1`
keys, instead of the `old_count - index` live keys, would explain the observed
condition and byte provenance. This is a diagnostic inference, not a verified
source expression. The exact implementation location remains unexamined.

## Minimal reproductions

Only two insertions are needed with 128-byte nodes, 8-byte native signed keys,
16-byte values, an ascending comparator, and ample cache. Both layouts show
the same result:

| Call | Key | Input value bytes |
| --- | --- | --- |
| 1 | `20` | `404142434445464748494a4b4c4d4e4f` |
| 2 | `10` | `eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee` |

Both calls return zero. The keys correctly become `[10, 20]`. The value for
key `20` becomes `000000000000000048494a4b4c4d4e4f`. Capacity is two, so this
is a single root leaf with no split, reload, duplicate, or caller-buffer
mutation involved.

The existing unit reproduces the same defect with its original 8/8 layout:

```sh
make insert
bin/insert --case permutations --layout normal
```

Scenario `permutation-0-1` inserts `-2, 0, 17, 5`, with all four returns zero.
After the fourth call, the value for `-2` becomes eight zero bytes instead of
`c900dac300807700`. The other three records retain their expected bytes.

Retained case directories contain the operation history, expected manifest,
live failure diagnostic and persisted image. Independently check both aspects:

```sh
make temp
bin/temp_verify --expect CASE_DIR/expected.json CASE_DIR/instance.bptr --json
bin/temp_verify --expect CASE_DIR/expected.json --check structure CASE_DIR/instance.bptr --json
```

The first command fails on contents; the second passes. Teardown may flush
the image, so the recorded live failure remains separate evidence.

## Limits and next investigation

The next implementation review should inspect key-shift bounds when a leaf's
last free slot is filled, including whether a count is taken before or after
incrementing occupancy. Regression coverage should explicitly cross all
insertion positions, capacity boundaries, both layouts, and key widths both
smaller and larger than value widths. Nonzero distinguishable payloads are
needed because a zero replacement can otherwise look correct.

An early failure aborts the remaining subscenarios of its group/layout.
Consequently these results do not establish that later ownership, comparator,
eviction, or tall-tree scenarios pass. Other defects may be masked by this
shared boundary failure. The controlled experiments isolate the current
observable failure; they do not establish an implementation fix.
