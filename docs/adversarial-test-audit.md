# Adversarial Test Audit Report

**Date:** 2026-07-13
**Scope:** All 3 test units (`node_split`, `cache`, `fflush`) + test infrastructure, evaluated against 5 core library modules.
**Methodology:** 50-agent adversarial workflow — 6 audit phases + adversarial verification pass. 44 raw findings → 26 confirmed (18 refuted as false positives).

---

## HIGH Severity (2 findings)

### H1: Untested branch split `==` code path
**Location:** `core/src/bptr_node.c:824-837`

The branch node split has three insertion-position cases: `<`, `==`, and `>`. The `<` case is tested by `test_sing_brch_split_beg`, the `>` case by `test_sing_brch_split_end`. **The `==` case has zero test coverage.** This path has unique semantics: the new key IS the promoted key (not inserted into children), and vals are written with a different offset pattern than the other two cases. No existing test configuration can reach it because:
- `_end` always inserts at the right boundary → `new_elem_idx` > `node->key_count`
- `_beg` always inserts at the left boundary → `new_elem_idx` < `node->key_count`
- `_iter` always splits the leftmost leaf → promoted key always maps to position 0 in the parent

**Recommendation:** Create a targeted test that splits a middle leaf whose promoted key falls at `brch.up/2` in the parent, exercising the `==` case with full key/value/parent-pointer verification.

### H2: Latent data-corruption bug in erase helper macros
**Location:** `test/test_bptr_temp.c:36,44` — `_key_ers__generate` and `_val_ers__generate`

```c
// BROKEN: source is always (T*)node->keys + 1, not (T*)node->keys + idx + 1
memmove((T*)node->keys, (T*)node->keys + 1,
        (node->key_count - idx - 1) * sizeof(T));
```

Both macros hardcode source offset to `+ 1` instead of `+ idx + 1`, and destination to index 0 instead of `+ idx`. This works only for `idx == 0`. Any erase at a non-zero index **silently corrupts** the key/value arrays. Currently dormant because `_bptr_kv_ers` is never called by any test, but this is shared infrastructure linked into every test unit.

**Recommendation:** Fix to `memmove((T*)node->keys + idx, (T*)node->keys + idx + 1, ...)`. Same for `_val_ers__generate`.

---

## MEDIUM Severity (9 findings)

### M1: Missing last-element value verification
**Location:** `test/node_split/src/simple.c:148`

`test_simp_split_end` verifies all key/value pairs EXCEPT the value of the last element in the right child (the newly inserted `0xFFFFFFFF`). Only the key is checked; the value is never read. The other three split test functions (`_beg`, `_mid`, `_itr`) check both key AND value for every element.

**Recommendation:** Add value assertion for the last element of child[1].

### M2: Weak verification in `test_sing_brch_split_iter`
**Location:** `test/node_split/src/brch_sp.c:784-868`

This test iterates over ALL insertion positions (0 to `k`) but per-iteration verification is minimal: only checks that `key_count` sums match and root changed. It never verifies key/value content, leaf chain linkages, parent pointers, or internal node structure. This means position-specific bugs (especially in the `==` branch split case) go undetected despite being exercised.

**Recommendation:** Add comprehensive checks for at least 3 key positions (beginning, middle, end) that mirror the verification depth of `test_sing_brch_split_end`/`_beg`.

### M3: Missing `root_n->vals[1]` verification in iter test
**Location:** `test/node_split/src/brch_sp.c:850-861`

The iter test only checks `root_n->vals[0] == old_root` but never examines `root_n->vals[1]` (the right half of the split internal node). Cannot detect incorrect second-child assignment or missing parent-pointer updates in the right subtree.

**Recommendation:** Add verification that `root_n->vals[1]` is non-zero and points to a valid node with correct parent pointer.

### M4: Gaps in `_bptr_full_brch_verify`
**Location:** `test/test_bptr_brch_sp.c:119-157`

Four gaps:
- (a) Last leaf's `next == 0` never asserted
- (b) Root metadata (parent/prev/next/is_leaf/level) not verified
- (c) `node_cnt`/`record_cnt` never verified against expected counts
- (d) Assertion message "prev of first child not 0" is a copy-paste error — the assertion checks sibling linkage, not first-child prev

**Recommendation:** Add assertions for the missing checks; fix the misleading message.

### M5: Misleading assertion messages
**Location:** `test/node_split/src/brch_sp.c:156-161, 421-423`

Several assertion messages state the opposite of what the assertion checks. E.g., line 156 checks `right_keys <= left_keys` but the message says "right child has more key than left child." Line 421 checks `node->key_count >= next_n->key_count` but message says "node has less key than new_n after split."

**Recommendation:** Fix messages to match assertion direction.

### M6: Stale doc comment in `_node_promote`
**Location:** `core/src/bptr_node.c:232-233`

The doc claims `_node_promote` ignores `key` for branch nodes and uses `prm_n->keys[0]` instead. The actual implementation unconditionally uses the `key` parameter. All three callers correctly compute the key beforehand, so this is a documentation-only issue.

**Recommendation:** Update doc comment to reflect actual behavior: the caller is responsible for passing the correct promoted key.

### M7: Missing cross-L1 leaf chain boundary checks
**Location:** `test/test_bptr_brch_sp.c:442-457` (`_bptr_full_brch_casc_verify`)

The cascading verify function only checks leaf `prev`/`next` within each L1's bucket. Cross-L1 boundary links (e.g., last leaf of L1[N] → first leaf of L1[N+1]) are never asserted. The post-split test functions cover this via independent linked-list walks, but the pre-split verification helper has the gap.

**Recommendation:** Add cross-boundary assertions for non-first-L1 first-leaf `prev` and non-last-L1 last-leaf `next`.

### M8: Cache capacity tests don't reach cache-level validation
**Location:** `test/cache/src/cache.c:227-244`

The `cap=0` and `cap=1` tests call `bptr_init()` which validates `cache_capacity < 2` at the `bptr_init` level (line 74 of `bptr_core.c`), BEFORE `bptr_cache_init` is ever reached. The tests verify correct error behavior but the exercised code path is `bptr_init` parameter validation, not `bptr_cache_init` boundary checking. The `MSB-set` pool_cap guard in `bptr_cache_init:100` is genuinely untested.

**Recommendation:** Either rename/relocate tests to clarify they test `bptr_init` validation, or add direct tests for `bptr_cache_init` edge cases.

### M9: Missing cache error-path and lifecycle coverage
- `bptr_cache_reclaim()` (error-recovery in `bptr_node_new`) has zero coverage — no test triggers `PARENT_LOAD_ERR`
- `evict_remove` middle-of-queue and tail-only paths untested (only head-and-tail-simultaneously tested)
- Dirty-node implicit flush during `bptr_cache_deinit` not explicitly verified by any cache test (covered indirectly by node_split tests but without reload verification)

**Recommendation:** Add targeted tests for reclaim path, middle-of-queue eviction, and deferred-flush correctness.

---

## LOW Severity (15 findings)

| # | Location | Description |
|---|----------|-------------|
| L1 | `simple.c:161` | Comment copy-paste: `_beg` comment says "greater than" should be "less than" |
| L2 | `simple.c:283` | Dead code: spurious `val_ins_i64(node, 0, 0)` before fill loop in `_mid` |
| L3 | `simple.c:122,234,348` | Misleading message "child[0] key not match" for promoted-key-vs-boundary check |
| L4 | `brch_sp.c:187` | Missing `bptr_node_unload(bptr, root_n)` in `test_sing_brch_split_end` (inconsistency, not a functional bug) |
| L5 | `brch_sp.c:199` | Weak assertion: `>= 1` check for internal node key_count; message says "vals" not "keys" |
| L6 | `brch_sp_casc.c:659-660, 1006-1007` | Inconsistent monotonicity message: `_beg` says "not increasing", `_iter` says "not decreasing" |
| L7 | `brch_sp_casc.c:982-986` | Dead code block: abandoned cross-leaf monotonicity check with `(void)cur_key` |
| L8 | `brch_sp_casc.c:237-245,etc.` | TODO'd separator key verification (acknowledged, depends on incomplete `promote`) |
| L9 | `test_bptr_setup.c:21-46` | `_bptr_path_subdir` null-termination bug: `mkdir` called on unterminated string |
| L10 | `test_bptr_temp.c:57-65` | Fragile 2-slot flip-flop buffer in wrapper functions (undocumented limit) |
| L11 | `test_bptr_temp.c:116-167` | `_iu` template arrays byte-identical to non-`_iu` arrays — redundant maintenance burden |
| L12 | `test_bptr_setup.c:48-58` | No file cleanup if `_bptr_create`'s `bptr_init` partially succeeds then fails |
| L13 | `fflush/header.c:68-83` | Only 6 of 12+ serialized fields verified after create→load round-trip |
| L14 | `fflush/header.c:20-63` | No error path testing: 8 `bptr_io_fload` + 3 `bptr_io_fcreat` error branches untested |
| L15 | `cache.c:174-178` | Incomplete comment: doesn't mention node B's implicit eviction-and-flush during A's re-fetch |

---

## Summary

| Severity | Count | Key Themes |
|----------|-------|------------|
| **HIGH** | 2 | Untested `==` branch split path; latent data-corruption bug in erase helpers |
| **MEDIUM** | 9 | Missing value verification, weak iter-test assertions, verify-function gaps, misleading messages, cache boundary test doesn't reach cache code |
| **LOW** | 15 | Comment errors, dead code, fragile infrastructure, incomplete header checks, missing error-path coverage |

**Overall assessment:** The test suite correctly validates the primary success paths for leaf splits and simple branch splits. The critical gaps are: (1) the `==` branch split case is completely untested, (2) the erase helper macros contain a dormant data-corruption bug, and (3) the iterative split tests provide only structural-level (not content-level) verification. The `fflush` test unit is misnamed (tests header round-trip, not node flush) and has zero error-path coverage.
