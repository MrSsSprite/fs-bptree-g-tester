# Manifest-driven fixtures and verification

Manifest mode uses a C99/POSIX read-only image backend with no core or Unity
dependency. The existing lattice CLI and C entry points retain their behavior.
In particular, the legacy verifier still loads through the library; use
manifest mode when input immutability and independent decoding are required.

From the checkout root:

```sh
make temp
bin/temp_gen --spec utils/temp/examples/binary.json --out .cache/binary.bptr
bin/temp_verify --expect utils/temp/examples/binary.json .cache/binary.bptr
bin/temp_verify --expect utils/temp/examples/binary.json .cache/binary.bptr \
    --check contents,structure,shape --json
```

Generation requires `initial_shape`. It validates the specification before
creating output directories, constructs nodes without calling `bptr_insert`,
and verifies the completed image. An existing output is reused only after
exact contents/shape verification, and is never overwritten on mismatch.
Choose explicit output paths for different layout/data/comparator requests.

Verification always validates structure. Its default is `contents,structure`:
compare all expected record bytes, with any legal final topology. `shape`
additionally checks the exact `initial_shape`; use it for pristine fixtures.
`--check structure` skips content comparison. Optional `expect` totals are
always applied individually. Verification opens the image read-only and never
flushes or unloads a tree. Use a successful persistence checkpoint in the
tester before verifying a changed tree. Dirty in-memory state needs a separate
live observer; this utility does not silently flush it.

## Schema 1

```json
{
  "schema": 1,
  "byte_order": "native",
  "layout": {
    "lite": true, "node_size": 128, "key_size": 3, "value_size": 2
  },
  "comparator": {"profile": "bytes-asc"},
  "initial_shape": {"height": 1, "node_keys": {"root": 2}},
  "records": [
    {"key_hex": "000001", "value_hex": "00ff"},
    {"key_hex": "000100", "value_hex": "f000"}
  ],
  "expect": {"height": 1}
}
```

`schema`, `layout`, `comparator`, and `records` are required. `initial_shape`,
`expect`, and `byte_order` are optional for verification. Unknown/duplicate
fields, malformed JSON, invalid UTF-8, NUL strings, and excessive nesting are
rejected. Manifests are limited to 16 MiB; compact recipes support large data
sets without listing their records. Integers accept exact decimal literals or
decimal strings, without floating-point conversion. Record bytes use exact
hexadecimal widths, so embedded NULs and large numeric values remain precise.

`byte_order` accepts `native`, or `little`/`big` matching the current host.
The existing tree encoding is native byte order; see `FORMAT.md`. POSIX runtime
portability does not make an existing native-endian image cross-endian.

Shapes use height 0 for an empty rootless image (`records: []`, no overrides).
Nonempty heights are 1..254. Missing occupancy overrides mean full nodes.
`node_keys` maps `root` or canonical zero-based paths such as `0.1` to key
counts. All overrides are validated together, including paths made nonexistent
by an ancestor's occupancy. Generation requires the record total to match the
shape. Post-operation expectations may omit `initial_shape`; original child
paths need not describe the resulting tree.

Preprocessing uses temporary merge-sorted pointer indexes. JSON object names
and shape paths are checked for duplicates with O(n log n) comparisons while
preserving the caller's input order. Numeric shape paths group each child
subtree into a contiguous range, so planning visits only affected ranges and
aggregates untouched full subtrees. Generation and exact-shape verification
reuse the sorted index for binary lookups. Index storage is O(n); neither
duplicate checking nor child grouping performs pairwise override scans.

Records must be in strict comparator order, with no equivalent keys. Values
are independent fixed-width byte sequences. The expected stream describes the
mapping after a checkpoint, not the sequence used to obtain it. The insertion
tester owns its independent first-insert-wins model, duplicate attempts,
return-code checks, operation logs, and failure policies. Expected records
must come from that model/specification rather than an observed-tree export.

`expect` accepts any combination of `height`, `nodes`, and `leaves`. These are
optional exact assertions, never inferred split increments. Without them,
observed totals are still checked against traversal and the persisted header.

## Record recipes

Instead of a record array, 8-byte keys and values may use:

```json
"records": {
  "kind": "i64-lattice", "count": "1000",
  "start": "-500", "step": "1",
  "value_start": "9999", "value_step": "-3"
}
```

Record i has signed native `int64_t` key `start + i * step` and value
`value_start + i * value_step`. Endpoints are checked without overflowing;
the unused step after the final record is never evaluated. Descending recipes
are supported with an appropriate comparator. Generation checks the complete
source order, including custom equivalence classes. Values need not be derived
from keys. `examples/lattice.json` exercises both inclusive signed boundaries.

## Builtin and external comparators

Builtin `profile` names are:

| Profile | Order |
| --- | --- |
| `i64-asc`, `i64-desc` | Native signed 8-byte integers |
| `u64-asc`, `u64-desc` | Native unsigned 8-byte integers |
| `bytes-asc`, `bytes-desc` | All key bytes, lexicographically |
| `bytes-nonunit` | Byte order, returning -17/31 |
| `prefix-asc`, `prefix-desc` | First `prefix_bytes` bytes; ignores suffix |
| `all-equal` | Every key equivalent; at most one fixture record |

Prefix profiles require `"prefix_bytes": N` in `[1,key_size]`. Each manifest
owns its comparator configuration; there is no shared mutable active profile.

For a test-owned shared library, replace the comparator object with:

```json
"comparator": {
  "library": "comparators/case_order.so",
  "id": "test/id-only/v1"
}
```

Library paths in the manifest resolve relative to that manifest, independent
of the caller's working directory. `--comparator-library PATH` overrides the
location, resolving relative paths from the working directory while retaining
the expected ID. Module filenames can use `.so`, `.dylib`, or another suffix.

Include `src/temp_compare.h` in the module and export the fixed C entry point
`temp_comparator_v1()`. It returns a static descriptor with ABI version 1,
`sizeof` the descriptor, a nonempty versioned ID, exact key width, zero reserved
field, and a callback matching `int (*)(const void *, const void *)`.
`tests/comparator_plugin.c` is a complete example comparing an ID while ignoring
a tag, using non-unit results. Build the module once per case:

```sh
# Darwin toolchain
cc -fPIC -dynamiclib -Iutils/temp/src utils/temp/tests/comparator_plugin.c -o case_order.dylib
# Toolchains with ELF-style shared-library flags
cc -fPIC -shared -Iutils/temp/src utils/temp/tests/comparator_plugin.c -o case_order.so
```

Supply the same comparator semantics to generation, the tester, loading, and
verification. The loader uses POSIX `dlopen`/`dlsym`, reports ABI/ID/width errors,
and keeps the module open for the entire check. Rebuilding the module does not
require rebuilding the utilities. Callbacks have no context argument; compile
case-specific immutable configuration into the module as needed.

## Results and reusable C APIs

Exit statuses are 0 success, 1 invalid tree/content, 2 invalid specification or
plugin, 3 image I/O failure, and 4 image resource exhaustion. Normal failures
report invariant, node, slot, record, and detail on stderr. `--json` writes one
JSON result on stdout on success or failure. Wide counts/indices are decimal
strings; byte mismatches include byte offset and expected/actual byte values.
Results also report observed height, records, nodes, leaves, free nodes,
comparator ID, and whether generation reused an existing output.

`src/temp_image.h` exposes record-source and comparator/context callbacks for
C callers, along with sparse shape descriptors, planning, generation and
verification. Callers can assert returned results in Unity without coupling
the checker to Unity's longjmp lifecycle. `src/temp_manifest.h` loads owned
manifest storage and exposes the corresponding borrowed descriptors; free it
after all descriptor use. Keep a loaded manifest at its original address,
because its callback context refers to its descriptor object.

`make -C utils/temp check-manifest` tests both ordinary executables and separately
linked core-free drivers against independent Python encodings. It covers
binary widths, sparse trees, legal alternative distributions, empty images,
free lists, resource limits, corruption, read-only success/failure, plugin
loading, and interoperability with the legacy library-backed tools.

`check-json` and `check-image` additionally enforce comparison budgets for
10,000/20,000-member objects and shape override sets. They cover late duplicate
names (including escaped equivalents), duplicate/nonexistent paths, preserved
input order, and generation/verification of a 20,000-override fixture. These
regressions count actual comparisons rather than asserting wall-clock limits.

The new backend requires C99 and POSIX.1-2008 file/dynamic-loader interfaces.
`DL_LIBS` is linker-probed; override it for cross-compilation. Override
`PLUGIN_CFLAGS` and `PLUGIN_LDFLAGS` for other compiler conventions. Python 3
is required only to run the existing and new executable regression suites.
