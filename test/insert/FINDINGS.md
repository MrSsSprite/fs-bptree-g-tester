# Observed insertion failure

The current core reports successful insertion while changing an existing
record's value. The tester detects this before flushing, and independently
decoding the retained image after teardown confirms the same value mismatch.
No insertion implementation was inspected or changed to obtain this result.

From the checkout root:

```sh
make insert
bin/insert --case permutations --layout normal
```

The failing scenario is `permutation-0-1`. It uses normal pointers, 128-byte
nodes, 8-byte signed integer keys, 8-byte values, ascending numeric comparison,
and a cache capacity of 4096. The comparator reads keys with `memcpy` and
returns -17, 0, or 31. Value bytes are independent of key values.

All four calls return `BPTR_E_SUCCESS`:

| Operation | Key | Input value bytes |
| --- | --- | --- |
| 1 | -2 | `c900dac300807700` |
| 2 | 0 | `88009bea00a51200` |
| 3 | 17 | `0e001edc0063f500` |
| 4 | 5 | `4f005ff500469000` |

Immediately after operation 4, the record for key -2 has value
`0000000000000000`, rather than `c900dac300807700`. The other three records
match their expected bytes. Four records fit in this root leaf, so these
four insertions do not require a split.

The failing case directory is printed on stderr and retained with
`operations.jsonl`, `expected.json`, `failure.txt`, and `instance.bptr`.
The expected manifest comes from the tester's copied-input reference model.
After building the utilities, the independent read-only verifier can check
the retained image:

```sh
make temp
bin/temp_verify --expect CASE_DIR/expected.json CASE_DIR/instance.bptr --json
```

That command should return a content failure for this reproducer. Teardown
unloads the tree after the live failure; the persisted mismatch is additional
evidence, rather than a replacement for the immediate live check. The report
identifies observable behavior and does not infer an implementation cause.
